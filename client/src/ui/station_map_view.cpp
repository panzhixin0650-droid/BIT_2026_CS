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
    modeLabel_ = new QLabel(QStringLiteral("DEMO MAP · 离线示意地图"), this);
    modeLabel_->setObjectName(QStringLiteral("stationMapMode"));
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
    }
    if (webMap_) webMap_->setVisible(!url.isEmpty());
    modeLabel_->setVisible(url.isEmpty());
    for (auto *marker : markers_) marker->setVisible(url.isEmpty());
    webSceneDirty_ = true;
    applyWebScene();
    update();
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
    if (!webMap_ || scriptUrl_.isEmpty() || !isVisible() || !webSceneDirty_) return;
    QJsonArray stations;
    for (const auto &station : stations_) {
        auto item = coordinate({{}, station.longitude, station.latitude});
        // IDs cross JavaScript as strings, preserving the full qint64 range.
        item.insert(QStringLiteral("id"), QString::number(station.stationId));
        item.insert(QStringLiteral("available"), station.availablePileCount);
        stations.append(item);
    }
    webMap_->setStationScene(scriptUrl_, QJsonObject{
        {QStringLiteral("stations"), stations},
        {QStringLiteral("location"), location_ ? QJsonValue(coordinate(*location_)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("selected"), selectedId_ > 0 ? QString::number(selectedId_) : QString{}},
        {QStringLiteral("padding"), QJsonObject{{QStringLiteral("left"), margins_.left()},
            {QStringLiteral("top"), margins_.top()}, {QStringLiteral("right"), margins_.right()},
            {QStringLiteral("bottom"), margins_.bottom()}}}
    });
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
    // Decorative roads and park blocks share the projection, so they move with
    // markers. These are a schematic, not geographic data or navigable roads.
    const auto point = [this](double lng, double lat) { return pointForLocation({{}, lng, lat}); };
    const double unit = std::clamp(scale_ / 2000000.0, 0.5, 1.8);
    for (int x = -8; x <= 8; ++x) {
        for (int y = -12; y <= 12; ++y) {
            QRectF block(point(123.42 + x * .027 + .002, 41.75 + y * .018 + .002),
                         point(123.42 + x * .027 + .024, 41.75 + y * .018 + .015));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor((x + y) % 5 == 0 ? "#d9e6ce" : "#e4e9df"));
            painter.drawRoundedRect(block.normalized(), 5 * unit, 5 * unit);
        }
    }
    painter.setBrush(Qt::NoBrush);
    QPainterPath river;
    river.moveTo(point(123.25, 41.752));
    river.cubicTo(point(123.38, 41.737), point(123.42, 41.772), point(123.60, 41.745));
    painter.setPen(QPen(QColor("#c3dbd5"), 22 * unit, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(river);
    for (int axis = 0; axis < 2; ++axis) {
        for (int i = -12; i <= 12; ++i) {
            const QPointF start = axis == 0 ? point(123.42 + i * .027, 41.48) : point(123.02, 41.75 + i * .018);
            const QPointF end = axis == 0 ? point(123.42 + i * .027, 42.02) : point(123.82, 41.75 + i * .018);
            painter.setPen(QPen(QColor("#dce2d5"), 7 * unit)); painter.drawLine(start, end);
            painter.setPen(QPen(QColor("#fffef7"), 4 * unit)); painter.drawLine(start, end);
        }
    }
    QPainterPath avenue;
    avenue.moveTo(point(123.39, 41.86));
    avenue.cubicTo(point(123.40, 41.76), point(123.45, 41.77), point(123.43, 41.64));
    painter.setPen(QPen(QColor("#dedbc2"), 13 * unit)); painter.drawPath(avenue);
    painter.setPen(QPen(QColor("#fff8df"), 9 * unit)); painter.drawPath(avenue);
    QFont font = painter.font(); font.setPixelSize(16); font.setLetterSpacing(QFont::AbsoluteSpacing, 3);
    painter.setFont(font); painter.setPen(QColor("#94a38f"));
    painter.drawText(point(123.376, 41.788), QStringLiteral("和平区"));
    painter.drawText(point(123.447, 41.724), QStringLiteral("浑南区"));
    font.setPixelSize(11); painter.setFont(font); painter.setPen(QColor("#80a79d"));
    painter.drawText(point(123.385, 41.754), QStringLiteral("示意水域"));
    if (location_) {
        const QPointF current = pointForLocation(*location_);
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(42, 142, 140, 25));
        painter.drawEllipse(current, 22, 22);
        painter.setPen(QPen(Qt::white, 3)); painter.setBrush(QColor("#2a8e8c"));
        painter.drawEllipse(current, 8, 8);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0); font.setPixelSize(11); painter.setFont(font);
        painter.setPen(QColor("#28766e"));
        painter.drawText(QRectF(current.x() - 48, current.y() + 15, 96, 20), Qt::AlignCenter, QStringLiteral("当前选定位置"));
    }
}

}  // namespace charging::client
