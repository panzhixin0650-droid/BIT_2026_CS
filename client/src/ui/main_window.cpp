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

#include <QFrame>
#include <QLabel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace charging::client {

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

void MainWindow::initialize(IChargingApi &api, IMapService &mapService)
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

    const auto createSupportPage = [this]() {
        auto *page = new QWidget(mainTabs_);
        page->setObjectName(QStringLiteral("supportPage"));
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(20, 24, 20, 20);
        layout->setSpacing(14);

        auto *heading = new QLabel(QStringLiteral("客服助理"), page);
        heading->setObjectName(QStringLiteral("supportHeading"));
        QFont headingFont = heading->font();
        headingFont.setPointSize(24);
        headingFont.setBold(true);
        heading->setFont(headingFont);

        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("supportCard"));
        card->setMaximumWidth(520);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(24, 28, 24, 28);
        cardLayout->setSpacing(12);

        auto *badge = new QLabel(QStringLiteral("功能预留"), card);
        badge->setObjectName(QStringLiteral("supportBadge"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        auto *title = new QLabel(QStringLiteral("客服助理将在后续阶段接入"), card);
        title->setObjectName(QStringLiteral("supportTitle"));
        QFont titleFont = title->font();
        titleFont.setPointSize(16);
        titleFont.setBold(true);
        title->setFont(titleFont);
        title->setAlignment(Qt::AlignCenter);
        auto *description = new QLabel(
            QStringLiteral("当前 Demo 暂不提供客服业务。充电、订单和账户功能可从底部导航继续使用。"),
            card);
        description->setObjectName(QStringLiteral("supportDescription"));
        description->setAlignment(Qt::AlignCenter);
        description->setWordWrap(true);
        description->setStyleSheet(QStringLiteral("color: #667085;"));

        cardLayout->addWidget(badge, 0, Qt::AlignHCenter);
        cardLayout->addWidget(title);
        cardLayout->addWidget(description);

        layout->addWidget(heading);
        layout->addStretch();
        layout->addWidget(card, 0, Qt::AlignHCenter);
        layout->addStretch(2);
        return page;
    };

    mainTabs_->addTab(homePage_, QStringLiteral("充电"));
    mainTabs_->addTab(orderPage_, QStringLiteral("订单"));
    mainTabs_->addTab(scanPage_, QStringLiteral("扫一扫"));
    mainTabs_->addTab(createSupportPage(),
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
            this, [this](const protocol::OrderDto &) {
                mainTabs_->setCurrentWidget(homePage_);
                homePage_->showListMessage(QStringLiteral("充电已开始"));
                stationBrowserController_->refreshStations();
            });
    connect(scanController_, &ScanController::currentOrderRequiresAttention,
            this, [this](protocol::OrderStatus status) {
                if (status == protocol::OrderStatus::PendingPayment) {
                    mainTabs_->setCurrentWidget(orderPage_);
                } else {
                    mainTabs_->setCurrentWidget(homePage_);
                }
            });
}

MainWindow::~MainWindow() = default;

void MainWindow::showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser)
{
    homePage_->setGreeting(user.nickname, isNewUser);
    profileController_->setInitialUser(user);
    mainTabs_->setCurrentWidget(homePage_);
    pages_->setCurrentWidget(mainTabs_);
    stationBrowserController_->refreshStations();
}

void MainWindow::showLoginPage(const QString &message)
{
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
