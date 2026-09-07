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
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace charging::client {
namespace {
constexpr double pi = 3.14159265358979323846;

bool valid(const MapLocation &location)
{
    return std::isfinite(location.longitude) && std::isfinite(location.latitude)
        && std::abs(location.longitude) <= 180 && std::abs(location.latitude) <= 90;
}

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
}  // namespace

StationMapView::StationMapView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("stationMapView"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(0, 220);
    setCursor(Qt::OpenHandCursor);
    center_ = project({{}, 123.42, 41.75});
    controls_ = new QWidget(this);
    controls_->setObjectName(QStringLiteral("stationMapControls"));
    auto *tools = new QVBoxLayout(controls_);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(7);
    const auto button = [this, tools](const QString &text, const QString &name, const QString &label) {
        auto *result = new QPushButton(text, controls_);
        result->setObjectName(name);
        result->setProperty("role", "mapControl");
        result->setFixedSize(40, 40);
        result->setAccessibleName(label);
        result->setToolTip(label);
        tools->addWidget(result);
        return result;
    };
    auto *plus = button(QStringLiteral("＋"), QStringLiteral("stationMapZoomIn"), QStringLiteral("放大地图"));
    auto *minus = button(QStringLiteral("−"), QStringLiteral("stationMapZoomOut"), QStringLiteral("缩小地图"));
    locate_ = button(QStringLiteral("◎"), QStringLiteral("stationMapLocate"), QStringLiteral("回到当前选定位置"));
    connect(plus, &QPushButton::clicked, this, &StationMapView::zoomIn);
    connect(minus, &QPushButton::clicked, this, &StationMapView::zoomOut);
    connect(locate_, &QPushButton::clicked, this, [this] { if (location_) setCenter(*location_); });
    modeLabel_ = new QLabel(QStringLiteral("沈阳 · 离线默认地图（示意）"), this);
    modeLabel_->setObjectName(QStringLiteral("stationMapMode"));
    modeLabel_->setToolTip(QStringLiteral("内置原创地图示意，不是实测路网，不用于真实导航。"));
    modeLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
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

void StationMapView::setMapScriptUrl(const QUrl &url)
{
    if (url == scriptUrl_) return;
    scriptUrl_ = url;
    if (!url.isEmpty() && !webMap_) {
        webMap_ = new RouteMapView(this);
        webMap_->setObjectName(QStringLiteral("stationWebMapCanvas"));
        webMap_->setGeometry(rect());
        webMap_->lower();
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
            if (loading) statusLabel_->setText(QStringLiteral("正在加载腾讯地图…"));
            statusLabel_->setVisible(loading);
            updateControls();
        });
        connect(webMap_, &RouteMapView::retryAvailableChanged, retry_, &QWidget::setVisible);
        connect(webMap_, &RouteMapView::readyChanged, this, [this](bool ready) {
            if (ready) emit mapReady();
        });
    }
    if (webMap_) webMap_->setVisible(!url.isEmpty());
    modeLabel_->setVisible(url.isEmpty());
    for (auto *marker : markers_) marker->setVisible(url.isEmpty());
    webSceneDirty_ = true;
    applyWebScene();
    update();
}

void StationMapView::preload()
{
    if (preloadStarted_) return;
    preloadStarted_ = true;
    warming_ = !isVisible();
    // resizeEvent is deferred for a hidden widget. Size the nested canvas
    // explicitly before it creates the browser and its initial tile viewport.
    if (webMap_) webMap_->setGeometry(rect());
    if (scriptUrl_.isEmpty()) {
        fitStations();
        demoBackdrop_.prepare(size(), center_, scale_, devicePixelRatioF());
    }
    webSceneDirty_ = true;
    applyWebScene();
    if (scriptUrl_.isEmpty()) emit mapReady();
}

bool StationMapView::isReady() const
{
    return scriptUrl_.isEmpty() ? demoBackdrop_.isReady() : (webMap_ && webMap_->isReady());
}

void StationMapView::setCurrentLocation(const std::optional<MapLocation> &location)
{
    location_ = location && valid(*location) ? location : std::nullopt;
    locate_->setEnabled(location_.has_value());
    webSceneDirty_ = true;
    applyWebScene();
    update();
}

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
    fitStations();
}

void StationMapView::selectStation(qint64 stationId)
{
    selectedId_ = stationId;
    for (auto it = markers_.cbegin(); it != markers_.cend(); ++it) it.value()->setChecked(it.key() == stationId);
    if (webMap_ && isVisible()) webMap_->selectStation(stationId > 0 ? QString::number(stationId) : QString{});
    else webSceneDirty_ = true;
    updateMarkers();
}

void StationMapView::activateStation(qint64 stationId)
{
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

void StationMapView::setCenter(const MapLocation &location)
{
    if (!valid(location)) return;
    autoFit_ = false;
    center_ = project(location) - (usableViewport().center() - QRectF(rect()).center()) / scale_;
    if (webMap_) webMap_->setStationCenter(coordinate(location));
    updateMarkers();
    update();
}

void StationMapView::setViewportMargins(const QMargins &margins)
{
    if (margins_ == margins) return;
    margins_ = margins;
    if (webMap_ && isVisible()) webMap_->setStationViewport(margins);
    else webSceneDirty_ = true;
    if (autoFit_) fitStations();
    updateControls();
}

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
            coordinate({{}, 123.39, 41.70}), coordinate({{}, 123.44, 41.79})});
    }
    webMap_->setStationScene(scriptUrl_, scene);
    webSceneDirty_ = false;
}

void StationMapView::updateMarkers()
{
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

void StationMapView::updateControls()
{
    controls_->resize(40, 134);
    controls_->move(width() - 54, std::max(110, height() - margins_.bottom() - 134));
    modeLabel_->adjustSize();
    modeLabel_->move(16, height() - 25);
    statusLabel_->setGeometry(24, height() / 2 - 55, std::max(0, width() - 48), 110);
    retry_->setGeometry(width() / 2 - 75, height() / 2 + 60, 150, 40);
    controls_->raise(); statusLabel_->raise(); retry_->raise();
}

void StationMapView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (webMap_) webMap_->setGeometry(rect());
    if (autoFit_) fitStations(); else updateMarkers();
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

void StationMapView::changeZoom(int delta)
{
    autoFit_ = false;
    const QPointF anchor = center_ + (usableViewport().center() - QRectF(rect()).center()) / scale_;
    scale_ = std::clamp(scale_ * std::pow(1.5, delta), 2048.0, 268435456.0);
    center_ = anchor - (usableViewport().center() - QRectF(rect()).center()) / scale_;
    updateMarkers(); update();
}
void StationMapView::zoomIn() { if (webMap_) webMap_->zoomIn(); else changeZoom(1); }
void StationMapView::zoomOut() { if (webMap_) webMap_->zoomOut(); else changeZoom(-1); }
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
    if ((event->position() - pressPosition_).manhattanLength() > 4) dragging_ = true;
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
    if (event->angleDelta().y() != 0) changeZoom(event->angleDelta().y() > 0 ? 1 : -1);
    event->accept();
}
void StationMapView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) zoomIn();
}

void StationMapView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#edf1e8"));
    if (!scriptUrl_.isEmpty()) return;
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
