#include "ui/main_window.h"

#include "api/i_charging_api.h"
#include "local/avatar_storage.h"
#include "local/i_map_service.h"
#include "local/mock_map_service.h"
#include "ui/client_theme.h"
#include "ui/login_controller.h"
#include "ui/login_page.h"
#include "ui/map_controller.h"
#include "ui/order_controller.h"
#include "ui/order_page.h"
#include "ui/profile_controller.h"
#include "ui/profile_page.h"
#include "ui/scan_controller.h"
#include "ui/scan_page.h"
#include "ui/station_browser_controller.h"
#include "ui/station_browser_page.h"
#include "assistant/assistant_service.h"
#include "ui/support_page.h"

#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace charging::client {

namespace {

void showPendingPaymentNotice(QWidget *parent)
{
    QMessageBox notice(QMessageBox::Warning,
                       QStringLiteral("存在待支付订单"),
                       QStringLiteral("您有待支付订单，请先完成结算。"),
                       QMessageBox::Ok,
                       parent);
    notice.setObjectName(QStringLiteral("pendingPaymentDialog"));
    notice.button(QMessageBox::Ok)->setText(QStringLiteral("前往订单"));
    notice.exec();
}

void showChargingStartedNotice(QWidget *parent,
                               const protocol::OrderDto &order)
{
    QMessageBox notice(QMessageBox::Information,
                       QStringLiteral("充电已开始"),
                       QStringLiteral("充电桩 %1 已开始充电。")
                           .arg(order.pileCode),
                       QMessageBox::Ok,
                       parent);
    notice.setObjectName(QStringLiteral("chargingStartedDialog"));
    notice.button(QMessageBox::Ok)->setText(QStringLiteral("查看充电进度"));
    notice.exec();
}

void showChargingStoppedNotice(QWidget *parent,
                               const ChargingStopPayload &result)
{
    const bool debt = !result.paid;
    const QString message = debt
        ? QStringLiteral("充电已结束，当前欠费 ¥%1，请充值后完成结算。")
              .arg(result.shortfallCents.value_or(0) / 100.0, 0, 'f', 2)
        : QStringLiteral("充电已结束并完成结算，实付 ¥%1。")
              .arg(result.order.amountCents / 100.0, 0, 'f', 2);
    QMessageBox notice(debt ? QMessageBox::Warning : QMessageBox::Information,
                       debt ? QStringLiteral("充电结束，余额不足")
                            : QStringLiteral("充电结束"),
                       message,
                       QMessageBox::Ok,
                       parent);
    notice.setObjectName(debt ? QStringLiteral("chargingDebtDialog")
                              : QStringLiteral("chargingStoppedDialog"));
    notice.button(QMessageBox::Ok)->setText(
        debt ? QStringLiteral("前往充值") : QStringLiteral("知道了"));
    notice.exec();
}

void showAutomaticSettlementNotice(QWidget *parent,
                                   const PaymentPayload &result)
{
    QMessageBox notice(
        QMessageBox::Information,
        QStringLiteral("自动结算成功"),
        QStringLiteral("待支付订单 %1 已结算，实付 ¥%2。")
            .arg(result.order.orderNo)
            .arg(result.order.amountCents / 100.0, 0, 'f', 2),
        QMessageBox::Ok,
        parent);
    notice.setObjectName(QStringLiteral("automaticSettlementDialog"));
    notice.button(QMessageBox::Ok)->setText(QStringLiteral("知道了"));
    notice.exec();
}

}  // namespace

MainWindow::MainWindow(IChargingApi &api, QWidget *parent)
    : QMainWindow(parent)
    , ownedMapService_(std::make_unique<MockMapService>())
{
    initialize(api, *ownedMapService_);
}

MainWindow::MainWindow(IChargingApi &api,
                       IMapService &mapService,
                       QWidget *parent)
    : QMainWindow(parent)
{
    initialize(api, mapService);
}

MainWindow::MainWindow(IChargingApi &api, IMapService &mapService,
                       const AssistantConfig &assistantConfig, QWidget *parent)
    : QMainWindow(parent)
{
    initialize(api, mapService, assistantConfig);
}

void MainWindow::initialize(IChargingApi &api, IMapService &mapService,
                            const AssistantConfig &assistantConfig)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("新能源汽车充电服务"));
    resize(420, 760);
    setMinimumSize(360, 640);
    setStyleSheet(clientThemeStyleSheet());

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("applicationPages"));
    loginPage_ = new LoginPage(pages_);

    mainTabs_ = new QTabWidget(pages_);
    mainTabs_->setObjectName(QStringLiteral("mainNavigation"));
    mainTabs_->setTabPosition(QTabWidget::South);
    mainTabs_->setDocumentMode(true);
    mainTabs_->tabBar()->setExpanding(true);
    mainTabs_->tabBar()->setUsesScrollButtons(false);

    homePage_ = new StationBrowserPage(mainTabs_);
    orderPage_ = new OrderPage(mainTabs_);
    scanPage_ = new ScanPage(mainTabs_);

    assistantService_ = new AssistantService(assistantConfig, this);
    supportPage_ = new SupportPage(*assistantService_, mainTabs_);

    mainTabs_->addTab(homePage_, QStringLiteral("充电"));
    mainTabs_->addTab(orderPage_, QStringLiteral("订单"));
    mainTabs_->addTab(scanPage_, QStringLiteral("扫一扫"));
    mainTabs_->addTab(supportPage_,
                      QStringLiteral("客服助理"));
    profilePage_ = new ProfilePage(mainTabs_);
    mainTabs_->addTab(profilePage_, QStringLiteral("我的"));

    pages_->addWidget(loginPage_);
    pages_->addWidget(mainTabs_);
    pages_->setCurrentWidget(loginPage_);
    setCentralWidget(pages_);

    loginController_ = new LoginController(*loginPage_, api, this);
    avatarStorage_ = std::make_unique<AvatarStorage>();
    profileController_ =
        new ProfileController(*profilePage_, api, *avatarStorage_, this);
    stationBrowserController_ =
        new StationBrowserController(*homePage_, api, this);
    mapController_ = new MapController(*homePage_, mapService, this);
    orderController_ = new OrderController(*orderPage_, api, this);
    scanController_ = new ScanController(*scanPage_, api, this);
    connect(loginController_,
            &LoginController::loginSucceeded,
            this,
            &MainWindow::showAuthenticatedHome);
    connect(mainTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        QWidget *selectedPage = mainTabs_->widget(index);
        if (selectedPage != orderPage_) {
            orderController_->leavePage();
        }
        if (selectedPage == homePage_) {
            stationBrowserController_->refreshStations();
        } else if (selectedPage == orderPage_) {
            orderController_->refreshOrders();
        } else if (selectedPage == profilePage_) {
            profileController_->refreshProfile();
        }
    });
    connect(profileController_, &ProfileController::loggedOut, this, [this]() {
        showLoginPage();
    });
    connect(profileController_,
            &ProfileController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
    connect(profileController_,
            &ProfileController::profileChanged,
            this,
            [this](const protocol::UserDto &user) {
                homePage_->setGreetingNickname(user.nickname);
            });
    connect(profileController_,
            &ProfileController::pendingOrderSettled,
            this,
            [this](const PaymentPayload &result) {
                showAutomaticSettlementNotice(this, result);
            });
    connect(stationBrowserController_,
            &StationBrowserController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
    connect(mapController_, &MapController::locationChanged,
            stationBrowserController_, &StationBrowserController::refreshStations);
    const auto openOrderStationNavigation =
        [this](const protocol::StationDto &station) {
            homePage_->showListPage();
            mainTabs_->setCurrentWidget(homePage_);
            mapController_->openNavigation(station);
        };
    connect(stationBrowserController_,
            &StationBrowserController::navigationReady,
            this,
            openOrderStationNavigation);
    connect(stationBrowserController_,
            &StationBrowserController::currentOrderRequiresAttention,
            this, [this](protocol::OrderStatus status) {
                if (status == protocol::OrderStatus::PendingPayment) {
                    showPendingPaymentNotice(this);
                    mainTabs_->setCurrentWidget(orderPage_);
                } else {
                    mainTabs_->setCurrentWidget(homePage_);
                }
            });
    connect(orderController_,
            &OrderController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
    connect(orderController_, &OrderController::rechargeRequested,
            this, [this]() { mainTabs_->setCurrentWidget(profilePage_); });
    connect(orderController_,
            &OrderController::navigationReady,
            this,
            openOrderStationNavigation);
    connect(orderController_,
            &OrderController::chargingStopped,
            stationBrowserController_,
            &StationBrowserController::synchronizeChargingStop);
    const auto showChargingStopResult =
        [this](const ChargingStopPayload &result) {
            showChargingStoppedNotice(this, result);
            if (!result.paid) {
                mainTabs_->setCurrentWidget(profilePage_);
            }
        };
    connect(orderController_,
            &OrderController::chargingStopped,
            this,
            showChargingStopResult);
    connect(stationBrowserController_,
            &StationBrowserController::chargingStopped,
            this,
            showChargingStopResult);
    const auto openReservationScan = [this](const QString &pileCode) {
        scanPage_->preparePileCode(pileCode);
        mainTabs_->setCurrentWidget(scanPage_);
    };
    connect(homePage_, &StationBrowserPage::reservationScanRequested,
            this, openReservationScan);
    connect(orderPage_, &OrderPage::reservationScanRequested,
            this, openReservationScan);
    connect(scanController_,
            &ScanController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
    connect(scanController_, &ScanController::chargingStarted,
            this, [this](const protocol::OrderDto &order) {
                showChargingStartedNotice(this, order);
                mainTabs_->setCurrentWidget(homePage_);
                homePage_->showListMessage(QStringLiteral("充电已开始"));
                stationBrowserController_->refreshStations();
            });
    connect(scanController_, &ScanController::currentOrderRequiresAttention,
            this, [this](protocol::OrderStatus status) {
                if (status == protocol::OrderStatus::PendingPayment) {
                    showPendingPaymentNotice(this);
                    mainTabs_->setCurrentWidget(orderPage_);
                } else {
                    mainTabs_->setCurrentWidget(homePage_);
                }
            });
}

MainWindow::~MainWindow() = default;

void MainWindow::showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser)
{
    supportPage_->resetConversation();
    homePage_->setGreeting(user.nickname, isNewUser);
    profileController_->setInitialUser(user);
    mainTabs_->setCurrentWidget(homePage_);
    pages_->setCurrentWidget(mainTabs_);
    stationBrowserController_->refreshStations();
}

void MainWindow::showLoginPage(const QString &message)
{
    supportPage_->resetConversation();
    loginPage_->setLoading(false);
    loginPage_->setErrorMessage(message);
    stationBrowserController_->reset();
    orderController_->reset();
    scanController_->reset();
    profileController_->reset();
    mapController_->reset();
    pages_->setCurrentWidget(loginPage_);
}

}  // namespace charging::client
