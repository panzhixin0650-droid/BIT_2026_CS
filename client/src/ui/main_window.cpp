#include "ui/main_window.h"

#include "api/i_charging_api.h"
#include "local/avatar_storage.h"
#include "ui/login_controller.h"
#include "ui/login_page.h"
#include "ui/order_controller.h"
#include "ui/order_page.h"
#include "ui/profile_controller.h"
#include "ui/profile_page.h"
#include "ui/station_browser_controller.h"
#include "ui/station_browser_page.h"

#include <QLabel>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace charging::client {

MainWindow::MainWindow(IChargingApi &api, QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("新能源汽车充电服务"));
    resize(420, 760);
    setMinimumSize(360, 640);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("applicationPages"));
    loginPage_ = new LoginPage(pages_);

    mainTabs_ = new QTabWidget(pages_);
    mainTabs_->setObjectName(QStringLiteral("mainNavigation"));
    mainTabs_->setTabPosition(QTabWidget::South);
    mainTabs_->setDocumentMode(true);

    homePage_ = new StationBrowserPage(mainTabs_);
    orderPage_ = new OrderPage(mainTabs_);

    const auto createPlaceholderPage = [this](const QString &objectName,
                                               const QString &message) {
        auto *page = new QWidget(mainTabs_);
        page->setObjectName(objectName);
        auto *layout = new QVBoxLayout(page);
        auto *label = new QLabel(message, page);
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        layout->addWidget(label);
        return page;
    };

    mainTabs_->addTab(homePage_, QStringLiteral("充电"));
    mainTabs_->addTab(orderPage_, QStringLiteral("订单"));
    mainTabs_->addTab(createPlaceholderPage(QStringLiteral("scanPage"),
                                             QStringLiteral("扫码功能将在后续阶段接入")),
                      QStringLiteral("扫一扫"));
    mainTabs_->addTab(createPlaceholderPage(QStringLiteral("supportPage"),
                                             QStringLiteral("客服助理将在后续阶段接入")),
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
    orderController_ = new OrderController(*orderPage_, api, this);
    connect(loginController_,
            &LoginController::loginSucceeded,
            this,
            &MainWindow::showAuthenticatedHome);
    connect(mainTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (mainTabs_->widget(index) == homePage_) {
            stationBrowserController_->refreshStations();
        } else if (mainTabs_->widget(index) == orderPage_) {
            orderController_->refreshOrders();
        } else if (mainTabs_->widget(index) == profilePage_) {
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
    connect(stationBrowserController_,
            &StationBrowserController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
    connect(orderController_,
            &OrderController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
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
    homePage_->reset();
    orderPage_->reset();
    pages_->setCurrentWidget(loginPage_);
}

}  // namespace charging::client
