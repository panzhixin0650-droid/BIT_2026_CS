#include "vehicle/vehicle_main_window.h"

#include "api/i_charging_api.h"
#include "common/charging_session_state.h"
#include "local/avatar_storage.h"
#include "local/i_map_service.h"
#include "ui/api_error_message.h"
#include "ui/client_theme.h"
#include "ui/login_controller.h"
#include "ui/login_page.h"
#include "ui/support_desk_page.h"
#include "ui/support_page.h"
#include "vehicle/vehicle_pages.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace charging::client {
namespace {

bool matches(const ApiResponse &response, const QString &request, const char *type)
{
    return !request.isEmpty() && response.requestId == request
        && response.type == QString::fromLatin1(type);
}

QString failure(const ApiResponse &response, const QString &fallback)
{
    return apiErrorMessage(response, fallback);
}

}  // namespace

VehicleMainWindow::VehicleMainWindow(IChargingApi &api, IMapService &mapService,
                                     const AssistantConfig &assistantConfig, QWidget *parent)
    : QMainWindow(parent), api_(api), mapService_(mapService), assistantConfig_(assistantConfig)
{
    setObjectName(QStringLiteral("vehicleMainWindow"));
    setWindowTitle(QStringLiteral("悦充车载端"));
    resize(1280, 720);
    setMinimumSize(1024, 600);
    setStyleSheet(clientThemeStyleSheet() + QStringLiteral(R"QSS(
        QMainWindow#vehicleMainWindow QPushButton { min-height:48px; }
        QTabWidget#vehicleNavigation::pane { border: none; }
        QTabWidget#vehicleNavigation QTabBar::tab {
            min-width: 190px; min-height: 58px; padding: 0 28px;
            color:#65796c; background:#edf1e9; font-size:16px; font-weight:600;
        }
        QTabWidget#vehicleNavigation QTabBar::tab:selected { color:white; background:#245c45; }
        #vehicleHomeSidePanel { background:#fffefa; border:1px solid #dce5d7; border-radius:18px; }
        #vehicleHomeSidePanel QPushButton[role="stationCard"] { text-align:left; padding:8px 12px; }
        #vehicleChargingProgress { min-height:56px; text-align:center; font-size:17px; font-weight:700; }
    )QSS"));

    auto *shell = new QWidget(this);
    auto *shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    auto *header = new QFrame(shell);
    header->setObjectName(QStringLiteral("vehicleHeader"));
    header->setFixedHeight(70);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 8, 24, 8);
    auto *brand = new QLabel(QStringLiteral("ϟ  悦充车载端"), header);
    auto font = brand->font(); font.setPointSize(20); font.setBold(true); brand->setFont(font);
    headerLayout->addWidget(brand);
    auto *notice = new QLabel(QStringLiteral("请停车后操作 · 演示位置，不代表车辆 GPS"), header);
    notice->setStyleSheet(QStringLiteral("color:#65796c"));
    headerLayout->addWidget(notice, 1, Qt::AlignCenter);
    refresh_ = new QPushButton(QStringLiteral("刷新"), header);
    refresh_->setObjectName(QStringLiteral("vehicleRefreshButton"));
    refresh_->setMinimumSize(72, 48);
    account_ = new QPushButton(QStringLiteral("未登录"), header);
    account_->setObjectName(QStringLiteral("vehicleAccountButton"));
    account_->setMinimumSize(150, 48);
    headerLayout->addWidget(refresh_); headerLayout->addWidget(account_);
    shellLayout->addWidget(header);

    applicationPages_ = new QStackedWidget(shell);
    applicationPages_->setObjectName(QStringLiteral("vehicleApplicationPages"));
    loginPage_ = new LoginPage(applicationPages_);
    navigation_ = new QTabWidget(applicationPages_);
    navigation_->setObjectName(QStringLiteral("vehicleNavigation"));
    navigation_->setTabPosition(QTabWidget::South);
    navigation_->setDocumentMode(true);
    navigation_->tabBar()->setExpanding(true);
    home_ = new VehicleHomePage(mapService.mapScriptUrl(), navigation_);
    charging_ = new VehicleChargingPage(navigation_);
    profile_ = new VehicleProfilePage(navigation_);
    navigation_->addTab(home_, QStringLiteral("首页"));
    navigation_->addTab(charging_, QStringLiteral("充电"));
    navigation_->addTab(profile_, QStringLiteral("我的"));
    applicationPages_->addWidget(loginPage_);
    applicationPages_->addWidget(navigation_);
    applicationPages_->setCurrentWidget(loginPage_);
    shellLayout->addWidget(applicationPages_, 1);
    setCentralWidget(shell);

    loginController_ = new LoginController(*loginPage_, api_, this);
    avatarStorage_ = std::make_unique<AvatarStorage>();
    chargingTimer_ = new QTimer(this);
    chargingTimer_->setInterval(1000);

    connect(loginController_, &LoginController::loginSucceeded,
            this, &VehicleMainWindow::authenticated);
    connect(refresh_, &QPushButton::clicked, this, [this] {
        if (!authenticated_) return;
        if (navigation_->currentWidget() == home_) refreshHome();
        else if (navigation_->currentWidget() == charging_) refreshCharging();
        else refreshProfile();
    });
    connect(account_, &QPushButton::clicked, this, [this] {
        if (authenticated_) navigation_->setCurrentWidget(profile_);
    });
    connect(navigation_, &QTabWidget::currentChanged, this, [this](int) {
        if (!authenticated_) return;
        if (navigation_->currentWidget() == home_) refreshHome();
        else if (navigation_->currentWidget() == charging_) refreshCharging();
        else { profile_->showOverview(); refreshProfile(); }
    });
    connect(chargingTimer_, &QTimer::timeout, this, [this] {
        if (!authenticated_ || !currentOrder_
            || currentOrder_->status != protocol::OrderStatus::Charging
            || !progressRequest_.isEmpty()) return;
        progressRequest_ = api_.getChargingProgress(currentOrder_->orderId);
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && authenticated_) {
            refreshHome(); refreshCharging(); refreshProfile();
        }
    });

    connect(home_, &VehicleHomePage::refreshRequested, this, &VehicleMainWindow::refreshHome);
    connect(home_, &VehicleHomePage::stationSelected, this, [this](qint64 stationId) {
        detailPurpose_ = DetailPurpose::SelectedStation;
        stationDetailRequest_ = api_.getStation(stationId);
    });
    connect(home_, &VehicleHomePage::reserveRequested, this, [this](const QString &pileCode) {
        if (!actionRequest_.isEmpty()) return;
        actionRequest_ = api_.reserve(pileCode);
        home_->showMessage(QStringLiteral("正在预约…"));
    });
    connect(home_, &VehicleHomePage::chargeRequested, this,
            [this](const QString &pileCode, double power) {
        candidatePile_ = pileCode; candidatePowerKw_ = power;
        ratedPowerByPile_.insert(pileCode, power);
        charging_->prepare(pileCode, power);
        navigation_->setCurrentWidget(charging_);
    });
    connect(home_, &VehicleHomePage::locationRequested, this, [this](const QString &address) {
        if (address == QStringLiteral("演示位置")) {
            home_->setResolvedLocation({address, 123.42, 41.70}); refreshHome(); return;
        }
        if (address.isEmpty() || !geocodeRequest_.isEmpty()) return;
        geocodePurpose_ = GeocodePurpose::Location;
        geocodeRequest_ = mapService_.geocode(address);
        home_->showMessage(QStringLiteral("正在解析位置…"));
    });
    connect(home_, &VehicleHomePage::routeRequested, this,
            [this](const MapLocation &start, const MapLocation &end, RouteMode mode) {
        pendingRouteEnd_ = end; pendingRouteMode_ = mode;
        const auto current = home_->currentLocation();
        if (start.address.isEmpty() || start.address == current.address) {
            routeRequest_ = mapService_.openRoute(current, end, mode);
        } else {
            geocodePurpose_ = GeocodePurpose::RouteStart;
            geocodeRequest_ = mapService_.geocode(start.address);
        }
        home_->showRouteMessage(QStringLiteral("正在规划路线…"));
    });

    connect(charging_, &VehicleChargingPage::refreshRequested, this, &VehicleMainWindow::refreshCharging);
    connect(charging_, &VehicleChargingPage::startRequested, this, &VehicleMainWindow::startCharging);
    connect(charging_, &VehicleChargingPage::stopRequested, this, &VehicleMainWindow::stopCharging);
    connect(charging_, &VehicleChargingPage::cancelRequested, this, [this](qint64 orderId) {
        if (actionRequest_.isEmpty()) { charging_->setBusy(true); actionRequest_ = api_.cancel(orderId); }
    });
    connect(charging_, &VehicleChargingPage::payRequested, this, [this](qint64 orderId) {
        if (actionRequest_.isEmpty()) { charging_->setBusy(true); actionRequest_ = api_.payOrder(orderId); }
    });
    connect(charging_, &VehicleChargingPage::rechargeRequested, this, [this] {
        navigation_->setCurrentWidget(profile_); profile_->showOverview();
    });
    connect(charging_, &VehicleChargingPage::ordersRequested, this, [this] {
        navigation_->setCurrentWidget(profile_); ordersRequest_ = api_.listOrders();
    });
    connect(charging_, &VehicleChargingPage::repairRequested, this,
            [this](const QString &pile) { openDesk(true, false, pile); });

    connect(profile_, &VehicleProfilePage::refreshRequested, this, &VehicleMainWindow::refreshProfile);
    connect(profile_, &VehicleProfilePage::ordersRequested, this, [this] {
        if (ordersRequest_.isEmpty()) ordersRequest_ = api_.listOrders();
    });
    connect(profile_, &VehicleProfilePage::supportRequested, this, &VehicleMainWindow::openSupport);
    connect(profile_, &VehicleProfilePage::repairRequested, this,
            [this] { openDesk(true, false); });
    connect(profile_, &VehicleProfilePage::ticketsRequested, this,
            [this] { openDesk(false, true); });
    connect(profile_, &VehicleProfilePage::nicknameRequested, this, [this](const QString &nickname) {
        if (!actionRequest_.isEmpty()) return;
        actionRequest_ = api_.updateNickname(nickname);
        profile_->setBusy(true);
    });
    connect(profile_, &VehicleProfilePage::rechargeRequested, this, [this](const QString &amount) {
        bool ok = false; const double yuan = amount.toDouble(&ok);
        const qint64 cents = qRound64(yuan * 100.0);
        if (!ok || cents < 1 || cents > 1000000) {
            profile_->showMessage(QStringLiteral("请输入 0.01～10000 元"), true); return;
        }
        if (!actionRequest_.isEmpty()) return;
        actionRequest_ = api_.recharge(cents); profile_->setBusy(true);
    });
    connect(profile_, &VehicleProfilePage::logoutRequested, this, [this] {
        if (actionRequest_.isEmpty()) actionRequest_ = api_.logout();
    });

    using namespace protocol::MessageType;
    connect(&api_, &IChargingApi::stationListCompleted, this, [this](const StationListResult &result) {
        if (!matches(result.response, stationListRequest_, StationList)) return;
        stationListRequest_.clear(); home_->setLoading(false);
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) { home_->showMessage(failure(result.response, QStringLiteral("电站刷新失败")), true); return; }
        home_->showStations(result.payload->items);
    });
    connect(&api_, &IChargingApi::stationDetailCompleted, this, [this](const StationDetailResult &result) {
        if (!matches(result.response, stationDetailRequest_, StationDetail)) return;
        stationDetailRequest_.clear();
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) { home_->showMessage(failure(result.response, QStringLiteral("站点详情失败")), true); return; }
        for (const auto &pile : result.payload->piles) ratedPowerByPile_.insert(pile.pileCode, pile.ratedPowerKw);
        if (detailPurpose_ == DetailPurpose::SelectedStation) home_->showStationDetail(*result.payload);
        else if (currentOrder_) {
            candidatePowerKw_ = ratedPowerByPile_.value(currentOrder_->pileCode, 0.0);
            charging_->prepare(currentOrder_->pileCode, candidatePowerKw_);
            charging_->showOrder(*currentOrder_);
        }
        detailPurpose_ = DetailPurpose::None;
    });
    connect(&api_, &IChargingApi::orderListCompleted, this, [this](const OrderListResult &result) {
        if (matches(result.response, historyRequest_, OrderList)) {
            historyRequest_.clear();
            if (handleInvalidSession(result.response.code)) return;
            if (result.ok() && result.payload) home_->showVisitHistory(result.payload->items);
            else home_->showMessage(failure(result.response, QStringLiteral("最近使用记录刷新失败")), true);
            return;
        }
        if (matches(result.response, ordersRequest_, OrderList)) {
            ordersRequest_.clear();
            if (handleInvalidSession(result.response.code)) return;
            if (result.ok() && result.payload) profile_->showOrders(result.payload->items);
            else profile_->showMessage(failure(result.response, QStringLiteral("订单读取失败")), true);
            return;
        }
        if (!matches(result.response, finalHistoryRequest_, OrderList)) return;
        finalHistoryRequest_.clear();
        if (handleInvalidSession(result.response.code)) return;
        if (result.ok() && result.payload && currentOrder_) {
            for (const auto &order : result.payload->items) if (order.orderId == currentOrder_->orderId) {
                currentOrder_ = order; charging_->showOrder(order); break;
            }
        }
    });
    connect(&api_, &IChargingApi::currentOrderCompleted, this, [this](const CurrentOrderResult &result) {
        if (!matches(result.response, currentRequest_, OrderCurrent)) return;
        currentRequest_.clear();
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) { charging_->setBusy(false); charging_->showMessage(failure(result.response, QStringLiteral("当前订单刷新失败")), true); currentPurpose_ = CurrentPurpose::None; return; }
        const auto purpose = currentPurpose_; currentPurpose_ = CurrentPurpose::None;
        if (purpose == CurrentPurpose::StartCheck) {
            const auto decision = session::startDecision(result.payload->order, candidatePile_);
            if (decision == session::StartDecision::Direct) {
                actionRequest_ = api_.startCharging(candidatePile_); return;
            }
            const auto order = *result.payload->order;
            if (decision == session::StartDecision::UseReservation) {
                actionRequest_ = api_.startCharging(candidatePile_, order.orderId); return;
            }
            currentOrder_ = order; charging_->showOrder(order); charging_->setBusy(false);
            charging_->showMessage(QStringLiteral("请先处理当前订单"), true); return;
        }
        if (purpose == CurrentPurpose::RechargeCheck) {
            if (result.payload->order && result.payload->order->status == protocol::OrderStatus::PendingPayment) {
                actionRequest_ = api_.payOrder(result.payload->order->orderId); return;
            }
            refreshProfile(); return;
        }
        if (result.payload->order) {
            currentOrder_ = *result.payload->order;
            candidatePile_ = currentOrder_->pileCode;
            candidatePowerKw_ = ratedPowerByPile_.value(candidatePile_, candidatePowerKw_);
            charging_->prepare(candidatePile_, candidatePowerKw_); charging_->showOrder(*currentOrder_);
            if (currentOrder_->status == protocol::OrderStatus::Charging) chargingTimer_->start();
            else chargingTimer_->stop();
            if (candidatePowerKw_ <= 0 && stationDetailRequest_.isEmpty()) {
                detailPurpose_ = DetailPurpose::ResolveOrderPower;
                stationDetailRequest_ = api_.getStation(currentOrder_->stationId);
            }
        } else {
            chargingTimer_->stop();
            if (session::needsFinalHistory(currentOrder_)) finalHistoryRequest_ = api_.listOrders();
            else charging_->showNoOrder();
        }
        charging_->setBusy(false);
    });
    connect(&api_, &IChargingApi::chargingProgressCompleted, this, [this](const ChargingProgressResult &result) {
        if (!matches(result.response, progressRequest_, OrderProgress)) return;
        progressRequest_.clear();
        if (handleInvalidSession(result.response.code)) return;
        if (result.response.code == protocol::ErrorCode::IllegalOrderState) { refreshCharging(); return; }
        if (!result.ok() || !result.payload) { charging_->showMessage(failure(result.response, QStringLiteral("进度刷新失败")), true); return; }
        currentOrder_ = result.payload->order; charging_->showOrder(*currentOrder_);
    });
    connect(&api_, &IChargingApi::reservationCompleted, this, [this](const OrderResult &result) {
        if (!matches(result.response, actionRequest_, OrderReserve)) return;
        actionRequest_.clear();
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) {
            if (result.response.code == protocol::ErrorCode::CurrentOrderExists) {
                navigation_->setCurrentWidget(charging_); refreshCharging();
            } else home_->showMessage(failure(result.response, QStringLiteral("预约失败")), true);
            return;
        }
        currentOrder_ = result.payload->order; candidatePile_ = currentOrder_->pileCode;
        charging_->prepare(candidatePile_, ratedPowerByPile_.value(candidatePile_, 0.0));
        charging_->showOrder(*currentOrder_); navigation_->setCurrentWidget(charging_);
    });
    connect(&api_, &IChargingApi::chargingStartCompleted, this, [this](const OrderResult &result) {
        if (!matches(result.response, actionRequest_, OrderStart)) return;
        actionRequest_.clear(); charging_->setBusy(false);
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) {
            if (result.response.code == protocol::ErrorCode::CurrentOrderExists
                || result.response.code == protocol::ErrorCode::IllegalOrderState) refreshCharging();
            else charging_->showMessage(failure(result.response, QStringLiteral("开始充电失败")), true);
            return;
        }
        currentOrder_ = result.payload->order; charging_->showOrder(*currentOrder_); chargingTimer_->start();
        charging_->showMessage(QStringLiteral("充电已开始，数据由服务端每秒刷新"));
    });
    connect(&api_, &IChargingApi::cancellationCompleted, this, [this](const OrderResult &result) {
        if (!matches(result.response, actionRequest_, OrderCancel)) return;
        actionRequest_.clear(); charging_->setBusy(false);
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) {
            if (result.response.code == protocol::ErrorCode::IllegalOrderState) refreshCharging();
            else charging_->showMessage(failure(result.response, QStringLiteral("取消预约失败")), true);
            return;
        }
        currentOrder_ = result.payload->order; charging_->showOrder(*currentOrder_);
        charging_->showMessage(QStringLiteral("预约已取消")); refreshHome();
    });
    connect(&api_, &IChargingApi::chargingStopCompleted, this, [this](const ChargingStopResult &result) {
        if (!matches(result.response, actionRequest_, OrderStop)) return;
        actionRequest_.clear(); charging_->setBusy(false);
        if (handleInvalidSession(result.response.code)) return;
        if (result.response.code == protocol::ErrorCode::IllegalOrderState) { refreshCharging(); return; }
        if (!result.ok() || !result.payload) { charging_->showMessage(failure(result.response, QStringLiteral("停止结果未知，请刷新核对")), true); return; }
        currentOrder_ = result.payload->order; charging_->showOrder(*currentOrder_); chargingTimer_->stop();
        charging_->showMessage(result.payload->paid ? QStringLiteral("充电已结束并完成结算")
                                                    : QStringLiteral("充电已结束，余额不足，请前往充值"), !result.payload->paid);
        refreshProfile();
    });
    connect(&api_, &IChargingApi::profileCompleted, this, [this](const UserResult &result) {
        if (!matches(result.response, profileRequest_, UserProfileGet)) return;
        profileRequest_.clear(); profile_->setBusy(false);
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) { profile_->showMessage(failure(result.response, QStringLiteral("资料刷新失败")), true); return; }
        user_ = result.payload->user; profile_->setUser(user_, avatarStorage_->avatarPath(user_.phone)); updateHeader();
    });
    connect(&api_, &IChargingApi::profileUpdateCompleted, this, [this](const UserResult &result) {
        if (!matches(result.response, actionRequest_, UserProfileUpdate)) return;
        actionRequest_.clear(); profile_->setBusy(false);
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) { profile_->showMessage(failure(result.response, QStringLiteral("昵称保存失败")), true); return; }
        user_ = result.payload->user; profile_->setUser(user_, avatarStorage_->avatarPath(user_.phone)); home_->setUser(user_); updateHeader();
        profile_->showMessage(QStringLiteral("昵称已从服务端刷新"));
    });
    connect(&api_, &IChargingApi::rechargeCompleted, this, [this](const RechargeResult &result) {
        if (!matches(result.response, actionRequest_, WalletRecharge)) return;
        actionRequest_.clear(); profile_->setBusy(false);
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) { profile_->showMessage(failure(result.response, QStringLiteral("充值结果未知，请刷新核对")), true); return; }
        currentPurpose_ = CurrentPurpose::RechargeCheck; currentRequest_ = api_.getCurrentOrder();
        profile_->showMessage(QStringLiteral("充值成功，正在刷新余额和待支付订单"));
    });
    connect(&api_, &IChargingApi::paymentCompleted, this, [this](const PaymentResult &result) {
        if (!matches(result.response, actionRequest_, OrderPay)) return;
        actionRequest_.clear(); charging_->setBusy(false);
        if (handleInvalidSession(result.response.code)) return;
        if (!result.ok() || !result.payload) { profile_->showMessage(failure(result.response, QStringLiteral("结算失败")), true); refreshProfile(); return; }
        currentOrder_ = result.payload->order; charging_->showOrder(*currentOrder_);
        profile_->showMessage(QStringLiteral("待支付订单已结算")); refreshProfile();
    });
    connect(&api_, &IChargingApi::logoutCompleted, this, [this](const LogoutResult &result) {
        if (!matches(result.response, actionRequest_, AuthLogout)) return;
        actionRequest_.clear(); showLogin();
    });
    connect(&mapService_, &IMapService::geocodeCompleted, this, [this](const GeocodeResult &result) {
        if (result.requestId != geocodeRequest_) return;
        geocodeRequest_.clear();
        if (!result.success || !result.location) { home_->showMessage(result.message.isEmpty() ? QStringLiteral("位置解析失败") : result.message, true); geocodePurpose_ = GeocodePurpose::None; return; }
        if (geocodePurpose_ == GeocodePurpose::Location) { home_->setResolvedLocation(*result.location); refreshHome(); }
        else if (geocodePurpose_ == GeocodePurpose::RouteStart) routeRequest_ = mapService_.openRoute(*result.location, pendingRouteEnd_, pendingRouteMode_);
        geocodePurpose_ = GeocodePurpose::None;
    });
    connect(&mapService_, &IMapService::routeCompleted, this, [this](const RouteResult &result) {
        if (result.requestId != routeRequest_) return;
        routeRequest_.clear(); home_->showRoute(result);
    });
}

VehicleMainWindow::~VehicleMainWindow()
{
    if (desk_) { desk_->resetSession(); delete desk_; desk_ = nullptr; }
}

void VehicleMainWindow::authenticated(const protocol::UserDto &user, bool)
{
    ++sessionGeneration_; clearPending(); authenticated_ = true; user_ = user;
    home_->setUser(user_); profile_->setUser(user_, avatarStorage_->avatarPath(user_.phone));
    updateHeader(); applicationPages_->setCurrentWidget(navigation_);
    navigation_->setCurrentWidget(home_); refreshHome(); refreshCharging(); refreshProfile();
}

void VehicleMainWindow::showLogin(const QString &message)
{
    ++sessionGeneration_; authenticated_ = false; chargingTimer_->stop(); clearPending();
    currentOrder_.reset(); candidatePile_.clear(); candidatePowerKw_ = 0; ratedPowerByPile_.clear();
    home_->reset(); charging_->reset(); profile_->reset();
    if (support_) support_->resetConversation();
    if (desk_) desk_->resetSession();
    loginPage_->setLoading(false); loginPage_->setErrorMessage(message);
    applicationPages_->setCurrentWidget(loginPage_); updateHeader();
}

void VehicleMainWindow::refreshHome()
{
    if (!authenticated_ || !stationListRequest_.isEmpty()) return;
    home_->setLoading(true); stationListRequest_ = api_.listStations(home_->stationQuery());
    if (historyRequest_.isEmpty()) historyRequest_ = api_.listOrders();
}

void VehicleMainWindow::refreshCharging()
{
    if (!authenticated_ || !currentRequest_.isEmpty()) return;
    currentPurpose_ = CurrentPurpose::Refresh; currentRequest_ = api_.getCurrentOrder();
}

void VehicleMainWindow::refreshProfile()
{
    if (!authenticated_ || !profileRequest_.isEmpty()) return;
    profile_->setBusy(true); profileRequest_ = api_.getProfile();
}

void VehicleMainWindow::startCharging(const QString &pileCode)
{
    if (!authenticated_ || pileCode.trimmed().isEmpty() || !actionRequest_.isEmpty()) return;
    candidatePile_ = pileCode.trimmed(); charging_->setBusy(true);
    currentPurpose_ = CurrentPurpose::StartCheck; currentRequest_ = api_.getCurrentOrder();
}

void VehicleMainWindow::stopCharging()
{
    if (!currentOrder_ || currentOrder_->status != protocol::OrderStatus::Charging
        || !actionRequest_.isEmpty()) return;
    QMessageBox confirm(QMessageBox::Warning, QStringLiteral("确认提前停止"),
                        QStringLiteral("停止后将由服务端生成最终电量和金额，是否继续？"),
                        QMessageBox::Yes | QMessageBox::No, this);
    confirm.setObjectName(QStringLiteral("vehicleStopConfirmation"));
    confirm.button(QMessageBox::Yes)->setText(QStringLiteral("确认停止"));
    confirm.button(QMessageBox::No)->setText(QStringLiteral("继续充电"));
    if (confirm.exec() != QMessageBox::Yes) return;
    charging_->setBusy(true); actionRequest_ = api_.stopCharging(currentOrder_->orderId);
}

void VehicleMainWindow::openSupport()
{
    if (!support_) {
        assistant_ = new AssistantService(assistantConfig_, this);
        supportContainer_ = new QWidget(applicationPages_);
        auto *layout = new QVBoxLayout(supportContainer_);
        layout->setContentsMargins(18, 10, 18, 10);
        auto *back = new QPushButton(QStringLiteral("‹ 返回我的"), supportContainer_);
        back->setObjectName(QStringLiteral("vehicleSupportBackButton"));
        back->setMinimumHeight(48);
        support_ = new SupportPage(*assistant_, supportContainer_);
        support_->setObjectName(QStringLiteral("vehicleSupportPage"));
        layout->addWidget(back); layout->addWidget(support_, 1);
        applicationPages_->addWidget(supportContainer_);
        connect(back, &QPushButton::clicked, this, [this] {
            applicationPages_->setCurrentWidget(navigation_);
            navigation_->setCurrentWidget(profile_);
        });
        connect(support_, &SupportPage::supportDeskRequested, this, [this] { openDesk(false, false); });
    }
    applicationPages_->setCurrentWidget(supportContainer_);
}

void VehicleMainWindow::openDesk(bool repair, bool tickets, const QString &pileCode)
{
    if (!desk_) {
        const auto config = assistantConfig_.forSupportDesk();
        auto *deskService = new AssistantService(config, this, nullptr, AssistantPurpose::SupportDesk);
        auto *summaryService = new AssistantService(config, this, nullptr, AssistantPurpose::TicketSummary);
        desk_ = new SupportDeskPage(api_, *deskService, *summaryService, applicationPages_);
        desk_->setObjectName(QStringLiteral("vehicleSupportDeskPage"));
        applicationPages_->addWidget(desk_);
        connect(desk_, &SupportDeskPage::backRequested, this, [this] { applicationPages_->setCurrentWidget(navigation_); });
        connect(desk_, &SupportDeskPage::invalidSession, this, &VehicleMainWindow::showLogin);
    }
    if (repair) desk_->openRepair(pileCode);
    else if (tickets) desk_->openTickets();
    else desk_->openDesk(support_ ? support_->recentHistory() : QList<AssistantTurn>{});
    applicationPages_->setCurrentWidget(desk_);
}

bool VehicleMainWindow::handleInvalidSession(int code)
{
    if (code != protocol::ErrorCode::InvalidSession) return false;
    showLogin(QStringLiteral("登录状态已失效，请重新登录")); return true;
}

void VehicleMainWindow::updateHeader()
{
    refresh_->setEnabled(authenticated_);
    if (!authenticated_) { account_->setText(QStringLiteral("未登录")); return; }
    const QString phone = user_.phone.size() >= 7
        ? user_.phone.left(3) + QStringLiteral("****") + user_.phone.right(4) : user_.phone;
    account_->setText(user_.nickname.left(8) + QStringLiteral("  ") + phone);
}

void VehicleMainWindow::clearPending()
{
    stationListRequest_.clear(); stationDetailRequest_.clear(); historyRequest_.clear();
    currentRequest_.clear(); progressRequest_.clear(); actionRequest_.clear(); profileRequest_.clear();
    ordersRequest_.clear(); finalHistoryRequest_.clear(); geocodeRequest_.clear(); routeRequest_.clear();
    currentPurpose_ = CurrentPurpose::None; detailPurpose_ = DetailPurpose::None;
    geocodePurpose_ = GeocodePurpose::None;
}

}  // namespace charging::client
