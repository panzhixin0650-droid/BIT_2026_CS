// 客户端主窗口：装配登录页、底部导航各页面与对应控制器
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

// 底部导航条的尺寸与阴影常量
constexpr int navigationItemSize = 68;
constexpr int navigationPadding = 12;
constexpr int navigationBottomGap = 28;
constexpr int navigationHeight = navigationItemSize + 2 * navigationPadding;
constexpr int navigationShadowRadius = 13;
constexpr int navigationShadowOffset = 3;

// 自绘底部导航标签栏，等宽点击区、选中项为方块
class NavigationTabBar final : public QTabBar {
public:
    explicit NavigationTabBar(QWidget *parent = nullptr)
        : QTabBar(parent)
    {
        setElideMode(Qt::ElideNone);
        setFixedHeight(navigationHeight + navigationBottomGap);
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
    // 逐个标签绘制图标与文字，按状态选择配色
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

// 带圆角容器和阴影的导航标签页容器
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

    // 让地图页可覆盖导航区域，进入沉浸式全屏
    void overlayMapPage(StationBrowserPage *page)
    {
        mapPage_ = page;
        contentStack_ = findChild<QStackedWidget *>(QStringLiteral("qt_tabwidget_stackedwidget"), Qt::FindDirectChildrenOnly);
        if (!contentStack_) return;
        contentStack_->installEventFilter(this);
        connect(this, &QTabWidget::currentChanged, this, [this] { updateMapOverlay(); });
        connect(page, &StationBrowserPage::mapFullscreenChanged, this, [this] { updateMapOverlay(); });
        auto *browserPages = page->findChild<QStackedWidget *>(QStringLiteral("stationBrowserPages"));
        connect(browserPages, &QStackedWidget::currentChanged, this, [this] { updateMapOverlay(); });
        QTimer::singleShot(0, this, [this] { updateMapOverlay(); });
    }

protected:
    void initStyleOption(QStyleOptionTabWidgetFrame *option) const override
    {
        QTabWidget::initStyleOption(option);
        const int outerMargin = qBound(12, (width() - 240) / 10, 22);
        option->tabBarSize.setWidth(
            qMax(0, width() - 2 * (outerMargin + navigationPadding)));
    }

    // 手工绘制导航条下方阴影，避免整片重绘
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

    // 跟随标签栏移动或缩放，同步阴影容器位置
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
        if ((watched == contentStack_ || watched == tabBar())
            && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            // Correct the page bounds before the first paint, avoiding a costly
            // intermediate map render at the tab widget's reserved-footer height.
            if (watched == contentStack_) updateMapOverlay();
            QTimer::singleShot(0, this, [this] { updateMapOverlay(); });
        }
        return QTabWidget::eventFilter(watched, event);
    }

private:
    // 按当前页决定是否隐藏导航，并调整页面显示区域
    void updateMapOverlay()
    {
        if (!contentStack_ || !mapPage_ || updatingOverlay_) return;
        updatingOverlay_ = true;
        auto *browserPages = mapPage_->findChild<QStackedWidget *>(QStringLiteral("stationBrowserPages"));
        const bool fullMap = currentWidget() == mapPage_ && browserPages->currentWidget()
            && browserPages->currentWidget()->objectName() == QStringLiteral("stationListPage");
        const bool hideNavigation = fullMap && mapPage_->isMapFullscreen();
        if (tabBar()->isHidden() != hideNavigation) tabBar()->setVisible(!hideNavigation);
        navigationContainer_->setVisible(!hideNavigation);
        QStyleOptionTabWidgetFrame option;
        initStyleOption(&option);
        const QRect normalContents = style()->subElementRect(QStyle::SE_TabWidgetTabContents, &option, this);
        QRect bar = style()->subElementRect(QStyle::SE_TabWidgetTabBar, &option, this);
        // One shared height and bottom gap on every tab, including the map.
        // The gap also keeps SDK attribution visible below the floating pill.
        mapPage_->setBottomNavigationInset(fullMap && !hideNavigation ? navigationHeight + navigationBottomGap : 0);
        const QRect contents = fullMap ? rect() : normalContents;
        if (contentStack_->geometry() != contents) contentStack_->setGeometry(contents);
        if (tabBar()->geometry() != bar) tabBar()->setGeometry(bar);
        // Keep the WebEngine canvas in place when entering/exiting immersion.
        // Repeated raise/update calls repaint the entire native/GL overlap.
        if (!hideNavigation) {
            navigationContainer_->raise();
            tabBar()->raise();
        }
        updatingOverlay_ = false;
    }
    StationBrowserPage *mapPage_ = nullptr;
    QStackedWidget *contentStack_ = nullptr;
    bool updatingOverlay_ = false;
    QFrame *navigationContainer_ = nullptr;
};

// 提示存在待支付订单，确认后跳转订单页
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

// 充电结束提示：金额结清或欠费需充值
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

// 待支付订单被自动结算后的提示
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

// 充值成功并显示当前余额
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

// 充值成功但余额仍不足或需核对订单时的提示
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

// 未注入地图服务时默认使用Mock地图
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

// 统一初始化：建页面、连信号、创建各控制器
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

    // 首页站点浏览页并配置地图脚本地址
    homePage_ = new StationBrowserPage(mainTabs_);
    homePage_->configureHomeMap(mapService.mapScriptUrl());
    static_cast<NavigationTabWidget *>(mainTabs_)->overlayMapPage(homePage_);
    orderPage_ = new OrderPage(mainTabs_);
    chargingPage_ = new ChargingPage(mainTabs_);
    scanPage_ = new ScanPage(mainTabs_);

    // 普通AI助理页与后续按需创建的模拟客服页
    assistantService_ = new AssistantService(assistantConfig, this);
    supportPage_ = new SupportPage(*assistantService_, mainTabs_);
    // 懒加载客服台/报修/工单页，各自使用独立助理实例
    const auto ensureDeskPage = [this, &api, assistantConfig](SupportDeskPage *&page, const QString &name) {
        if (page) return;
        const auto config = assistantConfig.forSupportDesk();
        auto *desk = new AssistantService(config, this, nullptr, AssistantPurpose::SupportDesk);
        auto *summary = new AssistantService(config, this, nullptr, AssistantPurpose::TicketSummary);
        page = new SupportDeskPage(api, *desk, *summary, pages_);
        page->setObjectName(name);
        pages_->addWidget(page);
        connect(page, &SupportDeskPage::backRequested, this, [this] { pages_->setCurrentWidget(mainTabs_); });
        connect(page, &SupportDeskPage::invalidSession, this, &MainWindow::showLoginPage);
        connect(page, &SupportDeskPage::ticketObserved, this, [this](const protocol::SupportTicketDto &ticket) {
            if (repairPage_) repairPage_->confirmSubmission(ticket);
        });
    };
    connect(supportPage_, &SupportPage::supportDeskRequested, this, [this, ensureDeskPage] {
        ensureDeskPage(supportDesk_, "supportDeskPage");
        supportDesk_->openDesk(supportPage_->recentHistory());
        pages_->setCurrentWidget(supportDesk_);
    });
    connect(chargingPage_, &ChargingPage::repairRequested, this, [this, ensureDeskPage](const QString &pileCode) {
        ensureDeskPage(repairPage_, "repairPage");
        repairPage_->openRepair(pileCode);
        pages_->setCurrentWidget(repairPage_);
    });

    // 依次注册底部导航的各个功能页
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
    // 我的分区内含个人主页与订单容器两层
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
    connect(profilePage_, &ProfilePage::repairRequested, this, [this, ensureDeskPage] {
        ensureDeskPage(repairPage_, "repairPage");
        repairPage_->openRepair({});
        pages_->setCurrentWidget(repairPage_);
    });
    connect(profilePage_, &ProfilePage::ticketsRequested, this, [this, ensureDeskPage] {
        ensureDeskPage(ticketsPage_, "ticketsPage");
        ticketsPage_->openTickets();
        pages_->setCurrentWidget(ticketsPage_);
    });
    mainTabs_->addTab(profileSection_,
                      clientNavigationIcon(NavigationIcon::Profile),
                      QStringLiteral("我的"));

    pages_->addWidget(loginPage_);
    pages_->addWidget(mainTabs_);
    pages_->setCurrentWidget(loginPage_);
    // 外壳布局：顶部品牌栏加下方页面区
    auto *shell = new QWidget(this);
    shell->setObjectName("applicationShell");
    auto *shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    auto *header = new QFrame(shell);
    header->setObjectName("applicationHeader");
    header->setFixedHeight(64);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 6, 16, 6);
    auto *brandIcon = new QLabel(QStringLiteral("ϟ"), header);
    brandIcon->setObjectName("brandIcon");
    brandIcon->setFixedSize(40,40);
    brandIcon->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(brandIcon);
    auto *brand = new QLabel(QStringLiteral("悦充\n充电服务"), header);
    brand->setObjectName("brandName");
    headerLayout->addWidget(brand, 1);
    headerRefresh_ = new QPushButton(QStringLiteral("↻"), header);
    headerRefresh_->setObjectName("headerRefreshButton");
    headerRefresh_->setFixedSize(44,44);
    headerRefresh_->setAccessibleName(QStringLiteral("刷新当前页面数据"));
    headerRefresh_->setToolTip(headerRefresh_->accessibleName());
    headerLayout->addWidget(headerRefresh_);
    headerAccount_ = new QPushButton(QStringLiteral("未登录"), header);
    headerAccount_->setObjectName("headerAccountButton");
    headerAccount_->setFixedSize(110,48);
    headerLayout->addWidget(headerAccount_);
    shellLayout->addWidget(header);
    shellLayout->addWidget(pages_, 1);
    setCentralWidget(shell);
    // 点账号按钮回到我的页并展开个人详情
    connect(headerAccount_, &QPushButton::clicked, this, [this] {
        if (!authenticated_) return;
        pages_->setCurrentWidget(mainTabs_);
        showProfile();
        profilePage_->openDetails();
    });
    // 仅登录且停留在数据页时才允许点刷新
    const auto updateRefresh = [this] {
        const bool mainData = pages_->currentWidget() == mainTabs_
            && (mainTabs_->currentWidget() == homePage_ || mainTabs_->currentWidget() == chargingPage_
                || mainTabs_->currentWidget() == profileSection_);
        headerRefresh_->setEnabled(authenticated_ && (mainData || (ticketsPage_ && pages_->currentWidget() == ticketsPage_)));
    };
    connect(pages_, &QStackedWidget::currentChanged, this, updateRefresh);
    connect(mainTabs_, &QTabWidget::currentChanged, this, updateRefresh);
    // 刷新按钮按当前页面分发到对应控制器
    connect(headerRefresh_, &QPushButton::clicked, this, [this, updateRefresh] {
        if (!authenticated_) return;
        if (ticketsPage_ && pages_->currentWidget() == ticketsPage_) ticketsPage_->refreshCurrentPage();
        else if (mainTabs_->currentWidget() == homePage_) stationBrowserController_->refreshStations();
        else if (mainTabs_->currentWidget() == chargingPage_) chargingController_->refresh();
        else if (profileSection_->currentWidget() == orderContainer_) orderController_->refreshOrders();
        else profileController_->refreshProfile();
        headerRefresh_->setEnabled(false);
        QTimer::singleShot(600, this, updateRefresh);
    });
    updateRefresh();

    // 创建登录、个人、站点、订单、充电各控制器
    loginController_ = new LoginController(*loginPage_, api, this);
    avatarStorage_ = std::make_unique<AvatarStorage>();
    profileController_ =
        new ProfileController(*profilePage_, api, *avatarStorage_, this);
    // 选头像时打开本地演示相册页
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
                emit profilePage_->avatarSelected(path);
            });
            connect(avatarAlbum_, &PhotoAlbumPage::imageSelectedImage, this, [this](const QImage &image) {
                if (pages_->currentWidget() != avatarAlbum_) return;
                pages_->setCurrentWidget(mainTabs_);
                emit profilePage_->avatarImageSelected(image);
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
    // 切换标签时刷新目标页数据并离开订单页
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
            profilePage_->showOverview();
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
                updateAccountHeader(user);
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
    // 个人页自动结算待支付订单后提示并同步站点
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
    // 定位变化后重新拉取站点列表
    connect(stationBrowserController_,
            &StationBrowserController::authenticationRequired,
            this,
            &MainWindow::showLoginPage);
    connect(mapController_, &MapController::locationChanged,
            stationBrowserController_, &StationBrowserController::refreshStations);
    // 从订单或站点发起导航时切回首页地图
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
    // 当前订单需处理：待支付去订单页，否则去充电页
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
    // 充电结束弹提示，欠费则引导到个人页充值
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
    // 预约赴约、直接充电与扫码都进入充电页
    connect(homePage_, &StationBrowserPage::reservationScanRequested, this, &MainWindow::openCharging);
    connect(homePage_, &StationBrowserPage::directChargingRequested, this, &MainWindow::openCharging);
    connect(orderPage_, &OrderPage::reservationScanRequested, this, &MainWindow::openCharging);
    connect(scanPage_, &ScanPage::scanRequested, this, &MainWindow::openCharging);
    connect(chargingController_, &ChargingController::authenticationRequired, this, &MainWindow::showLoginPage);
    // 订单已完成或取消则清除首页当前订单卡片
    connect(chargingController_, &ChargingController::orderChanged, this, [this](const protocol::OrderDto &order) {
        if (order.status == protocol::OrderStatus::Completed || order.status == protocol::OrderStatus::Cancelled)
            homePage_->showCurrentOrder(std::nullopt);
        else homePage_->showCurrentOrder(order);
    });
    connect(chargingController_, &ChargingController::sessionStarted, stationBrowserController_, &StationBrowserController::refreshStations);
    connect(chargingController_, &ChargingController::reservationReleased, stationBrowserController_, &StationBrowserController::refreshStations);
    connect(chargingController_, &ChargingController::reservationReleased, orderController_, &OrderController::refreshOrders);
    // 会话结束后刷新钱包余额与站点空闲情况
    connect(chargingController_, &ChargingController::sessionFinished, this, [this] {
        profileController_->refreshProfile();
        stationBrowserController_->refreshStations();
    });
    connect(chargingPage_, &ChargingPage::ordersRequested, this, &MainWindow::openOrders);
    connect(chargingPage_, &ChargingPage::rechargeRequested, this, &MainWindow::showProfile);
    connect(chargingPage_, &ChargingPage::homeRequested, this, [this]{mainTabs_->setCurrentWidget(homePage_);});
    connect(chargingPage_, &ChargingPage::scanRequested, this, [this]{mainTabs_->setCurrentWidget(scanPage_);});
    connect(scanPage_, &ScanPage::cancelled, this, [this]{mainTabs_->setCurrentWidget(homePage_);});

    // 登录界面显示期间预热首页地图，异步不涉登录数据
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
            qMax(220, pages_->height())));
        // Navigation waits for the home's actual visible tiles, not a fixed
        // delay that downloads a second SDK while the first map is still cold.
    });
}

// 带桩号进入充电页准备下一步操作
void MainWindow::openCharging(const QString &pileCode)
{
    chargingController_->prepare(pileCode);
    mainTabs_->setCurrentWidget(chargingPage_);
}
// 进入我的分区并显示订单列表
void MainWindow::openOrders()
{
    mainTabs_->setCurrentWidget(profileSection_);
    profileSection_->setCurrentWidget(orderContainer_);
    orderController_->refreshOrders();
}
// 回到个人主页并刷新资料
void MainWindow::showProfile()
{
    mainTabs_->setCurrentWidget(profileSection_);
    profileSection_->setCurrentWidget(profilePage_);
    orderController_->leavePage();
    profileController_->refreshProfile();
}

// 析构先清理客服页会话，再销毁地图控制器
MainWindow::~MainWindow()
{
    for (auto *page : {supportDesk_, repairPage_, ticketsPage_}) {
        if (page) { page->resetSession(); delete page; }
    }
    // Child pages may emit navigationClosed while the window is being torn down.
    // Disconnect their controller before ownedMapService_ is destroyed.
    delete mapController_;
    mapController_ = nullptr;
}

// 登录成功：重置会话状态并展示首页
void MainWindow::showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser)
{
    for (auto *page : {supportDesk_, repairPage_, ticketsPage_}) if (page) page->resetSession();
    supportPage_->resetConversation();
    authenticated_ = true;
    updateAccountHeader(user);
    profilePage_->showOverview();
    homePage_->setUserId(user.userId);
    homePage_->setGreeting(user.nickname, isNewUser);
    profileController_->setInitialUser(user);
    mainTabs_->setCurrentWidget(homePage_);
    pages_->setCurrentWidget(mainTabs_);
    stationBrowserController_->refreshStations();
    chargingController_->activate();
}

// 退出或会话失效：重置各控制器并回到登录页
void MainWindow::showLoginPage(const QString &message)
{
    authenticated_ = false;
    updateAccountHeader({});
    profilePage_->showOverview();
    for (auto *page : {supportDesk_, repairPage_, ticketsPage_}) if (page) page->resetSession();
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

// 顶部账号显示昵称与脱敏手机号
void MainWindow::updateAccountHeader(const protocol::UserDto &user)
{
    if (!authenticated_) {
        headerAccount_->setText(QStringLiteral("未登录"));
        headerAccount_->setAccessibleName(QStringLiteral("尚未登录"));
        return;
    }
    const QString phone = user.phone.size() >= 7 ? user.phone.left(3) + "****" + user.phone.right(4) : user.phone;
    headerAccount_->setText(user.nickname.left(6).replace('&', "&&") + "\n" + phone);
    headerAccount_->setAccessibleName(QStringLiteral("%1，%2，查看个人详细信息").arg(user.nickname, phone));
    headerAccount_->setToolTip(headerAccount_->accessibleName());
}

}  // namespace charging::client
