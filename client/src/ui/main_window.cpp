#include "ui/main_window.h"
#include "ui/support_desk_page.h"
#include "ui/station_map_view.h"

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
#include "ui/photo_album_page.h"
#include "ui/avatar_art.h"
#include "ui/charging_controller.h"
#include "ui/charging_page.h"
#include <QPushButton>
#include "ui/scan_page.h"
#include "ui/station_browser_controller.h"
#include "ui/station_browser_page.h"
#include "assistant/assistant_service.h"
#include "ui/support_page.h"

#include <QAbstractButton>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace charging::client {

namespace {

constexpr int navigationItemSize = 68;
constexpr int navigationPadding = 12;
constexpr int navigationBottomGap = 18;
constexpr int navigationHeight = navigationItemSize + 2 * navigationPadding;
constexpr int navigationShadowRadius = 13;
constexpr int navigationShadowOffset = 3;

class NavigationTabBar final : public QTabBar {
public:
    explicit NavigationTabBar(QWidget *parent = nullptr)
        : QTabBar(parent)
    {
        setElideMode(Qt::ElideNone);
    }

    QSize tabSizeHint(int index) const override
    {
        QSize size = QTabBar::tabSizeHint(index);
        size.setWidth(64);
        size.setHeight(navigationHeight + navigationBottomGap);
        return size;
    }

    QSize minimumTabSizeHint(int index) const override
    {
        QSize size = tabSizeHint(index);
        size.setWidth(56);
        return size;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QStylePainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        for (int index = 0; index < count(); ++index) {
            QStyleOptionTab option;
            initStyleOption(&option, index);
            // Keep the full equal-width hit area; only the selected tile is square.
            const int side = qMin(navigationItemSize, option.rect.width() - 4);
            const int centerX = option.rect.center().x();
            option.rect = QRect(centerX - side / 2,
                                (navigationHeight - side) / 2, side, side);

            const bool selected = option.state.testFlag(QStyle::State_Selected);
            const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
            const bool enabled = option.state.testFlag(QStyle::State_Enabled);
            if (selected) {
                painter.drawControl(QStyle::CE_TabBarTabShape, option);
            }
            const QRect content = option.rect.adjusted(2, 0, -2, 0);
            const QSize drawnIconSize(27, 27);
            QFont labelFont = painter.font();
            labelFont.setPointSize(9);
            labelFont.setWeight(selected ? QFont::DemiBold : QFont::Medium);
            painter.setFont(labelFont);
            const int labelHeight = painter.fontMetrics().height();
            constexpr int iconTextGap = 4;
            const int groupHeight =
                drawnIconSize.height() + iconTextGap + labelHeight;
            const int groupTop =
                content.top() + (content.height() - groupHeight) / 2;
            const QRect iconRect(content.center().x() - drawnIconSize.width() / 2,
                                 groupTop,
                                 drawnIconSize.width(),
                                 drawnIconSize.height());
            const QIcon::Mode iconMode = !enabled ? QIcon::Disabled
                : selected ? QIcon::Selected
                           : hovered ? QIcon::Active : QIcon::Normal;
            option.icon.paint(&painter,
                              iconRect,
                              Qt::AlignCenter,
                              iconMode,
                              QIcon::Off);

            const QRect textRect(content.left(),
                                 iconRect.bottom() + iconTextGap,
                                 content.width(),
                                 labelHeight + 1);
            const QColor textColor = !enabled ? QColor(QStringLiteral("#96a18e"))
                : selected ? QColor(QStringLiteral("#245c45"))
                           : hovered ? QColor(QStringLiteral("#245c45"))
                                     : QColor(QStringLiteral("#697969"));
            painter.setPen(textColor);
            painter.drawText(textRect,
                             Qt::AlignHCenter | Qt::AlignTop,
                             option.text);
        }
    }
};

class NavigationTabWidget final : public QTabWidget {
public:
    explicit NavigationTabWidget(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new NavigationTabBar(this));
        navigationContainer_ = new QFrame(this);
        navigationContainer_->setObjectName(QStringLiteral("navigationContainer"));
        navigationContainer_->setAttribute(Qt::WA_TransparentForMouseEvents);
        navigationContainer_->stackUnder(tabBar());

        tabBar()->installEventFilter(this);
    }

protected:
    void initStyleOption(QStyleOptionTabWidgetFrame *option) const override
    {
        QTabWidget::initStyleOption(option);
        const int outerMargin = qBound(12, (width() - 240) / 10, 22);
        option->tabBarSize.setWidth(
            qMax(0, width() - 2 * (outerMargin + navigationPadding)));
    }

    void paintEvent(QPaintEvent *event) override
    {
        QTabWidget::paintEvent(event);
        if (!tabBar()->isVisible()) return;

        // Paint the shadow below both children, respecting the original dirty
        // region. A live effect on the sibling frame expands disjoint tab
        // updates to their bounding rectangle and erases the clean tabs between.
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(32, 61, 48, 2));
        const QRectF frame = QRectF(navigationContainer_->geometry())
                                 .translated(0, navigationShadowOffset);
        for (int spread = navigationShadowRadius; spread > 0; --spread) {
            const qreal radius = navigationHeight / 2.0 + spread;
            painter.drawRoundedRect(frame.adjusted(-spread, -spread, spread, spread),
                                    radius, radius);
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == tabBar()
            && (event->type() == QEvent::Move
                || event->type() == QEvent::Resize
                || event->type() == QEvent::Show)) {
            const QRect oldFrame = navigationContainer_->geometry();
            navigationContainer_->setGeometry(tabBar()->geometry().adjusted(
                -navigationPadding, 0, navigationPadding, -navigationBottomGap));
            const int shadowMargin = navigationShadowRadius + navigationShadowOffset;
            update(oldFrame.united(navigationContainer_->geometry()).adjusted(
                -shadowMargin, -shadowMargin, shadowMargin, shadowMargin));
        }
        return QTabWidget::eventFilter(watched, event);
    }

private:
    QFrame *navigationContainer_ = nullptr;
};

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

void showRechargeSuccessNotice(QWidget *parent, qint64 balanceCents)
{
    QMessageBox notice(
        QMessageBox::Information,
        QStringLiteral("充值成功"),
        QStringLiteral("充值已到账，当前余额 ¥%1。")
            .arg(balanceCents / 100.0, 0, 'f', 2),
        QMessageBox::Ok,
        parent);
    notice.setObjectName(QStringLiteral("rechargeSuccessDialog"));
    notice.button(QMessageBox::Ok)->setText(QStringLiteral("知道了"));
    notice.exec();
}

void showRechargeAttentionNotice(QWidget *parent,
                                 qint64 balanceCents,
                                 const QString &message,
                                 bool insufficientBalance)
{
    QMessageBox notice(
        QMessageBox::Warning,
        insufficientBalance ? QStringLiteral("充值成功，余额仍不足")
                            : QStringLiteral("充值成功，请核对订单"),
        QStringLiteral("%1\n当前余额 ¥%2。")
            .arg(message)
            .arg(balanceCents / 100.0, 0, 'f', 2),
        QMessageBox::Ok,
        parent);
    notice.setObjectName(insufficientBalance
                             ? QStringLiteral("rechargeInsufficientDialog")
                             : QStringLiteral("rechargeAttentionDialog"));
    notice.button(QMessageBox::Ok)->setText(
        insufficientBalance ? QStringLiteral("继续充值")
                            : QStringLiteral("知道了"));
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
    resize(480, 860);
    setMinimumSize(360, 640);
    setStyleSheet(clientThemeStyleSheet());

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("applicationPages"));
    loginPage_ = new LoginPage(pages_);

    mainTabs_ = new NavigationTabWidget(pages_);
    mainTabs_->setObjectName(QStringLiteral("mainNavigation"));
    mainTabs_->setTabPosition(QTabWidget::South);
    mainTabs_->setDocumentMode(true);
    mainTabs_->setIconSize(QSize(27, 27));
    mainTabs_->tabBar()->setExpanding(true);
    mainTabs_->tabBar()->setUsesScrollButtons(false);

    homePage_ = new StationBrowserPage(mainTabs_);
    homePage_->configureHomeMap(mapService.mapScriptUrl());
    orderPage_ = new OrderPage(mainTabs_);
    chargingPage_ = new ChargingPage(mainTabs_);
    scanPage_ = new ScanPage(mainTabs_);

    assistantService_ = new AssistantService(assistantConfig, this);
    supportPage_ = new SupportPage(*assistantService_, mainTabs_);
    const auto ensureSupportDesk = [this, &api, assistantConfig] {
        if (!supportDesk_) {
            const auto config = assistantConfig.forSupportDesk();
            auto *desk = new AssistantService(config, this, nullptr, AssistantPurpose::SupportDesk);
            auto *summary = new AssistantService(config, this, nullptr, AssistantPurpose::TicketSummary);
            supportDesk_ = new SupportDeskPage(api, *desk, *summary, pages_);
            pages_->addWidget(supportDesk_);
            connect(supportDesk_, &SupportDeskPage::backRequested, this, [this] {
                pages_->setCurrentWidget(mainTabs_);
            });
            connect(supportDesk_, &SupportDeskPage::invalidSession, this, &MainWindow::showLoginPage);
        }
    };
    connect(supportPage_, &SupportPage::supportDeskRequested, this, [this, ensureSupportDesk] {
        ensureSupportDesk();
        supportDesk_->openDesk(supportPage_->recentHistory());
        pages_->setCurrentWidget(supportDesk_);
    });
    connect(chargingPage_, &ChargingPage::repairRequested, this, [this, ensureSupportDesk](const QString &pileCode) {
        ensureSupportDesk();
        supportDesk_->openRepair(pileCode);
        pages_->setCurrentWidget(supportDesk_);
    });

    mainTabs_->addTab(homePage_,
                      clientNavigationIcon(NavigationIcon::Route),
                      QStringLiteral("首页"));
    mainTabs_->addTab(chargingPage_,
                      clientNavigationIcon(NavigationIcon::Charging),
                      QStringLiteral("充电"));
    mainTabs_->addTab(scanPage_,
                      clientNavigationIcon(NavigationIcon::Scan),
                      QStringLiteral("扫一扫"));
    mainTabs_->addTab(supportPage_,
                      clientNavigationIcon(NavigationIcon::Support),
                      QStringLiteral("客服助理"));
    profileSection_ = new QStackedWidget(mainTabs_);
    profileSection_->setObjectName("profileSection");
    profilePage_ = new ProfilePage(profileSection_);
    orderContainer_ = new QWidget(profileSection_);
    auto *orderLayout = new QVBoxLayout(orderContainer_);
    orderLayout->setContentsMargins(0,0,0,0);
    auto *orderBack = new QPushButton(QStringLiteral("‹ 返回我的"), orderContainer_);
    orderBack->setObjectName("ordersBackButton");
    connect(orderBack, &QPushButton::clicked, this, &MainWindow::showProfile);
    orderLayout->addWidget(orderBack); orderLayout->addWidget(orderPage_);
    profileSection_->addWidget(profilePage_); profileSection_->addWidget(orderContainer_);
    connect(profilePage_, &ProfilePage::ordersRequested, this, &MainWindow::openOrders);
    mainTabs_->addTab(profileSection_,
                      clientNavigationIcon(NavigationIcon::Profile),
                      QStringLiteral("我的"));

    pages_->addWidget(loginPage_);
    pages_->addWidget(mainTabs_);
    pages_->setCurrentWidget(loginPage_);
    setCentralWidget(pages_);

    loginController_ = new LoginController(*loginPage_, api, this);
    avatarStorage_ = std::make_unique<AvatarStorage>();
    profileController_ =
        new ProfileController(*profilePage_, api, *avatarStorage_, this);
    connect(profilePage_, &ProfilePage::avatarSelectionRequested, this, [this] {
        if (!avatarAlbum_) {
            avatarAlbum_ = new PhotoAlbumPage(pages_, {}, PhotoAlbumPage::Purpose::Avatar);
            pages_->addWidget(avatarAlbum_);
            connect(avatarAlbum_, &PhotoAlbumPage::backRequested, this, [this] {
                if (pages_->currentWidget() == avatarAlbum_)
                    pages_->setCurrentWidget(mainTabs_);
            });
            connect(avatarAlbum_, &PhotoAlbumPage::imageSelected, this, [this](const QString &path) {
                if (pages_->currentWidget() != avatarAlbum_) return;
                pages_->setCurrentWidget(mainTabs_);
                emit profilePage_->avatarSelected(QImage(path));
            });
            connect(avatarAlbum_, &PhotoAlbumPage::imageSelectedImage, this, [this](const QImage &image) {
                if (pages_->currentWidget() != avatarAlbum_) return;
                pages_->setCurrentWidget(mainTabs_);
                emit profilePage_->avatarSelected(image);
            });
        }
        avatarAlbum_->reload();
        pages_->setCurrentWidget(avatarAlbum_);
    });
    stationBrowserController_ =
        new StationBrowserController(*homePage_, api, this);
    mapController_ = new MapController(*homePage_, mapService, this);
    orderController_ = new OrderController(*orderPage_, api, this);
    chargingController_ = new ChargingController(*chargingPage_, api, this);
    connect(loginController_,
            &LoginController::loginSucceeded,
            this,
            &MainWindow::showAuthenticatedHome);
    connect(mainTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        QWidget *selectedPage = mainTabs_->widget(index);
        if (selectedPage != profileSection_) {
            orderController_->leavePage();
        }
        if (selectedPage == homePage_) {
            stationBrowserController_->refreshStations();
        } else if (selectedPage == chargingPage_) {
            chargingController_->refresh();
        } else if (selectedPage == profileSection_) {
            profileSection_->setCurrentWidget(profilePage_);
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
            &ProfileController::rechargeSucceeded,
            this,
            [this](qint64 balanceCents) {
                showRechargeSuccessNotice(this, balanceCents);
            });
    connect(profileController_,
            &ProfileController::rechargeNeedsAttention,
            this,
            [this](qint64 balanceCents,
                   const QString &message,
                   bool insufficientBalance) {
                showRechargeAttentionNotice(
                    this, balanceCents, message, insufficientBalance);
            });
    connect(profileController_,
            &ProfileController::pendingOrderSettled,
            this,
            [this](const PaymentPayload &result) {
                showAutomaticSettlementNotice(this, result);
            });
    connect(profileController_,
            &ProfileController::pendingOrderSettled,
            stationBrowserController_,
            &StationBrowserController::synchronizePendingOrderSettlement);
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
                    openOrders();
                } else {
                    mainTabs_->setCurrentWidget(chargingPage_);
                    chargingController_->refresh();
                }
            });
    connect(orderController_,
            &OrderController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
    connect(orderController_, &OrderController::rechargeRequested,
            this, [this]() { showProfile(); });
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
                showProfile();
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
    connect(homePage_, &StationBrowserPage::reservationScanRequested, this, &MainWindow::openCharging);
    connect(homePage_, &StationBrowserPage::directChargingRequested, this, &MainWindow::openCharging);
    connect(orderPage_, &OrderPage::reservationScanRequested, this, &MainWindow::openCharging);
    connect(scanPage_, &ScanPage::scanRequested, this, &MainWindow::openCharging);
    connect(chargingController_, &ChargingController::authenticationRequired, this, &MainWindow::showLoginPage);
    connect(chargingController_, &ChargingController::orderChanged, this, [this](const protocol::OrderDto &order) {
        if (order.status == protocol::OrderStatus::Completed || order.status == protocol::OrderStatus::Cancelled)
            homePage_->showCurrentOrder(std::nullopt);
        else homePage_->showCurrentOrder(order);
    });
    connect(chargingController_, &ChargingController::sessionStarted, stationBrowserController_, &StationBrowserController::refreshStations);
    connect(chargingController_, &ChargingController::sessionFinished, this, [this] {
        profileController_->refreshProfile();
        stationBrowserController_->refreshStations();
    });
    connect(chargingPage_, &ChargingPage::ordersRequested, this, &MainWindow::openOrders);
    connect(chargingPage_, &ChargingPage::rechargeRequested, this, &MainWindow::showProfile);
    connect(chargingPage_, &ChargingPage::homeRequested, this, [this]{mainTabs_->setCurrentWidget(homePage_);});
    connect(chargingPage_, &ChargingPage::scanRequested, this, [this]{mainTabs_->setCurrentWidget(scanPage_);});
    connect(scanPage_, &ScanPage::cancelled, this, [this]{mainTabs_->setCurrentWidget(homePage_);});

    const QUrl mapScriptUrl = mapService.mapScriptUrl();
    // Let login paint first. During account entry, prepare the *home* canvas,
    // including SDK parsing / map initialization / default-area tiles. Nothing
    // authenticated is requested until showAuthenticatedHome(). Widgets stay
    // on the GUI thread; WebEngine downloads and readiness are asynchronous.
    if (!mapScriptUrl.isEmpty()) {
        connect(homePage_->findChild<StationMapView *>(), &StationMapView::mapReady,
            this, [this, mapScriptUrl] { homePage_->preloadMap(mapScriptUrl); }, Qt::SingleShotConnection);
    }
    QTimer::singleShot(0, this, [this]() {
        homePage_->prepareHomeMap(QSize(pages_->width(),
            qMax(220, pages_->height() - navigationHeight - navigationBottomGap)));
        // Navigation waits for the home's actual visible tiles, not a fixed
        // delay that downloads a second SDK while the first map is still cold.
    });
}

void MainWindow::openCharging(const QString &pileCode)
{
    chargingController_->prepare(pileCode);
    mainTabs_->setCurrentWidget(chargingPage_);
}
void MainWindow::openOrders()
{
    mainTabs_->setCurrentWidget(profileSection_);
    profileSection_->setCurrentWidget(orderContainer_);
    orderController_->refreshOrders();
}
void MainWindow::showProfile()
{
    mainTabs_->setCurrentWidget(profileSection_);
    profileSection_->setCurrentWidget(profilePage_);
    orderController_->leavePage();
    profileController_->refreshProfile();
}

MainWindow::~MainWindow()
{
    if (supportDesk_) {
        supportDesk_->resetSession();
        delete supportDesk_;
        supportDesk_ = nullptr;
    }
    // Child pages may emit navigationClosed while the window is being torn down.
    // Disconnect their controller before ownedMapService_ is destroyed.
    delete mapController_;
    mapController_ = nullptr;
}

void MainWindow::showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser)
{
    if (supportDesk_) supportDesk_->resetSession();
    supportPage_->resetConversation();
    homePage_->setGreeting(user.nickname, isNewUser);
    profileController_->setInitialUser(user);
    mainTabs_->setCurrentWidget(homePage_);
    pages_->setCurrentWidget(mainTabs_);
    stationBrowserController_->refreshStations();
    chargingController_->activate();
}

void MainWindow::showLoginPage(const QString &message)
{
    if (supportDesk_) supportDesk_->resetSession();
    supportPage_->resetConversation();
    loginPage_->setLoading(false);
    loginPage_->setErrorMessage(message);
    stationBrowserController_->reset();
    orderController_->reset();
    chargingController_->reset();
    scanPage_->reset();
    profileController_->reset();
    mapController_->reset();
    pages_->setCurrentWidget(loginPage_);
}

}  // namespace charging::client
