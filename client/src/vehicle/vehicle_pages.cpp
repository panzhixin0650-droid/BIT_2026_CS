#include "vehicle/vehicle_pages.h"

#include "common/client_tokens.h"
#include "common/charging_session_state.h"
#include "ui/route_map_view.h"
#include "ui/avatar_art.h"
#include "ui/station_discovery_policy.h"
#include "ui/station_map_view.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
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
    root->addWidget(mapStack_, 65);

    auto *panel = card(this);
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
    auto *searchRow = new QHBoxLayout;
    search_ = new QLineEdit(discoveryPanel_);
    search_->setObjectName(QStringLiteral("vehicleStationSearch"));
    search_->setPlaceholderText(QStringLiteral("搜索站名或地址"));
    search_->setMinimumHeight(tokens::Touch);
    searchRow->addWidget(search_, 1);
    refresh_ = actionButton(QStringLiteral("搜索"), discoveryPanel_);
    refresh_->setObjectName(QStringLiteral("vehicleSearchButton"));
    searchRow->addWidget(refresh_);
    discoveryLayout->addLayout(searchRow);
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
    routeStart_ = new QLineEdit(QStringLiteral("演示位置"), routePanel_);
    routeStart_->setMinimumHeight(tokens::Touch);
    routeLayout->addWidget(routeStart_);
    routeMode_ = new QComboBox(routePanel_);
    routeMode_->setObjectName(QStringLiteral("vehicleRouteMode"));
    routeMode_->addItems({QStringLiteral("驾车"), QStringLiteral("步行"),
                          QStringLiteral("公共交通"), QStringLiteral("骑行")});
    routeMode_->setMinimumHeight(tokens::Touch);
    routeLayout->addWidget(routeMode_);
    auto *plan = actionButton(QStringLiteral("开始路线规划"), routePanel_, true);
    plan->setObjectName(QStringLiteral("vehicleRoutePlanButton"));
    routeLayout->addWidget(plan);
    routeMessage_ = new QLabel(routePanel_);
    routeMessage_->setWordWrap(true);
    routeLayout->addWidget(routeMessage_);
    routeLayout->addStretch();
    panelStack_->addWidget(routePanel_);

    connect(refresh_, &QPushButton::clicked, this, &VehicleHomePage::refreshRequested);
    connect(search_, &QLineEdit::returnPressed, this, &VehicleHomePage::refreshRequested);
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
        mapStack_->setCurrentWidget(stationMap_);
        panelStack_->setCurrentWidget(detailPanel_);
    });
    connect(plan, &QPushButton::clicked, this, [this] {
        MapLocation start = location_;
        start.address = routeStart_->text().trimmed();
        const auto mode = static_cast<RouteMode>(routeMode_->currentIndex());
        emit routeRequested(start,
                            {selectedStation_.address, selectedStation_.longitude,
                             selectedStation_.latitude}, mode);
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
    refresh_->setEnabled(!loading);
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
                [this, pile] { emit chargeRequested(pile.pileCode, pile.ratedPowerKw); });
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
    mapStack_->setCurrentWidget(stationMap_);
    panelStack_->setCurrentWidget(discoveryPanel_);
}

void VehicleHomePage::showRoutePlanner()
{
    routeDestination_->setText(QStringLiteral("前往 %1\n%2")
                                   .arg(selectedStation_.name, selectedStation_.address));
    routeMessage_->setText(QStringLiteral("当前为路线规划和交互查看，不是持续 GPS 导航。"));
    mapStack_->setCurrentWidget(hasOnlineMapCanvas_ ? static_cast<QWidget *>(routeMap_)
                                                    : static_cast<QWidget *>(stationMap_));
    panelStack_->setCurrentWidget(routePanel_);
}

void VehicleHomePage::showRoute(const RouteResult &route)
{
    if (!route.success) {
        showRouteMessage(route.message, true);
        return;
    }
    if (hasOnlineMapCanvas_) routeMap_->setRoute(route);
    routeMessage_->setText(route.summary + QStringLiteral("\n") + route.instructions.join(QStringLiteral("\n")));
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
    showMessage({});
}

VehicleChargingPage::VehicleChargingPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("vehicleChargingPage"));
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(28, 20, 28, 20);
    root->setSpacing(20);
    auto *session = card(this);
    auto *sessionLayout = new QVBoxLayout(session);
    sessionLayout->setContentsMargins(28, 24, 28, 24);
    state_ = new QLabel(QStringLiteral("暂无当前订单"), session);
    state_->setObjectName(QStringLiteral("vehicleChargingState"));
    state_->setProperty("role", "sectionTitle");
    station_ = new QLabel(session);
    station_->setWordWrap(true);
    progress_ = new QProgressBar(session);
    progress_->setObjectName(QStringLiteral("vehicleChargingProgress"));
    progress_->setRange(0, 100);
    progress_->setFormat(QStringLiteral("Demo 会话进度 %p%"));
    progress_->setMinimumHeight(56);
    sessionLayout->addWidget(state_);
    sessionLayout->addWidget(station_);
    sessionLayout->addStretch();
    sessionLayout->addWidget(progress_);
    sessionLayout->addStretch();
    message_ = new QLabel(session);
    message_->setWordWrap(true);
    sessionLayout->addWidget(message_);
    root->addWidget(session, 55);

    auto *side = new QWidget(this);
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
    sideLayout->addLayout(metrics);
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

void VehicleChargingPage::prepare(const QString &pileCode, double ratedPowerKw)
{
    pileCode_ = pileCode.trimmed();
    ratedPowerKw_ = ratedPowerKw;
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
    progress_->setValue(order_ ? session::demoProgressPercent(order_->durationSeconds) : 0);
    power_->setText(ratedPowerKw_ > 0 ? QStringLiteral("%1 kW").arg(ratedPowerKw_, 0, 'f', 1)
                                     : QStringLiteral("待读取"));
    energy_->setText(order_ ? QStringLiteral("%1 kWh").arg(order_->energyWh / 1000.0, 0, 'f', 3)
                            : QStringLiteral("--"));
    duration_->setText(order_ ? durationText(order_->durationSeconds) : QStringLiteral("--"));
    amount_->setText(order_ ? QStringLiteral("¥%1").arg(order_->amountCents / 100.0, 0, 'f', 2)
                            : QStringLiteral("--"));
    start_->setVisible(!pileCode_.isEmpty() && (!order_ || reserved));
    start_->setEnabled(!busy_);
    stop_->setVisible(charging);
    stop_->setEnabled(!busy_);
    cancel_->setVisible(reserved);
    cancel_->setEnabled(!busy_);
    pay_->setVisible(pending);
    pay_->setEnabled(!busy_);
    recharge_->setVisible(pending);
    orders_->setVisible(order_.has_value());
    repair_->setVisible(!pileCode_.isEmpty());
}

void VehicleChargingPage::reset()
{
    pileCode_.clear(); ratedPowerKw_ = 0; order_.reset(); busy_ = false;
    showMessage({}); render();
}

VehicleProfilePage::VehicleProfilePage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("vehicleProfilePage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 18, 28, 18);
    sections_ = new QStackedWidget(this);
    root->addWidget(sections_);
    overview_ = new QWidget(sections_);
    auto *overviewLayout = new QHBoxLayout(overview_);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    overviewLayout->setSpacing(20);
    auto *profileCard = card(overview_);
    auto *profileLayout = new QVBoxLayout(profileCard);
    avatar_ = new QLabel(QStringLiteral("本机头像\n默认"), profileCard);
    avatar_->setObjectName(QStringLiteral("vehicleLocalAvatar"));
    avatar_->setAlignment(Qt::AlignCenter);
    avatar_->setMinimumSize(112, 112);
    avatar_->setStyleSheet(QStringLiteral("background:#e8eee4;border-radius:18px;color:#65796c"));
    nickname_ = new QLabel(QStringLiteral("未登录"), profileCard);
    nickname_->setObjectName(QStringLiteral("vehicleProfileNickname"));
    nickname_->setProperty("role", "sectionTitle");
    phone_ = new QLabel(profileCard);
    balance_ = new QLabel(QStringLiteral("余额 --"), profileCard);
    balance_->setObjectName(QStringLiteral("vehicleProfileBalance"));
    profileLayout->addWidget(avatar_, 0, Qt::AlignLeft);
    profileLayout->addWidget(nickname_); profileLayout->addWidget(phone_); profileLayout->addWidget(balance_);
    profileLayout->addStretch();
    overviewLayout->addWidget(profileCard, 35);
    auto *actions = card(overview_);
    auto *actionsLayout = new QGridLayout(actions);
    nicknameInput_ = new QLineEdit(actions);
    nicknameInput_->setObjectName(QStringLiteral("vehicleNicknameInput"));
    nicknameInput_->setPlaceholderText(QStringLiteral("昵称")); nicknameInput_->setMinimumHeight(tokens::Touch);
    saveNickname_ = actionButton(QStringLiteral("保存昵称"), actions, true);
    saveNickname_->setObjectName(QStringLiteral("vehicleSaveNicknameButton"));
    rechargeInput_ = new QLineEdit(actions);
    rechargeInput_->setObjectName(QStringLiteral("vehicleRechargeInput"));
    rechargeInput_->setPlaceholderText(QStringLiteral("充值金额（元）")); rechargeInput_->setMinimumHeight(tokens::Touch);
    rechargeButton_ = actionButton(QStringLiteral("充值"), actions, true);
    rechargeButton_->setObjectName(QStringLiteral("vehicleRechargeButton"));
    actionsLayout->addWidget(nicknameInput_, 0, 0); actionsLayout->addWidget(saveNickname_, 0, 1);
    actionsLayout->addWidget(rechargeInput_, 1, 0); actionsLayout->addWidget(rechargeButton_, 1, 1);
    const QList<QPair<QString, const char *>> entries = {
        {QStringLiteral("我的订单"), "vehicleOrdersButton"},
        {QStringLiteral("客服助理"), "vehicleSupportButton"},
        {QStringLiteral("故障报修"), "vehicleRepairButton"},
        {QStringLiteral("我的工单"), "vehicleTicketsButton"},
        {QStringLiteral("退出登录"), "vehicleLogoutButton"}};
    for (int i = 0; i < entries.size(); ++i) {
        auto *button = actionButton(entries[i].first, actions);
        button->setObjectName(QString::fromLatin1(entries[i].second));
        actionsLayout->addWidget(button, 2 + i / 2, i % 2);
        actions_.append(button);
    }
    message_ = new QLabel(actions); message_->setWordWrap(true);
    actionsLayout->addWidget(message_, 5, 0, 1, 2);
    overviewLayout->addWidget(actions, 65);
    sections_->addWidget(overview_);

    ordersPage_ = new QWidget(sections_);
    auto *ordersLayout = new QVBoxLayout(ordersPage_);
    auto *back = actionButton(QStringLiteral("‹ 返回我的"), ordersPage_);
    ordersList_ = new QListWidget(ordersPage_);
    ordersList_->setObjectName(QStringLiteral("vehicleOrderList"));
    ordersLayout->addWidget(back); ordersLayout->addWidget(ordersList_, 1);
    sections_->addWidget(ordersPage_);
    connect(back, &QPushButton::clicked, this, &VehicleProfilePage::showOverview);
    connect(saveNickname_, &QPushButton::clicked, this, [this] { emit nicknameRequested(nicknameInput_->text().trimmed()); });
    connect(rechargeButton_, &QPushButton::clicked, this, [this] { emit rechargeRequested(rechargeInput_->text().trimmed()); });
    connect(actions_[0], &QPushButton::clicked, this, &VehicleProfilePage::ordersRequested);
    connect(actions_[1], &QPushButton::clicked, this, &VehicleProfilePage::supportRequested);
    connect(actions_[2], &QPushButton::clicked, this, &VehicleProfilePage::repairRequested);
    connect(actions_[3], &QPushButton::clicked, this, &VehicleProfilePage::ticketsRequested);
    connect(actions_[4], &QPushButton::clicked, this, &VehicleProfilePage::logoutRequested);
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
    avatar_->setPixmap(circularAvatar(image, 96, devicePixelRatioF()));
    avatar_->setToolTip(QStringLiteral("头像仅保存在本车载客户端，不与手机端同步"));
}

void VehicleProfilePage::setBusy(bool busy)
{
    nicknameInput_->setEnabled(!busy); rechargeInput_->setEnabled(!busy);
    saveNickname_->setEnabled(!busy); rechargeButton_->setEnabled(!busy);
    for (auto *button : actions_) button->setEnabled(!busy);
}

void VehicleProfilePage::showOrders(const QList<protocol::OrderDto> &orders)
{
    ordersList_->clear();
    for (const auto &order : orders) {
        ordersList_->addItem(QStringLiteral("%1  %2\n%3 · %4 · %5 kWh · ¥%6")
                                 .arg(order.orderNo, statusText(order.status), order.stationName, order.pileCode)
                                 .arg(order.energyWh / 1000.0, 0, 'f', 3)
                                 .arg(order.amountCents / 100.0, 0, 'f', 2));
    }
    if (orders.isEmpty()) ordersList_->addItem(QStringLiteral("暂无订单"));
    showOrderList();
}

void VehicleProfilePage::showMessage(const QString &message, bool error)
{
    message_->setText(message);
    message_->setStyleSheet(error ? QStringLiteral("color:#a33b32") : QString());
}
void VehicleProfilePage::showOverview() { sections_->setCurrentWidget(overview_); }
void VehicleProfilePage::showOrderList() { sections_->setCurrentWidget(ordersPage_); }
void VehicleProfilePage::reset()
{
    nickname_->setText(QStringLiteral("未登录")); phone_->clear(); balance_->setText(QStringLiteral("余额 --"));
    nicknameInput_->clear(); rechargeInput_->clear(); ordersList_->clear(); showMessage({}); showOverview();
}

}  // namespace charging::client
