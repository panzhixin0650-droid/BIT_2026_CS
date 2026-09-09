#include "vehicle/vehicle_pages.h"

#include "common/client_tokens.h"
#include "common/charging_session_state.h"
#include "common/charging_progress_ring.h"
#include "ui/route_map_view.h"
#include "ui/avatar_art.h"
#include "ui/client_theme.h"
#include "ui/pricing_hint.h"
#include "ui/pricing_info_button.h"
#include "ui/reservation_hint.h"
#include "ui/station_discovery_policy.h"
#include "ui/station_map_view.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QDoubleValidator>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace charging::client {
namespace {

QString statusText(protocol::OrderStatus status)
{
    using S = protocol::OrderStatus;
    switch (status) {
    case S::Reserved: return QStringLiteral("已预约");
    case S::Charging: return QStringLiteral("充电中");
    case S::PendingPayment: return QStringLiteral("待支付");
    case S::Completed: return QStringLiteral("已完成");
    case S::Cancelled: return QStringLiteral("已取消");
    }
    return {};
}

QString durationText(qint64 seconds)
{
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString orderDurationText(qint64 seconds)
{
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    return hours > 0 ? QStringLiteral("%1 小时 %2 分钟").arg(hours).arg(minutes)
                     : QStringLiteral("%1 分钟").arg(minutes);
}

QString moneyText(qint64 cents)
{
    return QStringLiteral("¥%1.%2")
        .arg(cents / 100)
        .arg(cents % 100, 2, 10, QLatin1Char('0'));
}

QString dateTimeText(const QString &isoDateTime)
{
    const QDateTime parsed = QDateTime::fromString(isoDateTime, Qt::ISODate);
    return parsed.isValid() ? parsed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                            : isoDateTime;
}

QString orderModeText(protocol::OrderMode mode)
{
    return mode == protocol::OrderMode::Reservation ? QStringLiteral("预约充电")
                                                     : QStringLiteral("直接充电");
}

QString orderStatusColor(protocol::OrderStatus status)
{
    using S = protocol::OrderStatus;
    switch (status) {
    case S::Reserved: return QStringLiteral("#a75b00");
    case S::Charging: return QStringLiteral("#245c45");
    case S::PendingPayment: return QStringLiteral("#b43b32");
    case S::Completed: return QStringLiteral("#386a3c");
    case S::Cancelled: return QStringLiteral("#697969");
    }
    return QStringLiteral("#697969");
}

QPushButton *actionButton(const QString &text, QWidget *parent, bool primary = false)
{
    auto *button = new QPushButton(text, parent);
    button->setMinimumHeight(primary ? tokens::CriticalTouch : tokens::Touch);
    if (primary) button->setProperty("role", "primary");
    return button;
}

QFrame *card(QWidget *parent)
{
    auto *result = new QFrame(parent);
    result->setProperty("role", "card");
    return result;
}

}  // namespace

VehicleHomePage::VehicleHomePage(const QUrl &mapScriptUrl, QWidget *parent)
    : QWidget(parent), hasOnlineMapCanvas_(!mapScriptUrl.isEmpty())
{
    setObjectName(QStringLiteral("vehicleHomePage"));
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(tokens::Space, 12, tokens::Space, 12);
    root->setSpacing(tokens::Space);

    mapStack_ = new QStackedWidget(this);
    mapStack_->setObjectName(QStringLiteral("vehicleMapStack"));
    mapStack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    stationMap_ = new StationMapView(mapStack_);
    stationMap_->setObjectName(QStringLiteral("vehicleStationMap"));
    stationMap_->setMapScriptUrl(mapScriptUrl);
    stationMap_->setCurrentLocation(location_);
    stationMap_->setViewportMargins(QMargins(22, 22, 76, 54));
    routeMap_ = new RouteMapView(mapStack_);
    routeMap_->setObjectName(QStringLiteral("vehicleRouteMap"));
    mapStack_->addWidget(stationMap_);
    mapStack_->addWidget(routeMap_);
    auto *mapContainer = new QWidget(this);
    auto *mapLayout = new QGridLayout(mapContainer);
    mapLayout->setContentsMargins(0, 0, 0, 0);
    mapLayout->addWidget(mapStack_, 0, 0);
    auto *routeOverlay = new QFrame(mapContainer);
    routeOverlay->setObjectName(QStringLiteral("vehicleRouteOverlay"));
    routeOverlay->setMaximumWidth(460);
    routeOverlay->setStyleSheet(QStringLiteral(
        "QFrame#vehicleRouteOverlay { background: rgba(255,253,247,238); "
        "border:1px solid #d7e0d2;border-radius:14px; }"
        "QFrame#vehicleRouteOverlay QPushButton { min-height:52px; }"));
    auto *routeOverlayLayout = new QVBoxLayout(routeOverlay);
    routeOverlayLayout->setContentsMargins(10, 10, 10, 10);
    routeOverlayLayout->setSpacing(8);
    auto *routeOverlayActions = new QHBoxLayout;
    routeDetailsButton_ = actionButton(QStringLiteral("展开路线"), routeOverlay);
    routeDetailsButton_->setObjectName(QStringLiteral("vehicleRouteDetailsButton"));
    routeDetailsButton_->setCheckable(true);
    routeDetailsButton_->setAccessibleName(QStringLiteral("展开路线详情"));
    exitRouteFullscreen_ = actionButton(QStringLiteral("退出全屏"), routeOverlay);
    exitRouteFullscreen_->setObjectName(QStringLiteral("vehicleExitRouteFullscreen"));
    exitRouteFullscreen_->setMinimumWidth(112);
    routeOverlayActions->addWidget(routeDetailsButton_);
    routeOverlayActions->addWidget(exitRouteFullscreen_);
    routeOverlayLayout->addLayout(routeOverlayActions);
    routeDetails_ = new QPlainTextEdit(routeOverlay);
    routeDetails_->setObjectName(QStringLiteral("vehicleRouteDetails"));
    routeDetails_->setReadOnly(true);
    routeDetails_->setMaximumHeight(210);
    routeDetails_->setMinimumWidth(420);
    routeDetails_->hide();
    routeOverlayLayout->addWidget(routeDetails_);
    routeOverlay->hide();
    mapLayout->addWidget(routeOverlay, 0, 0, Qt::AlignTop | Qt::AlignLeft);
    root->addWidget(mapContainer, 65);

    auto *panel = card(this);
    sidePanel_ = panel;
    panel->setObjectName(QStringLiteral("vehicleHomeSidePanel"));
    panel->setMinimumWidth(310);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 16, 18, 16);
    panelLayout->setSpacing(10);
    greeting_ = new QLabel(QStringLiteral("附近充电站"), panel);
    greeting_->setProperty("role", "sectionTitle");
    panelLayout->addWidget(greeting_);
    panelStack_ = new QStackedWidget(panel);
    panelLayout->addWidget(panelStack_, 1);
    root->addWidget(panel, 35);

    discoveryPanel_ = new QWidget(panelStack_);
    auto *discoveryLayout = new QVBoxLayout(discoveryPanel_);
    discoveryLayout->setContentsMargins(0, 0, 0, 0);
    search_ = new QLineEdit(discoveryPanel_);
    search_->setObjectName(QStringLiteral("vehicleStationSearch"));
    search_->setPlaceholderText(QStringLiteral("⌕  搜索电站、区域或地址"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleDescription(QStringLiteral("输入关键词后按回车搜索"));
    search_->setMinimumHeight(tokens::Touch);
    search_->setStyleSheet(QStringLiteral(
        "QLineEdit { background:#edf2e9;border:none;border-radius:16px;"
        "padding:0 16px;color:#36523f; }"
        "QLineEdit:focus { border:2px solid #6f927a;padding:0 14px; }"));
    discoveryLayout->addWidget(search_);
    auto *locationRow = new QHBoxLayout;
    locationInput_ = new QLineEdit(QStringLiteral("演示位置"), discoveryPanel_);
    locationInput_->setObjectName(QStringLiteral("vehicleLocationInput"));
    locationInput_->setPlaceholderText(QStringLiteral("修改当前位置"));
    locationInput_->setMinimumHeight(tokens::Touch);
    locationRow->addWidget(locationInput_, 1);
    auto *locate = actionButton(QStringLiteral("确定位置"), discoveryPanel_);
    locate->setObjectName(QStringLiteral("vehicleLocationButton"));
    locationRow->addWidget(locate);
    discoveryLayout->addLayout(locationRow);
    message_ = new QLabel(discoveryPanel_);
    message_->setObjectName(QStringLiteral("vehicleHomeMessage"));
    message_->setWordWrap(true);
    discoveryLayout->addWidget(message_);
    auto *scroll = new QScrollArea(discoveryPanel_);
    scroll->setWidgetResizable(true);
    auto *listBody = new QWidget(scroll);
    stationList_ = new QVBoxLayout(listBody);
    stationList_->setContentsMargins(0, 0, 0, 0);
    stationList_->setSpacing(8);
    stationList_->addStretch();
    scroll->setWidget(listBody);
    discoveryLayout->addWidget(scroll, 1);
    panelStack_->addWidget(discoveryPanel_);

    detailPanel_ = new QWidget(panelStack_);
    auto *detailLayout = new QVBoxLayout(detailPanel_);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    auto *back = actionButton(QStringLiteral("‹ 返回电站列表"), detailPanel_);
    back->setObjectName(QStringLiteral("vehicleStationBackButton"));
    detailLayout->addWidget(back);
    detailTitle_ = new QLabel(detailPanel_);
    detailTitle_->setProperty("role", "sectionTitle");
    detailLayout->addWidget(detailTitle_);
    detailBody_ = new QLabel(detailPanel_);
    detailBody_->setWordWrap(true);
    detailLayout->addWidget(detailBody_);
    auto *route = actionButton(QStringLiteral("驾车路线规划"), detailPanel_);
    route->setObjectName(QStringLiteral("vehicleRouteButton"));
    detailLayout->addWidget(route);
    auto *pileScroll = new QScrollArea(detailPanel_);
    pileScroll->setWidgetResizable(true);
    auto *pileBody = new QWidget(pileScroll);
    pileList_ = new QVBoxLayout(pileBody);
    pileList_->setContentsMargins(0, 0, 0, 0);
    pileList_->addStretch();
    pileScroll->setWidget(pileBody);
    detailLayout->addWidget(pileScroll, 1);
    panelStack_->addWidget(detailPanel_);

    routePanel_ = new QWidget(panelStack_);
    auto *routeLayout = new QVBoxLayout(routePanel_);
    routeLayout->setContentsMargins(0, 0, 0, 0);
    auto *routeBack = actionButton(QStringLiteral("‹ 返回站点详情"), routePanel_);
    routeBack->setObjectName(QStringLiteral("vehicleRouteBackButton"));
    routeLayout->addWidget(routeBack);
    routeDestination_ = new QLabel(routePanel_);
    routeDestination_->setWordWrap(true);
    routeDestination_->setProperty("role", "sectionTitle");
    routeLayout->addWidget(routeDestination_);
    auto *mapMode = new QLabel(hasOnlineMapCanvas_
        ? QStringLiteral("腾讯地图 · 路线由腾讯 WebService 实时规划")
        : QStringLiteral("离线 Mock 路线 · 启动时请显式使用 --map tencent"), routePanel_);
    mapMode->setObjectName(QStringLiteral("vehicleRouteMapMode"));
    mapMode->setWordWrap(true);
    mapMode->setStyleSheet(QStringLiteral("color:#65796c"));
    routeLayout->addWidget(mapMode);
    routeStart_ = new QLineEdit(QStringLiteral("演示位置"), routePanel_);
    routeStart_->setMinimumHeight(tokens::Touch);
    routeLayout->addWidget(routeStart_);
    auto *plan = actionButton(QStringLiteral("开始驾车路线（全屏）"), routePanel_, true);
    plan->setObjectName(QStringLiteral("vehicleRoutePlanButton"));
    routeLayout->addWidget(plan);
    routePanelDetails_ = new QPlainTextEdit(routePanel_);
    routePanelDetails_->setObjectName(QStringLiteral("vehicleRoutePanelDetails"));
    routePanelDetails_->setReadOnly(true);
    routePanelDetails_->setPlainText(
        QStringLiteral("尚未开始驾车路线，左侧地图显示所选充电站。"));
    routePanelDetails_->setMinimumHeight(120);
    routeLayout->addWidget(routePanelDetails_, 1);
    routeMessage_ = new QLabel(routePanel_);
    routeMessage_->setWordWrap(true);
    routeLayout->addWidget(routeMessage_);
    panelStack_->addWidget(routePanel_);

    connect(search_, &QLineEdit::returnPressed, this, &VehicleHomePage::refreshRequested);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.isEmpty()) emit refreshRequested();
    });
    connect(locate, &QPushButton::clicked, this, [this] {
        emit locationRequested(locationInput_->text().trimmed());
    });
    connect(stationMap_, &StationMapView::stationSelected, this, [this](qint64 id) {
        stationMap_->focusStation(id);
        emit stationSelected(id);
    });
    connect(back, &QPushButton::clicked, this, &VehicleHomePage::showDiscovery);
    connect(route, &QPushButton::clicked, this, &VehicleHomePage::showRoutePlanner);
    connect(routeBack, &QPushButton::clicked, this, [this] {
        setRouteFullscreen(false);
        mapStack_->setCurrentWidget(stationMap_);
        panelStack_->setCurrentWidget(detailPanel_);
    });
    connect(exitRouteFullscreen_, &QPushButton::clicked, this,
            [this] { setRouteFullscreen(false); });
    connect(routeDetailsButton_, &QPushButton::toggled, this, [this](bool expanded) {
        routeDetails_->setVisible(expanded);
        routeDetailsButton_->setText(expanded ? QStringLiteral("收起路线")
                                              : QStringLiteral("展开路线"));
        routeDetailsButton_->setAccessibleName(expanded ? QStringLiteral("收起路线详情")
                                                        : QStringLiteral("展开路线详情"));
    });
    connect(plan, &QPushButton::clicked, this, [this] {
        MapLocation start = location_;
        start.address = routeStart_->text().trimmed();
        emit routeRequested(start,
                            {selectedStation_.address, selectedStation_.longitude,
                             selectedStation_.latitude}, RouteMode::Driving);
    });
    connect(routeMap_, &RouteMapView::statusChanged, this,
            [this](const QString &message, bool error) {
        if (error) showRouteMessage(message, true);
    });
}

StationQuery VehicleHomePage::stationQuery() const
{
    StationQuery query;
    query.longitude = location_.longitude;
    query.latitude = location_.latitude;
    query.keyword = search_->text().trimmed();
    return query;
}

void VehicleHomePage::setUser(const protocol::UserDto &user)
{
    greeting_->setText(QStringLiteral("%1，附近充电站").arg(user.nickname));
}

void VehicleHomePage::setLoading(bool loading)
{
    search_->setEnabled(!loading);
    if (loading) showMessage(QStringLiteral("正在刷新电站…"));
}

void VehicleHomePage::showStations(const QList<protocol::StationDto> &stations)
{
    stations_ = stations;
    stationMap_->setStations(stations_);
    stationMap_->fitStations();
    renderDiscovery();
    showMessage(stations_.isEmpty() ? QStringLiteral("没有匹配的电站") : QString());
}

void VehicleHomePage::showVisitHistory(const QList<protocol::OrderDto> &orders)
{
    orders_ = orders;
    renderDiscovery();
}

void VehicleHomePage::clearLayout(QVBoxLayout *layout)
{
    while (layout->count()) {
        QLayoutItem *item = layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void VehicleHomePage::renderDiscovery()
{
    clearLayout(stationList_);
    auto addHeading = [this](const QString &text) {
        auto *label = new QLabel(text, discoveryPanel_);
        label->setProperty("role", "sectionTitle");
        stationList_->addWidget(label);
    };
    auto addStation = [this](const protocol::StationDto &station, const QString &tag) {
        const QString distance = station.distanceKm
            ? QStringLiteral("%1 km").arg(*station.distanceKm, 0, 'f', 1)
            : QStringLiteral("距离待定位");
        auto *button = actionButton(
            QStringLiteral("%1  %2\n%3 · 空闲 %4/%5 · ¥%6/度")
                .arg(tag, station.name, distance)
                .arg(station.availablePileCount).arg(station.totalPileCount)
                .arg(station.priceCentsPerKwh / 100.0, 0, 'f', 2),
            discoveryPanel_);
        button->setProperty("role", "stationCard");
        button->setMinimumHeight(72);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        connect(button, &QPushButton::clicked, this, [this, station] {
            stationMap_->focusStation(station.stationId);
            emit stationSelected(station.stationId);
        });
        stationList_->addWidget(button);
    };

    qint64 recentId = 0;
    for (const auto &order : orders_) {
        if (order.startedAt) { recentId = order.stationId; break; }
    }
    if (recentId) {
        for (const auto &station : stations_) if (station.stationId == recentId) {
            addHeading(QStringLiteral("最近使用"));
            addStation(station, QStringLiteral("最近"));
            break;
        }
    }
    const qint64 recommended = discovery::recommend(stations_);
    if (recommended) {
        addHeading(QStringLiteral("推荐电站"));
        for (const auto &station : stations_) if (station.stationId == recommended) {
            addStation(station, QStringLiteral("推荐"));
            break;
        }
    }
    addHeading(QStringLiteral("附近电站"));
    const auto nearby = discovery::nearby(stations_);
    for (const auto &station : nearby) addStation(station, QStringLiteral("附近"));
    if (nearby.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("30 公里内暂无可用电站"), discoveryPanel_);
        empty->setWordWrap(true);
        stationList_->addWidget(empty);
    }
    stationList_->addStretch();
}

void VehicleHomePage::showStationDetail(const StationDetailPayload &detail)
{
    selectedStation_ = detail.station;
    stationMap_->focusStation(detail.station.stationId);
    detailTitle_->setText(detail.station.name);
    detailBody_->setText(QStringLiteral("%1\n%2 · 空闲 %3/%4 · ¥%5/度")
                             .arg(detail.station.address, detail.station.region)
                             .arg(detail.station.availablePileCount)
                             .arg(detail.station.totalPileCount)
                             .arg(detail.station.priceCentsPerKwh / 100.0, 0, 'f', 2));
    clearLayout(pileList_);
    for (const auto &pile : detail.piles) {
        auto *pileCard = card(detailPanel_);
        auto *layout = new QVBoxLayout(pileCard);
        auto *summary = new QLabel(
            QStringLiteral("%1 · %2 · 额定功率 %3 kW")
                .arg(pile.pileCode,
                     pile.pileType == protocol::PileType::Fast ? QStringLiteral("快充")
                                                                : QStringLiteral("慢充"))
                .arg(pile.ratedPowerKw, 0, 'f', 1), pileCard);
        summary->setWordWrap(true);
        layout->addWidget(summary);
        auto *buttons = new QHBoxLayout;
        auto *reserve = actionButton(QStringLiteral("预约"), pileCard);
        auto *charge = actionButton(QStringLiteral("选择并充电"), pileCard, true);
        reserve->setObjectName(QStringLiteral("vehicleReserve_%1").arg(pile.pileCode));
        charge->setObjectName(QStringLiteral("vehicleCharge_%1").arg(pile.pileCode));
        const bool idle = pile.status == protocol::PileStatus::Idle;
        reserve->setEnabled(idle);
        charge->setEnabled(idle || pile.status == protocol::PileStatus::Reserved);
        connect(reserve, &QPushButton::clicked, this,
                [this, pile] { emit reserveRequested(pile.pileCode); });
        connect(charge, &QPushButton::clicked, this,
                [this, pile] { emit chargeRequested(pile.pileCode, pile.ratedPowerKw,
                                                    selectedStation_); });
        buttons->addWidget(reserve);
        buttons->addWidget(charge);
        layout->addLayout(buttons);
        pileList_->addWidget(pileCard);
    }
    pileList_->addStretch();
    panelStack_->setCurrentWidget(detailPanel_);
}

void VehicleHomePage::showMessage(const QString &message, bool error)
{
    message_->setText(message);
    message_->setStyleSheet(error ? QStringLiteral("color:#a33b32") : QString());
}

void VehicleHomePage::setResolvedLocation(const MapLocation &location)
{
    location_ = location;
    locationInput_->setText(location.address);
    routeStart_->setText(location.address);
    stationMap_->setCurrentLocation(location_);
    stationMap_->setCenter(location_);
}

void VehicleHomePage::showDiscovery()
{
    setRouteFullscreen(false);
    mapStack_->setCurrentWidget(stationMap_);
    panelStack_->setCurrentWidget(discoveryPanel_);
}

void VehicleHomePage::showRoutePlanner()
{
    routeDestination_->setText(QStringLiteral("前往 %1\n%2")
                                   .arg(selectedStation_.name, selectedStation_.address));
    routeMessage_->setText(QStringLiteral(
        "当前提供驾车路线规划和交互查看，不含持续 GPS、语音播报或偏航重算。"));
    routeMessage_->setStyleSheet(QString());
    routePanelDetails_->setPlainText(
        QStringLiteral("尚未开始驾车路线，左侧地图显示所选充电站。"));
    routeDetailsButton_->setChecked(false);
    routeDetailsButton_->setEnabled(false);
    routeDetails_->clear();
    routeMap_->clearRoute();
    stationMap_->focusStation(selectedStation_.stationId);
    mapStack_->setCurrentWidget(stationMap_);
    panelStack_->setCurrentWidget(routePanel_);
}

void VehicleHomePage::showRoute(const RouteResult &route)
{
    if (!route.success) {
        showRouteMessage(route.message, true);
        return;
    }
    if (hasOnlineMapCanvas_) {
        routeMap_->setRoute(route);
        mapStack_->setCurrentWidget(routeMap_);
    }
    const QString instructions = route.instructions.isEmpty()
        ? QStringLiteral("服务未返回分步说明，请参考地图路线。")
        : route.instructions.join(QStringLiteral("\n\n"));
    routeDetails_->setPlainText(instructions);
    routePanelDetails_->setPlainText(
        QStringLiteral("%1\n\n具体路线\n%2").arg(route.summary, instructions));
    routeDetailsButton_->setEnabled(!route.instructions.isEmpty() || !route.paths.isEmpty());
    routeDetailsButton_->setChecked(false);
    setRouteFullscreen(true);
    routeMessage_->clear();
}

void VehicleHomePage::setRouteFullscreen(bool fullscreen)
{
    if (sidePanel_->isHidden() == fullscreen) return;
    sidePanel_->setVisible(!fullscreen);
    auto *overlay = exitRouteFullscreen_->parentWidget();
    overlay->setVisible(fullscreen);
    if (fullscreen) {
        overlay->raise();
    } else {
        routeDetailsButton_->setChecked(false);
    }
    if (auto *layout = qobject_cast<QHBoxLayout *>(this->layout()))
        layout->setContentsMargins(fullscreen ? 0 : tokens::Space,
                                   fullscreen ? 0 : 12,
                                   fullscreen ? 0 : tokens::Space,
                                   fullscreen ? 0 : 12);
    emit routeFullscreenChanged(fullscreen);
}

void VehicleHomePage::showRouteMessage(const QString &message, bool error)
{
    routeMessage_->setText(message);
    routeMessage_->setStyleSheet(error ? QStringLiteral("color:#a33b32") : QString());
}

void VehicleHomePage::reset()
{
    stations_.clear();
    orders_.clear();
    selectedStation_ = {};
    search_->clear();
    showDiscovery();
    stationMap_->setStations({});
    routeMap_->clearRoute();
    routePanelDetails_->clear();
    showMessage({});
}

VehicleChargingPage::VehicleChargingPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("vehicleChargingPage"));
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(28, 20, 28, 20);
    root->setSpacing(20);
    auto *session = card(this);
    session->setObjectName(QStringLiteral("vehicleChargingSession"));
    auto *sessionLayout = new QVBoxLayout(session);
    sessionLayout->setContentsMargins(28, 24, 28, 24);
    sessionLayout->setSpacing(8);
    state_ = new QLabel(QStringLiteral("暂无当前订单"), session);
    state_->setObjectName(QStringLiteral("vehicleChargingState"));
    state_->setAlignment(Qt::AlignCenter);
    state_->setStyleSheet(QStringLiteral(
        "color:#36583c;background:#edf4e5;border-radius:11px;padding:6px 14px;font-weight:700"));
    station_ = new QLabel(session);
    station_->setWordWrap(true);
    station_->setAlignment(Qt::AlignCenter);
    station_->setStyleSheet(QStringLiteral("color:#65796c"));
    reservationHint_ = new QLabel(session);
    reservationHint_->setObjectName(QStringLiteral("vehicleReservationHint"));
    reservationHint_->setAlignment(Qt::AlignCenter);
    reservationHint_->setWordWrap(true);
    progress_ = new ChargingProgressRing(session,
        QStringLiteral("vehicleChargingProgressRing"));
    sessionLayout->addWidget(state_, 0, Qt::AlignHCenter);
    sessionLayout->addWidget(station_);
    sessionLayout->addWidget(reservationHint_);
    sessionLayout->addWidget(progress_, 1);
    message_ = new QLabel(session);
    message_->setWordWrap(true);
    sessionLayout->addWidget(message_);
    root->addWidget(session, 55);

    auto *side = new QWidget(this);
    side->setObjectName(QStringLiteral("vehicleChargingSide"));
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    auto *metrics = new QGridLayout;
    auto metric = [&](const QString &title, QLabel *&value, int row, int col) {
        auto *box = card(side);
        auto *layout = new QVBoxLayout(box);
        auto *label = new QLabel(title, box);
        label->setStyleSheet(QStringLiteral("color:#65796c"));
        value = new QLabel(QStringLiteral("--"), box);
        auto font = value->font(); font.setPointSize(20); font.setBold(true); value->setFont(font);
        layout->addWidget(label); layout->addWidget(value);
        metrics->addWidget(box, row, col);
    };
    metric(QStringLiteral("额定功率"), power_, 0, 0);
    metric(QStringLiteral("已充电量"), energy_, 0, 1);
    metric(QStringLiteral("充电时长"), duration_, 1, 0);
    metric(QStringLiteral("预估/最终金额"), amount_, 1, 1);
    duration_->setObjectName(QStringLiteral("vehicleChargingDuration"));
    sideLayout->addLayout(metrics);
    auto *priceRow = new QHBoxLayout;
    price_ = new QLabel(side);
    price_->setObjectName(QStringLiteral("vehicleChargingPrice"));
    price_->setWordWrap(true);
    pricingInfo_ = new PricingInfoButton(side);
    pricingInfo_->setObjectName(QStringLiteral("vehicleChargingPricingInfo"));
    priceRow->addWidget(price_, 1);
    priceRow->addWidget(pricingInfo_);
    sideLayout->addLayout(priceRow);
    start_ = actionButton(QStringLiteral("确认开始充电"), side, true);
    start_->setObjectName(QStringLiteral("vehicleStartButton"));
    stop_ = actionButton(QStringLiteral("提前停止充电"), side, true);
    stop_->setObjectName(QStringLiteral("vehicleStopButton"));
    cancel_ = actionButton(QStringLiteral("取消预约"), side);
    cancel_->setObjectName(QStringLiteral("vehicleCancelReservationButton"));
    pay_ = actionButton(QStringLiteral("立即结算"), side, true);
    pay_->setObjectName(QStringLiteral("vehiclePayButton"));
    recharge_ = actionButton(QStringLiteral("前往充值"), side);
    recharge_->setObjectName(QStringLiteral("vehicleRechargeEntry"));
    orders_ = actionButton(QStringLiteral("查看我的订单"), side);
    orders_->setObjectName(QStringLiteral("vehicleChargingOrdersEntry"));
    repair_ = actionButton(QStringLiteral("当前电桩报修"), side);
    repair_->setObjectName(QStringLiteral("vehicleChargingRepairEntry"));
    sideLayout->addWidget(start_);
    sideLayout->addWidget(stop_);
    sideLayout->addWidget(cancel_);
    sideLayout->addWidget(pay_);
    auto *secondary = new QHBoxLayout;
    secondary->addWidget(recharge_); secondary->addWidget(orders_); secondary->addWidget(repair_);
    sideLayout->addLayout(secondary);
    sideLayout->addStretch();
    root->addWidget(side, 45);
    connect(start_, &QPushButton::clicked, this, [this] { emit startRequested(pileCode_); });
    connect(stop_, &QPushButton::clicked, this, &VehicleChargingPage::stopRequested);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (order_) emit cancelRequested(order_->orderId);
    });
    connect(pay_, &QPushButton::clicked, this, [this] {
        if (order_) emit payRequested(order_->orderId);
    });
    connect(recharge_, &QPushButton::clicked, this, &VehicleChargingPage::rechargeRequested);
    connect(orders_, &QPushButton::clicked, this, &VehicleChargingPage::ordersRequested);
    connect(repair_, &QPushButton::clicked, this, [this] { emit repairRequested(pileCode_); });
    render();
}

void VehicleChargingPage::prepare(const QString &pileCode, double ratedPowerKw,
                                  const std::optional<protocol::StationDto> &quote)
{
    const QString normalizedPile = pileCode.trimmed();
    const bool samePile = normalizedPile == pileCode_;
    pileCode_ = normalizedPile;
    ratedPowerKw_ = ratedPowerKw;
    if (quote || !samePile) quote_ = quote;
    order_.reset();
    render();
}

void VehicleChargingPage::showOrder(const protocol::OrderDto &order)
{
    order_ = order;
    pileCode_ = order.pileCode;
    render();
}

void VehicleChargingPage::showNoOrder()
{
    if (order_ && (order_->status == protocol::OrderStatus::Completed
                   || order_->status == protocol::OrderStatus::Cancelled)) return;
    order_.reset();
    render();
}

void VehicleChargingPage::setBusy(bool busy) { busy_ = busy; render(); }

void VehicleChargingPage::showMessage(const QString &message, bool error)
{
    message_->setText(message);
    message_->setStyleSheet(error ? QStringLiteral("color:#a33b32") : QString());
}

void VehicleChargingPage::render()
{
    using S = protocol::OrderStatus;
    const bool charging = order_ && order_->status == S::Charging;
    const bool reserved = order_ && order_->status == S::Reserved;
    const bool pending = order_ && order_->status == S::PendingPayment;
    state_->setText(order_ ? statusText(order_->status)
                           : pileCode_.isEmpty() ? QStringLiteral("暂无当前订单")
                                                 : QStringLiteral("等待启动确认"));
    station_->setText(order_ ? QStringLiteral("%1 · %2").arg(order_->stationName, order_->pileCode)
                             : pileCode_.isEmpty() ? QStringLiteral("请从首页选择电桩")
                                                   : QStringLiteral("已选择电桩 %1").arg(pileCode_));
    const qint64 seconds = order_ ? order_->durationSeconds : 0;
    QString progressCaption = charging
        ? QStringLiteral("预计剩余 %1 秒").arg(qMax<qint64>(
              0, protocol::DemoChargingDurationSeconds - seconds))
        : order_ && (order_->status == S::Completed || pending)
            ? QStringLiteral("本次充电结束")
            : reserved ? QStringLiteral("预约已确认，等待开始")
                       : QStringLiteral("连接充电枪后开始");
    progress_->setProgress(session::demoProgressPercent(seconds), progressCaption);
    reservationHint_->setText(order_ ? charging::client::reservationHint(*order_) : QString());
    reservationHint_->setVisible(reserved);
    power_->setText(ratedPowerKw_ > 0 ? QStringLiteral("%1 kW").arg(ratedPowerKw_, 0, 'f', 1)
                                     : QStringLiteral("待读取"));
    energy_->setText(order_ ? QStringLiteral("%1 kWh").arg(order_->energyWh / 1000.0, 0, 'f', 3)
                            : QStringLiteral("--"));
    duration_->setText(order_ ? durationText(order_->durationSeconds) : QStringLiteral("--"));
    amount_->setText(order_ ? QStringLiteral("¥%1").arg(order_->amountCents / 100.0, 0, 'f', 2)
                            : QStringLiteral("--"));
    const bool locked = order_ && order_->unitPriceCentsPerKwh.has_value();
    price_->setVisible(locked || quote_.has_value());
    if (locked) {
        price_->setText(QStringLiteral("本单锁定单价：%1")
                            .arg(chargingPriceText(*order_->unitPriceCentsPerKwh)));
        pricingInfo_->setRules(QStringLiteral(
            "本单按开始充电时的单价结算。\n跨时段结束或稍后补付款均不变价。"));
    } else if (quote_) {
        price_->setText(QStringLiteral("当前参考单价：%1")
                            .arg(chargingPriceText(quote_->priceCentsPerKwh)));
        pricingInfo_->setRules(pricingHint(*quote_));
    } else {
        price_->clear();
        pricingInfo_->setRules({});
    }
    start_->setVisible(!pileCode_.isEmpty() && (!order_ || reserved));
    start_->setEnabled(!busy_);
    stop_->setVisible(charging);
    stop_->setEnabled(!busy_);
    cancel_->setVisible(reserved);
    cancel_->setEnabled(!busy_);
    pay_->setVisible(pending);
    pay_->setEnabled(!busy_);
    recharge_->setVisible(pending);
    orders_->setVisible(true);
    repair_->setVisible(!pileCode_.isEmpty());
}

void VehicleChargingPage::reset()
{
    pileCode_.clear(); ratedPowerKw_ = 0; quote_.reset(); order_.reset(); busy_ = false;
    showMessage({}); render();
}

VehicleProfilePage::VehicleProfilePage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("vehicleProfilePage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 10, 28, 10);
    sections_ = new QStackedWidget(this);
    root->addWidget(sections_);
    overview_ = new QWidget(sections_);
    auto *overviewLayout = new QHBoxLayout(overview_);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    overviewLayout->setSpacing(20);

    auto *profileCard = card(overview_);
    profileCard->setObjectName(QStringLiteral("vehicleProfileDetailsCard"));
    auto *profileLayout = new QVBoxLayout(profileCard);
    profileLayout->setContentsMargins(20, 4, 20, 4);
    profileLayout->setSpacing(12);
    profileLayout->setAlignment(Qt::AlignVCenter);
    avatar_ = new QLabel(QStringLiteral("本机头像\n默认"), profileCard);
    avatar_->setObjectName(QStringLiteral("vehicleLocalAvatar"));
    avatar_->setAlignment(Qt::AlignCenter);
    avatar_->setFixedSize(168, 168);
    avatar_->setStyleSheet(QStringLiteral("background:#e8eee4;border-radius:18px;color:#65796c"));
    profileLayout->addWidget(avatar_, 0, Qt::AlignHCenter);
    nickname_ = new QLabel(QStringLiteral("未登录"), profileCard);
    nickname_->setObjectName(QStringLiteral("vehicleProfileNickname"));
    nickname_->setAlignment(Qt::AlignCenter);
    auto identityFont = nickname_->font();
    identityFont.setPointSize(20);
    identityFont.setBold(true);
    nickname_->setFont(identityFont);
    nickname_->setStyleSheet(QStringLiteral("color:#36583c"));
    phone_ = new QLabel(profileCard);
    phone_->setObjectName(QStringLiteral("vehicleProfilePhone"));
    phone_->setAlignment(Qt::AlignCenter);
    phone_->setFont(identityFont);
    phone_->setStyleSheet(QStringLiteral("color:#36583c"));
    balance_ = new QLabel(QStringLiteral("余额 --"), profileCard);
    balance_->setObjectName(QStringLiteral("vehicleProfileBalance"));
    balance_->setAlignment(Qt::AlignCenter);
    balance_->setFont(identityFont);
    balance_->setStyleSheet(QStringLiteral("color:#36583c"));
    auto *identityBlock = new QWidget(profileCard);
    identityBlock->setObjectName(QStringLiteral("vehicleProfileIdentityBlock"));
    identityBlock->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    identityBlock->setStyleSheet(QStringLiteral(
        "QWidget#vehicleProfileIdentityBlock { background:transparent; }"));
    auto *identityLayout = new QVBoxLayout(identityBlock);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(1);
    identityLayout->addWidget(nickname_);
    identityLayout->addWidget(phone_);
    identityLayout->addWidget(balance_);
    profileLayout->addWidget(identityBlock);
    auto *profileControls = new QWidget(profileCard);
    profileControls->setObjectName(QStringLiteral("vehicleProfileControls"));
    profileControls->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    profileControls->setStyleSheet(QStringLiteral(
        "QWidget#vehicleProfileControls { background:transparent; }"));
    auto *controlsLayout = new QVBoxLayout(profileControls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(5);
    editProfile_ = actionButton(QStringLiteral("修改个人信息"), profileCard, true);
    editProfile_->setObjectName(QStringLiteral("vehicleEditProfileButton"));
    editProfile_->setStyleSheet(QStringLiteral("min-height:58px;max-height:58px"));
    controlsLayout->addWidget(editProfile_);
    message_ = new QLabel(profileCard);
    message_->setObjectName(QStringLiteral("vehicleProfileMessage"));
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->hide();
    controlsLayout->addWidget(message_);
    logout_ = actionButton(QStringLiteral("退出登录"), profileCard);
    logout_->setObjectName(QStringLiteral("vehicleLogoutButton"));
    logout_->setIcon(clientNavigationIcon(NavigationIcon::Profile));
    logout_->setIconSize(QSize(22, 22));
    logout_->setStyleSheet(QStringLiteral(
        "QPushButton { background:#fff0ec;color:#9f3f32;border:1px solid #efc9c1;"
        "border-radius:12px;font-weight:700;min-height:58px;max-height:58px; }"
        "QPushButton:hover,QPushButton:focus { background:#ffe5df;border-color:#c86b5d; }"
        "QPushButton:pressed { background:#f8d7cf; }"));
    controlsLayout->addWidget(logout_);
    profileLayout->addWidget(profileControls);
    overviewLayout->addWidget(profileCard, 46);

    auto *rightColumn = new QWidget(overview_);
    rightColumn->setObjectName(QStringLiteral("vehicleProfileActionsColumn"));
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(14);
    auto *wallet = card(rightColumn);
    wallet->setStyleSheet(profileWalletStyleSheet());
    auto *walletLayout = new QVBoxLayout(wallet);
    walletLayout->setContentsMargins(20, 16, 20, 16);
    walletLayout->setSpacing(10);
    auto *walletTitle = new QLabel(QStringLiteral("余额充值"), wallet);
    walletTitle->setProperty("role", "sectionTitle");
    walletLayout->addWidget(walletTitle);
    auto *quickAmounts = new QHBoxLayout;
    quickAmounts->setSpacing(8);
    auto *amountGroup = new QButtonGroup(wallet);
    for (const QString &amount : {QStringLiteral("10"), QStringLiteral("20"),
                                  QStringLiteral("50"), QStringLiteral("100")}) {
        auto *button = actionButton(QStringLiteral("%1元").arg(amount), wallet);
        button->setProperty("rechargeAmount", amount);
        button->setCheckable(true);
        amountGroup->addButton(button);
        quickAmounts->addWidget(button, 1);
        connect(button, &QPushButton::clicked, this,
                [this, amount] { rechargeInput_->setText(amount); });
    }
    walletLayout->addLayout(quickAmounts);
    auto *rechargeRow = new QHBoxLayout;
    rechargeInput_ = new QLineEdit(wallet);
    rechargeInput_->setObjectName(QStringLiteral("vehicleRechargeInput"));
    rechargeInput_->setPlaceholderText(QStringLiteral("输入充值金额（元）"));
    rechargeInput_->setMinimumHeight(tokens::Touch);
    auto *validator = new QDoubleValidator(0.01, 10000.0, 2, rechargeInput_);
    validator->setNotation(QDoubleValidator::StandardNotation);
    rechargeInput_->setValidator(validator);
    connect(rechargeInput_, &QLineEdit::textChanged, this,
            [amountGroup](const QString &text) {
        amountGroup->setExclusive(false);
        for (auto *button : amountGroup->buttons())
            button->setChecked(text.toDouble()
                               == button->property("rechargeAmount").toDouble());
        amountGroup->setExclusive(true);
    });
    rechargeButton_ = actionButton(QStringLiteral("充值"), wallet, true);
    rechargeButton_->setObjectName(QStringLiteral("vehicleRechargeButton"));
    rechargeRow->addWidget(rechargeInput_, 1);
    rechargeRow->addWidget(rechargeButton_);
    walletLayout->addLayout(rechargeRow);
    rightLayout->addWidget(wallet);

    auto *actions = card(rightColumn);
    actions->setObjectName(QStringLiteral("vehicleProfileServiceCard"));
    auto *actionsLayout = new QGridLayout(actions);
    actionsLayout->setContentsMargins(14, 14, 14, 14);
    actionsLayout->setSpacing(10);
    const QList<QPair<QString, const char *>> entries = {
        {QStringLiteral("我的订单"), "vehicleOrdersButton"},
        {QStringLiteral("客服助理"), "vehicleSupportButton"},
        {QStringLiteral("故障报修"), "vehicleRepairButton"},
        {QStringLiteral("我的工单"), "vehicleTicketsButton"}};
    for (int i = 0; i < entries.size(); ++i) {
        auto *button = actionButton(entries[i].first, actions);
        button->setObjectName(QString::fromLatin1(entries[i].second));
        button->setProperty("role", "vehicleProfileService");
        const QList<NavigationIcon> icons = {NavigationIcon::Orders, NavigationIcon::Support,
            NavigationIcon::Repair, NavigationIcon::Tickets, NavigationIcon::Profile};
        button->setIcon(clientNavigationIcon(icons[i]));
        button->setIconSize(QSize(40, 40));
        auto serviceFont = button->font();
        serviceFont.setPointSize(19);
        serviceFont.setWeight(QFont::ExtraBold);
        button->setFont(serviceFont);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { padding:0 20px;border-radius:14px;font-weight:800; }"));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        actionsLayout->addWidget(button, i / 2, i % 2);
        actions_.append(button);
    }
    actionsLayout->setRowStretch(0, 1);
    actionsLayout->setRowStretch(1, 1);
    actionsLayout->setColumnStretch(0, 1);
    actionsLayout->setColumnStretch(1, 1);
    rightLayout->addWidget(actions, 1);
    overviewLayout->addWidget(rightColumn, 54);
    sections_->addWidget(overview_);

    editPage_ = new QWidget(sections_);
    editPage_->setObjectName(QStringLiteral("vehicleProfileEditPage"));
    auto *editPageLayout = new QVBoxLayout(editPage_);
    editPageLayout->setContentsMargins(0, 0, 0, 0);
    editPageLayout->setSpacing(14);
    auto *editHeader = new QHBoxLayout;
    auto *editBack = actionButton(QStringLiteral("‹ 返回我的"), editPage_);
    editBack->setObjectName(QStringLiteral("vehicleProfileEditBackButton"));
    auto *editHeading = new QLabel(QStringLiteral("修改个人信息"), editPage_);
    editHeading->setProperty("role", "sectionTitle");
    editHeader->addWidget(editBack);
    editHeader->addWidget(editHeading);
    editHeader->addStretch();
    editPageLayout->addLayout(editHeader);

    auto *editCard = card(editPage_);
    editCard->setObjectName(QStringLiteral("vehicleProfileEditCard"));
    auto *editCardLayout = new QHBoxLayout(editCard);
    editCardLayout->setContentsMargins(32, 28, 32, 28);
    editCardLayout->setSpacing(36);
    auto *avatarColumn = new QVBoxLayout;
    auto *avatarTitle = new QLabel(QStringLiteral("本机头像"), editCard);
    avatarTitle->setProperty("role", "sectionTitle");
    avatarTitle->setAlignment(Qt::AlignCenter);
    editAvatar_ = new QLabel(QStringLiteral("本机头像\n默认"), editCard);
    editAvatar_->setObjectName(QStringLiteral("vehicleEditAvatar"));
    editAvatar_->setAlignment(Qt::AlignCenter);
    editAvatar_->setFixedSize(152, 152);
    editAvatar_->setStyleSheet(QStringLiteral(
        "background:#e8eee4;border-radius:22px;color:#65796c"));
    avatarChange_ = actionButton(QStringLiteral("更换本机头像"), editCard);
    avatarChange_->setObjectName(QStringLiteral("vehicleAvatarChangeButton"));
    avatarChange_->setIcon(clientNavigationIcon(NavigationIcon::Profile));
    avatarChange_->setToolTip(QStringLiteral("头像只保存在当前车载端，不与手机端同步"));
    avatarColumn->addWidget(avatarTitle);
    avatarColumn->addWidget(editAvatar_, 0, Qt::AlignHCenter);
    avatarColumn->addWidget(avatarChange_);
    avatarColumn->addStretch();
    editCardLayout->addLayout(avatarColumn, 42);

    auto *nicknameColumn = new QVBoxLayout;
    auto *nicknameTitle = new QLabel(QStringLiteral("昵称"), editCard);
    nicknameTitle->setProperty("role", "sectionTitle");
    nicknameInput_ = new QLineEdit(editCard);
    nicknameInput_->setObjectName(QStringLiteral("vehicleNicknameInput"));
    nicknameInput_->setPlaceholderText(QStringLiteral("输入新昵称"));
    nicknameInput_->setMaxLength(32);
    nicknameInput_->setMinimumHeight(tokens::Touch);
    saveNickname_ = actionButton(QStringLiteral("保存昵称"), editCard, true);
    saveNickname_->setObjectName(QStringLiteral("vehicleSaveNicknameButton"));
    auto *localHint = new QLabel(
        QStringLiteral("昵称保存后会通过服务端同步到其他客户端。头像仍只保存在本车载端。"),
        editCard);
    localHint->setWordWrap(true);
    localHint->setStyleSheet(QStringLiteral("color:#65796c"));
    nicknameColumn->addWidget(nicknameTitle);
    nicknameColumn->addWidget(nicknameInput_);
    nicknameColumn->addWidget(saveNickname_);
    nicknameColumn->addWidget(localHint);
    nicknameColumn->addStretch();
    editCardLayout->addLayout(nicknameColumn, 58);
    editPageLayout->addWidget(editCard, 1);
    sections_->addWidget(editPage_);

    ordersPage_ = new QWidget(sections_);
    auto *ordersLayout = new QVBoxLayout(ordersPage_);
    ordersLayout->setContentsMargins(0, 0, 0, 0);
    ordersLayout->setSpacing(14);
    auto *ordersHeader = new QHBoxLayout;
    auto *back = actionButton(QStringLiteral("‹ 返回我的"), ordersPage_);
    back->setObjectName(QStringLiteral("vehicleOrdersBackButton"));
    auto *ordersHeading = new QLabel(QStringLiteral("我的订单"), ordersPage_);
    ordersHeading->setProperty("role", "sectionTitle");
    auto *ordersDescription = new QLabel(
        QStringLiteral("记录每一次补能，也照顾正在进行的一程。"), ordersPage_);
    ordersDescription->setStyleSheet(QStringLiteral("color:#697969"));
    ordersHeader->addWidget(back);
    ordersHeader->addWidget(ordersHeading);
    ordersHeader->addWidget(ordersDescription, 1);
    ordersLayout->addLayout(ordersHeader);

    auto *ordersContent = new QHBoxLayout;
    ordersContent->setSpacing(16);
    ordersList_ = new QListWidget(ordersPage_);
    ordersList_->setObjectName(QStringLiteral("vehicleOrderList"));
    ordersList_->setSpacing(9);
    ordersList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ordersList_->setStyleSheet(QStringLiteral(
        "QListWidget { background:transparent;border:none;outline:none; }"
        "QListWidget::item { background:transparent;border:none; }"));
    ordersContent->addWidget(ordersList_, 44);

    auto *detailScroll = new QScrollArea(ordersPage_);
    detailScroll->setObjectName(QStringLiteral("vehicleOrderDetailScroll"));
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    auto *detailViewport = new QWidget(detailScroll);
    auto *detailViewportLayout = new QVBoxLayout(detailViewport);
    detailViewportLayout->setContentsMargins(0, 0, 0, 0);
    auto *detailCard = card(detailViewport);
    detailCard->setObjectName(QStringLiteral("vehicleOrderDetailCard"));
    auto *detailLayout = new QVBoxLayout(detailCard);
    detailLayout->setContentsMargins(24, 22, 24, 22);
    detailLayout->setSpacing(16);
    auto *detailHeader = new QHBoxLayout;
    orderDetailNumber_ = new QLabel(QStringLiteral("选择左侧订单查看详情"), detailCard);
    orderDetailNumber_->setObjectName(QStringLiteral("vehicleOrderDetailNumber"));
    orderDetailNumber_->setProperty("role", "sectionTitle");
    orderDetailNumber_->setWordWrap(true);
    orderDetailStatus_ = new QLabel(detailCard);
    orderDetailStatus_->setObjectName(QStringLiteral("vehicleOrderDetailStatus"));
    orderDetailStatus_->setAlignment(Qt::AlignCenter);
    orderDetailStatus_->hide();
    detailHeader->addWidget(orderDetailNumber_, 1);
    detailHeader->addWidget(orderDetailStatus_, 0, Qt::AlignTop);
    detailLayout->addLayout(detailHeader);
    auto *separator = new QFrame(detailCard);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color:#e1e7dc"));
    detailLayout->addWidget(separator);
    orderDetailBody_ = new QLabel(
        QStringLiteral("订单数据刷新后，可在这里快速查看充电站、时间、充电量和金额。"),
        detailCard);
    orderDetailBody_->setObjectName(QStringLiteral("vehicleOrderDetailBody"));
    orderDetailBody_->setWordWrap(true);
    orderDetailBody_->setTextFormat(Qt::RichText);
    orderDetailBody_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(orderDetailBody_);
    detailLayout->addStretch();
    detailViewportLayout->addWidget(detailCard);
    detailViewportLayout->addStretch();
    detailScroll->setWidget(detailViewport);
    ordersContent->addWidget(detailScroll, 56);
    ordersLayout->addLayout(ordersContent, 1);
    sections_->addWidget(ordersPage_);
    connect(editProfile_, &QPushButton::clicked, this,
            [this] { sections_->setCurrentWidget(editPage_); });
    connect(editBack, &QPushButton::clicked, this, &VehicleProfilePage::showOverview);
    connect(back, &QPushButton::clicked, this, &VehicleProfilePage::showOverview);
    connect(avatarChange_, &QPushButton::clicked, this,
            &VehicleProfilePage::avatarChangeRequested);
    connect(saveNickname_, &QPushButton::clicked, this, [this] { emit nicknameRequested(nicknameInput_->text().trimmed()); });
    connect(rechargeButton_, &QPushButton::clicked, this, [this] { emit rechargeRequested(rechargeInput_->text().trimmed()); });
    connect(actions_[0], &QPushButton::clicked, this, &VehicleProfilePage::ordersRequested);
    connect(actions_[1], &QPushButton::clicked, this, &VehicleProfilePage::supportRequested);
    connect(actions_[2], &QPushButton::clicked, this, &VehicleProfilePage::repairRequested);
    connect(actions_[3], &QPushButton::clicked, this, &VehicleProfilePage::ticketsRequested);
    connect(logout_, &QPushButton::clicked, this, &VehicleProfilePage::logoutRequested);
    connect(ordersList_, &QListWidget::currentRowChanged,
            this, &VehicleProfilePage::showOrderDetail);
}

void VehicleProfilePage::setUser(const protocol::UserDto &user, const QString &avatarPath)
{
    nickname_->setText(user.nickname);
    phone_->setText(QStringLiteral("手机号 %1").arg(user.phone));
    balance_->setText(QStringLiteral("余额 ¥%1").arg(user.balanceCents / 100.0, 0, 'f', 2));
    nicknameInput_->setText(user.nickname);
    QImage image = avatarPath.isEmpty() ? defaultAvatar() : QImage(avatarPath);
    if (image.isNull()) image = defaultAvatar();
    avatar_->setText({});
    avatar_->setPixmap(circularAvatar(image, 152, devicePixelRatioF()));
    avatar_->setToolTip(QStringLiteral("头像仅保存在本车载客户端，不与手机端同步"));
    editAvatar_->setText({});
    editAvatar_->setPixmap(circularAvatar(image, 136, devicePixelRatioF()));
    editAvatar_->setToolTip(avatar_->toolTip());
}

void VehicleProfilePage::setBusy(bool busy)
{
    nicknameInput_->setEnabled(!busy); rechargeInput_->setEnabled(!busy);
    saveNickname_->setEnabled(!busy); rechargeButton_->setEnabled(!busy);
    avatarChange_->setEnabled(!busy); editProfile_->setEnabled(!busy); logout_->setEnabled(!busy);
    for (auto *button : actions_) button->setEnabled(!busy);
}

void VehicleProfilePage::showOrders(const QList<protocol::OrderDto> &orders)
{
    ordersList_->clear();
    displayedOrders_ = orders;
    for (const auto &order : orders) {
        auto *item = new QListWidgetItem(ordersList_);
        item->setSizeHint(QSize(320, 132));
        auto *orderCard = new QPushButton(ordersList_);
        orderCard->setObjectName(QStringLiteral("vehicleOrderCard_%1").arg(order.orderId));
        orderCard->setAccessibleName(QStringLiteral("查看订单%1详情").arg(order.orderNo));
        orderCard->setCursor(Qt::PointingHandCursor);
        orderCard->setStyleSheet(QStringLiteral(
            "QPushButton { background:white;border:1px solid #e1e7dc;border-radius:13px;"
            "padding:0;text-align:left; }"
            "QPushButton:hover,QPushButton:focus { background:#f5f8f1;border:2px solid #7d9b80; }"
            "QPushButton:pressed { background:#edf4e5; }"));
        auto *layout = new QVBoxLayout(orderCard);
        layout->setContentsMargins(16, 13, 16, 13);
        layout->setSpacing(7);
        auto *titleRow = new QHBoxLayout;
        auto *station = new QLabel(order.stationName, orderCard);
        auto stationFont = station->font();
        stationFont.setBold(true);
        station->setFont(stationFont);
        station->setWordWrap(true);
        auto *status = new QLabel(statusText(order.status), orderCard);
        status->setStyleSheet(QStringLiteral(
            "color:%1;background:#f0f4e9;border-radius:8px;padding:4px 8px;font-weight:700")
                                  .arg(orderStatusColor(order.status)));
        titleRow->addWidget(station, 1);
        titleRow->addWidget(status);
        auto *summary = new QLabel(
            QStringLiteral("%1 · %2\n%3")
                .arg(order.pileCode, order.orderNo, dateTimeText(order.createdAt)), orderCard);
        summary->setStyleSheet(QStringLiteral("color:#697969"));
        auto *bottomRow = new QHBoxLayout;
        auto *amount = new QLabel(order.amountCents > 0 ? moneyText(order.amountCents)
                                                        : QStringLiteral("金额待产生"), orderCard);
        amount->setStyleSheet(QStringLiteral("color:#245c45;font-size:17px;font-weight:700"));
        auto *hint = new QLabel(QStringLiteral("查看详情  ›"), orderCard);
        hint->setStyleSheet(QStringLiteral("color:#65796c"));
        hint->setAlignment(Qt::AlignRight);
        bottomRow->addWidget(amount, 1);
        bottomRow->addWidget(hint);
        for (auto *label : {station, status, summary, amount, hint})
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addLayout(titleRow);
        layout->addWidget(summary);
        layout->addLayout(bottomRow);
        ordersList_->setItemWidget(item, orderCard);
        connect(orderCard, &QPushButton::clicked, this, [this, item] {
            ordersList_->setCurrentItem(item);
            showOrderDetail(ordersList_->row(item));
        });
    }
    if (orders.isEmpty()) {
        auto *empty = new QListWidgetItem(QStringLiteral("暂无订单"), ordersList_);
        empty->setTextAlignment(Qt::AlignCenter);
        empty->setFlags(Qt::NoItemFlags);
        orderDetailNumber_->setText(QStringLiteral("暂无订单"));
        orderDetailStatus_->hide();
        orderDetailBody_->setText(QStringLiteral("完成充电或预约后，订单会显示在这里。"));
    } else {
        ordersList_->setCurrentRow(0);
        showOrderDetail(0);
    }
    showOrderList();
}

void VehicleProfilePage::showOrderDetail(int row)
{
    if (row < 0 || row >= displayedOrders_.size()) return;
    const auto &order = displayedOrders_.at(row);
    orderDetailNumber_->setText(QStringLiteral("订单 %1").arg(order.orderNo));
    orderDetailStatus_->setText(statusText(order.status));
    orderDetailStatus_->setStyleSheet(QStringLiteral(
        "color:%1;background:#edf4e5;border-radius:10px;padding:6px 12px;font-weight:700")
                                          .arg(orderStatusColor(order.status)));
    orderDetailStatus_->show();

    QString table = QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    const auto append = [&table](const QString &label, const QString &value) {
        table += QStringLiteral(
            "<tr><td width=\"110\" valign=\"top\" style=\"padding:0 18px 12px 0;"
            "color:#697969;white-space:nowrap\">%1：</td>"
            "<td valign=\"top\" style=\"padding:0 0 12px 0;color:#203d33\">%2</td></tr>")
                     .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    };
    append(QStringLiteral("充电站"), order.stationName);
    append(QStringLiteral("充电桩"), order.pileCode);
    append(QStringLiteral("充电方式"), orderModeText(order.mode));
    append(QStringLiteral("创建时间"), dateTimeText(order.createdAt));
    if (order.reservedAt) append(QStringLiteral("预约时间"), dateTimeText(*order.reservedAt));
    if (order.startedAt) append(QStringLiteral("开始时间"), dateTimeText(*order.startedAt));
    if (order.endedAt) append(QStringLiteral("结束时间"), dateTimeText(*order.endedAt));
    if (order.paidAt) append(QStringLiteral("支付时间"), dateTimeText(*order.paidAt));
    if (order.startedAt || order.endedAt) {
        append(QStringLiteral("充电时长"), orderDurationText(order.durationSeconds));
        append(QStringLiteral("充电量"),
               QStringLiteral("%1 度").arg(order.energyWh / 1000.0, 0, 'f', 3));
    }
    if (order.unitPriceCentsPerKwh)
        append(QStringLiteral("锁定单价"),
               QStringLiteral("%1/度").arg(moneyText(*order.unitPriceCentsPerKwh)));
    append(QStringLiteral("订单金额"), order.amountCents > 0
                                               ? moneyText(order.amountCents)
                                               : QStringLiteral("待产生"));
    table += QStringLiteral("</table>");
    orderDetailBody_->setText(table);
}

void VehicleProfilePage::showMessage(const QString &message, bool error)
{
    message_->setText(message);
    message_->setStyleSheet(error ? QStringLiteral("color:#a33b32") : QString());
    message_->setVisible(!message.isEmpty());
    if (!message.isEmpty()) showOverview();
}
void VehicleProfilePage::showOverview() { sections_->setCurrentWidget(overview_); }
void VehicleProfilePage::showOrderList() { sections_->setCurrentWidget(ordersPage_); }
void VehicleProfilePage::reset()
{
    nickname_->setText(QStringLiteral("未登录")); phone_->clear(); balance_->setText(QStringLiteral("余额 --"));
    avatar_->clear(); avatar_->setText(QStringLiteral("本机头像\n默认"));
    editAvatar_->clear(); editAvatar_->setText(QStringLiteral("本机头像\n默认"));
    nicknameInput_->clear(); rechargeInput_->clear(); ordersList_->clear(); displayedOrders_.clear();
    orderDetailNumber_->setText(QStringLiteral("选择左侧订单查看详情"));
    orderDetailStatus_->hide();
    orderDetailBody_->setText(QStringLiteral("订单数据刷新后，可在这里快速查看充电站、时间、充电量和金额。"));
    showMessage({}); showOverview();
}

}  // namespace charging::client
