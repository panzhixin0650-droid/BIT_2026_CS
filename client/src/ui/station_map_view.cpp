#include "ui/station_map_view.h"

#include "ui/route_map_view.h"
#include <QAbstractButton>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QWheelEvent>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

// 本文件实现电站地图视图：在线腾讯地图画布或离线绘制演示
namespace charging::client {
namespace {
constexpr double pi = 3.14159265358979323846;

bool valid(const MapLocation &location)
{
    return std::isfinite(location.longitude) && std::isfinite(location.latitude)
        && std::abs(location.longitude) <= 180 && std::abs(location.latitude) <= 90;
}

// 经纬度转墨卡托归一化坐标，纬度先截断到可投影范围
QPointF project(const MapLocation &location)
{
    const double latitude = std::clamp(location.latitude, -85.05112878, 85.05112878) * pi / 180;
    return {(location.longitude + 180) / 360,
            (1 - std::asinh(std::tan(latitude)) / pi) / 2};
}

QJsonObject coordinate(const MapLocation &location)
{
    return {{QStringLiteral("lat"), location.latitude}, {QStringLiteral("lng"), location.longitude}};
}

// 自绘电站标记按钮，颜色区分是否有空闲桩与选中态
class StationMarker final : public QAbstractButton {
public:
    explicit StationMarker(QWidget *parent) : QAbstractButton(parent)
    {
        setFixedSize(52, 46);
        setCheckable(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
    }
    int available = 0;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int radius = isChecked() ? 17 : 14;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(32, 61, 48, 24));
        painter.drawEllipse(QPointF(26, 25), radius + 3, radius + 3);
        painter.setBrush(QColor(available <= 0 ? "#8b9d90" : isChecked() ? "#388456" : "#245c45"));
        painter.setPen(QPen(Qt::white, isChecked() || hasFocus() ? 3 : 2));
        painter.drawEllipse(QPointF(26, 22), radius, radius);
        QPainterPath bolt;
        bolt.moveTo(28, 11); bolt.lineTo(19, 24); bolt.lineTo(25, 24);
        bolt.lineTo(23, 33); bolt.lineTo(33, 19); bolt.lineTo(27, 19);
        bolt.closeSubpath();
        painter.fillPath(bolt, Qt::white);
    }
};

class LoadingMapPreview final : public QWidget {
public:
    LoadingMapPreview(QWidget *parent, std::function<void(QPainter &)> draw)
        : QWidget(parent), draw_(std::move(draw))
    {
        setObjectName(QStringLiteral("stationLoadingPreview"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
protected:
    void paintEvent(QPaintEvent *) override { QPainter painter(this); draw_(painter); }
private:
    std::function<void(QPainter &)> draw_;
};
}  // namespace

// 构造视图：初始化离线预览、缩放与全景等控件
StationMapView::StationMapView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("stationMapView"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(0, 220);
    setCursor(Qt::OpenHandCursor);
    center_ = project({{}, 123.42, 41.75});
    loadingPreview_ = new LoadingMapPreview(this, [this](QPainter &painter) { paintOfflineMap(painter); });
    loadingPreview_->hide();
    controls_ = new QWidget(this);
    controls_->setObjectName(QStringLiteral("stationMapControls"));
    auto *tools = new QGridLayout(controls_);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(7);
    const auto button = [this, tools](const QString &text, const QString &name, const QString &label) {
        auto *result = new QPushButton(text, controls_);
        result->setObjectName(name);
        result->setProperty("role", "mapControl");
        result->setFixedSize(44, 44);
        result->setAccessibleName(label);
        result->setToolTip(label);
        const int index = tools->count();
        tools->addWidget(result, index / 2, index % 2);
        return result;
    };
    auto *plus = button(QStringLiteral("＋"), QStringLiteral("stationMapZoomIn"), QStringLiteral("放大地图"));
    auto *minus = button(QStringLiteral("−"), QStringLiteral("stationMapZoomOut"), QStringLiteral("缩小地图"));
    connect(plus, &QPushButton::clicked, this, &StationMapView::zoomIn);
    connect(minus, &QPushButton::clicked, this, &StationMapView::zoomOut);
    auto *overview = button(QStringLiteral("全景"), QStringLiteral("stationMapOverview"), QStringLiteral("查看全部电站"));
    locate_ = button(QStringLiteral("附近"), QStringLiteral("stationMapLocate"), QStringLiteral("回到自己的选定位置附近"));
    for (auto *control : {overview, locate_}) control->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600;"));
    connect(overview, &QPushButton::clicked, this, [this] { beginInteraction(); fitStations(); });
    connect(locate_, &QPushButton::clicked, this, [this] {
        if (location_) { beginInteraction(); scale_ = 8388608.0; setCenter(*location_); }
    });
    modeLabel_ = new QLabel(QStringLiteral("沈阳离线地图 · <a href=\"https://www.openstreetmap.org/copyright\">© OpenStreetMap</a>"), this);
    modeLabel_->setObjectName(QStringLiteral("stationMapMode"));
    modeLabel_->setToolTip(QStringLiteral("通过 Overpass API 预先获取的真实地图数据，ODbL 1.0；非实时路况或真实导航。"));
    modeLabel_->setOpenExternalLinks(true);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("stationMapStatus"));
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setWordWrap(true);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->hide();
    retry_ = new QPushButton(QStringLiteral("重新加载地图"), this);
    retry_->setObjectName(QStringLiteral("stationMapRetry"));
    retry_->hide();
    connect(retry_, &QPushButton::clicked, this, [this] { if (webMap_) webMap_->retry(); });
}

StationMapView::~StationMapView()
{
    // The worker owns only geometry/QImage, never this widget. Finish it before
    // QApplication/font resources are destroyed (bounded local computation).
    if (preloadThread_) preloadThread_->wait();
}

// 设置脚本地址后按需创建网页地图，并接管选中与加载状态
void StationMapView::setMapScriptUrl(const QUrl &url)
{
    if (url == scriptUrl_) return;
    scriptUrl_ = url;
    if (!url.isEmpty() && !webMap_) {
        webMap_ = new RouteMapView(this);
        webMap_->setObjectName(QStringLiteral("stationWebMapCanvas"));
        webMap_->setGeometry(rect());
        webMap_->lower();
        connect(webMap_, &RouteMapView::interactionStarted, this, &StationMapView::beginInteraction);
        connect(webMap_, &RouteMapView::stationSelected, this, [this](const QString &id) {
            bool ok = false;
            const qint64 stationId = id.toLongLong(&ok);
            if (id.isEmpty()) activateStation(0);
            else if (ok && std::any_of(stations_.cbegin(), stations_.cend(), [stationId](const auto &s) {
                return s.stationId == stationId;
            })) activateStation(stationId);
        });
        connect(webMap_, &RouteMapView::statusChanged, this, [this](const QString &message, bool error) {
            statusLabel_->setText(message);
            statusLabel_->setVisible(error);
            updateControls();
        });
        connect(webMap_, &RouteMapView::loadingChanged, this, [this](bool loading) {
            if (loading) statusLabel_->hide(); // Compact preview caption, not a blocking centre panel.
            updateControls();
        });
        connect(webMap_, &RouteMapView::retryAvailableChanged, retry_, &QWidget::setVisible);
        connect(webMap_, &RouteMapView::baseMapReadyChanged, this, [this](bool ready) {
            updatePreview();
            if (ready) emit mapReady();
        });
    }
    if (webMap_) webMap_->setVisible(!url.isEmpty());
    updatePreview();
    for (auto *marker : markers_) marker->setVisible(url.isEmpty());
    webSceneDirty_ = true;
    applyWebScene();
    update();
}

// 预热：先算好离线底图，隐藏时也提前给画布定尺寸
void StationMapView::preload()
{
    if (preloadStarted_) return;
    preloadStarted_ = true;
    warming_ = !isVisible();
    // resizeEvent is deferred for a hidden widget. Size the nested canvas
    // explicitly before it creates the browser and its initial tile viewport.
    if (webMap_) webMap_->setGeometry(rect());
    fitStations();
    const auto prepared = std::make_shared<DemoMapBackdrop>();
    const QSize viewportSize = size();
    const QPointF viewportCenter = center_;
    const double viewportScale = scale_;
    const qreal pixelRatio = devicePixelRatioF();
    // 底图计算放到后台线程，完成后回主线程替换并重绘
    preloadThread_ = QThread::create([prepared, viewportSize, viewportCenter, viewportScale, pixelRatio] {
        prepared->prepare(viewportSize, viewportCenter, viewportScale, pixelRatio);
    });
    preloadThread_->setParent(this);
    connect(preloadThread_, &QThread::finished, this, [this, prepared] {
        demoBackdrop_ = std::move(*prepared);
        preloadThread_->deleteLater();
        preloadThread_ = nullptr;
        update();
        loadingPreview_->update();
        if (scriptUrl_.isEmpty()) emit mapReady();
    });
    preloadThread_->start();
    webSceneDirty_ = true;
    applyWebScene();
}

bool StationMapView::isReady() const
{
    return scriptUrl_.isEmpty() ? demoBackdrop_.isReady() : (webMap_ && webMap_->isReady() && webMap_->isBaseMapReady());
}

void StationMapView::setCurrentLocation(const std::optional<MapLocation> &location)
{
    location_ = location && valid(*location) ? location : std::nullopt;
    locate_->setEnabled(location_.has_value());
    webSceneDirty_ = true;
    applyWebScene();
    update();
}

// 接收电站列表，过滤无效坐标并重建标记
void StationMapView::setStations(const QList<protocol::StationDto> &stations)
{
    receivedStations_ = true;
    stations_.clear();
    for (const auto &station : stations) {
        if (station.stationId > 0 && valid({{}, station.longitude, station.latitude})) stations_.append(station);
    }
    const bool selectedExists = std::any_of(stations_.cbegin(), stations_.cend(), [this](const auto &s) {
        return s.stationId == selectedId_;
    });
    if (!selectedExists) selectedId_ = 0;
    qDeleteAll(markers_);
    markers_.clear();
    if (scriptUrl_.isEmpty()) {
        for (const auto &station : stations_) {
            auto *marker = new StationMarker(this);
            marker->setObjectName(QStringLiteral("stationMarker_%1").arg(station.stationId));
            marker->setAccessibleName(QStringLiteral("%1，空闲 %2/%3，查看站点信息")
                .arg(station.name).arg(station.availablePileCount).arg(station.totalPileCount));
            marker->available = station.availablePileCount;
            marker->setChecked(station.stationId == selectedId_);
            connect(marker, &QAbstractButton::clicked, this, [this, id = station.stationId] { activateStation(id); });
            markers_.insert(station.stationId, marker);
        }
    }
    webSceneDirty_ = true;
    applyWebScene();
    if (autoFit_ && !webMap_) fitStations(); else updateMarkers();
}

void StationMapView::selectStation(qint64 stationId)
{
    selectedId_ = stationId;
    for (auto it = markers_.cbegin(); it != markers_.cend(); ++it) it.value()->setChecked(it.key() == stationId);
    if (webMap_ && isVisible()) webMap_->selectStation(stationId > 0 ? QString::number(stationId) : QString{});
    else webSceneDirty_ = true;
    updateMarkers();
}

// 点击空白视为取消选择，点中标记则通知外部打开预览
void StationMapView::activateStation(qint64 stationId)
{
    if (stationId <= 0) { autoFit_ = false; emit backgroundClicked(); return; }
    selectStation(stationId);
    emit stationSelected(stationId);
}

QRectF StationMapView::usableViewport() const
{
    QRectF area = QRectF(rect()).marginsRemoved(QMarginsF(margins_));
    if (area.width() < 80 || area.height() < 100) return QRectF(rect()).adjusted(28, 100, -64, -28);
    return area;
}

QPointF StationMapView::pointForLocation(const MapLocation &location) const
{
    return QRectF(rect()).center() + (project(location) - center_) * scale_;
}

// 根据所有电站与当前位置计算合适缩放和中心
void StationMapView::fitStations()
{
    autoFit_ = true;
    QList<QPointF> points;
    for (const auto &station : stations_) points.append(project({{}, station.longitude, station.latitude}));
    if (location_) points.append(project(*location_));
    if (!receivedStations_) {
        points = {project({{}, 123.40, 41.79}), project({{}, 123.43, 41.70})};
    }
    if (!points.isEmpty()) {
        double left = points.first().x(), right = left, top = points.first().y(), bottom = top;
        for (const auto &point : points) {
            left = std::min(left, point.x()); right = std::max(right, point.x());
            top = std::min(top, point.y()); bottom = std::max(bottom, point.y());
        }
        const QRectF area = usableViewport();
        scale_ = std::clamp(std::min(area.width() / std::max(right - left, 0.000035),
                                     area.height() / std::max(bottom - top, 0.000035)) * 0.90,
                            2048.0, 268435456.0);
        center_ = QPointF((left + right) / 2, (top + bottom) / 2)
            - (area.center() - QRectF(rect()).center()) / scale_;
    }
    if (webMap_ && isVisible()) webMap_->fitStations();
    else webSceneDirty_ = true;
    updateMarkers();
    update();
}

void StationMapView::focusStation(qint64 stationId)
{
    for (const auto &station : stations_) {
        if (station.stationId != stationId) continue;
        scale_ = 8388608.0;
        setCenter({station.address, station.longitude, station.latitude});
        return;
    }
}

void StationMapView::setCenter(const MapLocation &location)
{
    if (!valid(location)) return;
    autoFit_ = false;
    center_ = project(location) - (usableViewport().center() - QRectF(rect()).center()) / scale_;
    if (webMap_) {
        auto point = coordinate(location);
        point.insert(QStringLiteral("zoom"), 15);
        webMap_->setStationCenter(point);
    }
    updateMarkers();
    update();
}

void StationMapView::setViewportMargins(const QMargins &margins)
{
    if (margins_ == margins) return;
    margins_ = margins;
    if (webMap_ && isVisible()) webMap_->setStationViewport(margins);
    else webSceneDirty_ = true;
    if (autoFit_ && !webMap_) fitStations();
    updateControls();
}

// 把电站、定位、选中和边距整理成JSON场景交给网页地图
void StationMapView::applyWebScene()
{
    if (!webMap_ || scriptUrl_.isEmpty() || (!isVisible() && !warming_) || !webSceneDirty_) return;
    QJsonArray stations;
    for (const auto &station : stations_) {
        auto item = coordinate({{}, station.longitude, station.latitude});
        // IDs cross JavaScript as strings, preserving the full qint64 range.
        item.insert(QStringLiteral("id"), QString::number(station.stationId));
        item.insert(QStringLiteral("available"), station.availablePileCount);
        stations.append(item);
    }
    QJsonObject scene{
        {QStringLiteral("stations"), warming_ ? QJsonArray{} : stations},
        {QStringLiteral("location"), !warming_ && location_ ? QJsonValue(coordinate(*location_)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("selected"), !warming_ && selectedId_ > 0 ? QString::number(selectedId_) : QString{}},
        {QStringLiteral("padding"), QJsonObject{{QStringLiteral("left"), margins_.left()},
            {QStringLiteral("top"), margins_.top()}, {QStringLiteral("right"), margins_.right()},
            {QStringLiteral("bottom"), margins_.bottom()}}}
    };
    // A public, fixed Shenyang viewport, never account data or a geocoding /
    // station.list request before authentication. It covers the demo area at
    // the same broad zoom that the first station response will use.
    if (warming_ || !receivedStations_) {
        scene.insert(QStringLiteral("defaultBounds"), QJsonArray{
            coordinate({{}, 123.40, 41.79}), coordinate({{}, 123.43, 41.70})});
    }
    webMap_->setStationScene(scriptUrl_, scene);
    webSceneDirty_ = false;
}

// 按当前视图重新摆放离线标记，超出视口则隐藏
void StationMapView::updateMarkers()
{
    loadingPreview_->update();
    for (const auto &station : stations_) {
        if (auto *marker = markers_.value(station.stationId)) {
            const QPoint point = pointForLocation({{}, station.longitude, station.latitude}).toPoint();
            marker->move(point - QPoint(26, 22));
            marker->setVisible(scriptUrl_.isEmpty() && rect().intersects(marker->geometry()));
        }
    }
    if (auto *selected = markers_.value(selectedId_)) selected->raise();
    controls_->raise();
}

// 网页底图未就绪时显示离线预览并切换版权文案
void StationMapView::updatePreview()
{
    const bool preview = !scriptUrl_.isEmpty() && webMap_ && !webMap_->isBaseMapReady();
    loadingPreview_->setVisible(preview);
    modeLabel_->setVisible(scriptUrl_.isEmpty() || preview);
    modeLabel_->setText(preview
        ? QStringLiteral("腾讯加载中 · <a href=\"https://www.openstreetmap.org/copyright\">© OSM 离线预览</a>")
        : QStringLiteral("沈阳离线地图 · <a href=\"https://www.openstreetmap.org/copyright\">© OpenStreetMap</a>"));
    loadingPreview_->raise();
    updateControls();
}

void StationMapView::updateControls()
{
    controls_->resize(95, 95);
    controls_->move(qMax(8, width() - 107), 12);
    modeLabel_->adjustSize();
    modeLabel_->move(16, height() - 25);
    statusLabel_->setGeometry(24, height() / 2 - 55, std::max(0, width() - 48), 110);
    retry_->setGeometry(width() / 2 - 75, height() / 2 + 60, 150, 40);
    controls_->raise(); statusLabel_->raise(); retry_->raise();
    modeLabel_->raise();
}

// 尺寸变化时同步内嵌画布与控件位置
void StationMapView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (webMap_) webMap_->setGeometry(rect());
    loadingPreview_->setGeometry(rect());
    if (autoFit_ && !webMap_) fitStations(); else updateMarkers();
    updateControls();
}

void StationMapView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (warming_) {
        warming_ = false;
        webSceneDirty_ = true;
    }
    // One foreground attempt may take over a failed background warm-up. After
    // that, the existing explicit Retry button owns recovery.
    if (webMap_ && !webMap_->isPreloaded() && !webMap_->isLoading()) webSceneDirty_ = true;
    applyWebScene();
}

// 以可用视口中心为锚点缩放，限制最大最小比例
void StationMapView::changeZoom(int delta)
{
    autoFit_ = false;
    const QPointF anchor = center_ + (usableViewport().center() - QRectF(rect()).center()) / scale_;
    scale_ = std::clamp(scale_ * std::pow(1.5, delta), 2048.0, 268435456.0);
    center_ = anchor - (usableViewport().center() - QRectF(rect()).center()) / scale_;
    updateMarkers(); update();
}
void StationMapView::beginInteraction()
{
    autoFit_ = false;
    emit interactionStarted();
}
void StationMapView::zoomIn() { beginInteraction(); if (webMap_) webMap_->zoomIn(); else changeZoom(1); }
void StationMapView::zoomOut() { beginInteraction(); if (webMap_) webMap_->zoomOut(); else changeZoom(-1); }
// 鼠标拖拽平移地图，超过阈值才算拖动而非点击
void StationMapView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    pressPosition_ = lastPosition_ = event->position();
    dragging_ = false;
    setCursor(Qt::ClosedHandCursor);
}
void StationMapView::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) return;
    if (!dragging_ && (event->position() - pressPosition_).manhattanLength() > 4) {
        dragging_ = true; beginInteraction();
    }
    if (!dragging_) return;
    autoFit_ = false;
    center_ -= (event->position() - lastPosition_) / scale_;
    lastPosition_ = event->position();
    updateMarkers(); update();
}
void StationMapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    setCursor(Qt::OpenHandCursor);
    if (!dragging_ && (event->position() - pressPosition_).manhattanLength() <= 4) activateStation(0);
}
void StationMapView::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() != 0) {
        beginInteraction(); changeZoom(event->angleDelta().y() > 0 ? 1 : -1);
    }
    event->accept();
}
void StationMapView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) { dragging_ = true; zoomIn(); }
}

void StationMapView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#edf1e8"));
    if (!scriptUrl_.isEmpty()) return;
    paintOfflineMap(painter);
}

// 离线模式绘制底图，并标出当前选定位置
void StationMapView::paintOfflineMap(QPainter &painter)
{
    if (!preloadStarted_) preload();
    if (!demoBackdrop_.isReady()) {
        painter.fillRect(rect(), QColor("#f4f3ec"));
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    demoBackdrop_.paint(painter, size(), center_, scale_, devicePixelRatioF());
    if (location_) {
        const QPointF current = pointForLocation(*location_);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(42, 142, 140, 25));
        painter.drawEllipse(current, 22, 22);
        painter.setPen(QPen(Qt::white, 3)); painter.setBrush(QColor("#2a8e8c"));
        painter.drawEllipse(current, 8, 8);
        QFont font = painter.font();
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0); font.setPixelSize(11); painter.setFont(font);
        painter.setPen(QColor("#28766e"));
        painter.drawText(QRectF(current.x() - 48, current.y() + 15, 96, 20), Qt::AlignCenter, QStringLiteral("当前选定位置"));
    }
}

}  // namespace charging::client
