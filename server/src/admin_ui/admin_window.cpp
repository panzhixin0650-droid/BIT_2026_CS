#include "admin_window.h"

#include "admin_facade.h"
#include "revenue_chart.h"

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <utility>

namespace charging::server {
namespace {

using namespace charging::protocol;

QLabel *heading(const QString &text, QWidget *parent, const char *role = "sectionTitle")
{
    auto *label = new QLabel(text, parent);
    label->setProperty("role", role);
    return label;
}

QFrame *panel(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QWidget *metricCard(const QString &title,
                    const QString &accent,
                    QLabel **valueLabel,
                    QWidget *parent)
{
    auto *card = panel(parent);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(8);
    auto *bar = new QFrame(card);
    bar->setFixedSize(36, 4);
    bar->setStyleSheet(QStringLiteral("background:%1;border-radius:2px;").arg(accent));
    layout->addWidget(bar, 0, Qt::AlignLeft);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setProperty("role", "muted");
    layout->addWidget(titleLabel);
    *valueLabel = new QLabel(QStringLiteral("--"), card);
    (*valueLabel)->setProperty("role", "metric");
    layout->addWidget(*valueLabel);
    return card;
}

QTableWidgetItem *item(const QString &text)
{
    auto *result = new QTableWidgetItem(text);
    result->setFlags(result->flags() & ~Qt::ItemIsEditable);
    return result;
}

QTableWidgetItem *numberItem(qint64 value)
{
    auto *result = item(QString::number(value));
    result->setData(Qt::UserRole, value);
    return result;
}

QString userStatusText(const QString &status)
{
    return status == QStringLiteral("ACTIVE") ? QStringLiteral("正常")
                                               : QStringLiteral("已冻结");
}

QString stationStatusText(const QString &status)
{
    return status == QStringLiteral("ACTIVE") ? QStringLiteral("启用")
                                               : QStringLiteral("停用");
}

QString pileStatusText(const QString &status)
{
    if (status == QStringLiteral("IDLE")) return QStringLiteral("空闲");
    if (status == QStringLiteral("RESERVED")) return QStringLiteral("已预约");
    if (status == QStringLiteral("CHARGING")) return QStringLiteral("充电中");
    if (status == QStringLiteral("FAULT")) return QStringLiteral("故障");
    return QStringLiteral("离线");
}

QString orderStatusText(const QString &status)
{
    if (status == QStringLiteral("RESERVED")) return QStringLiteral("已预约");
    if (status == QStringLiteral("CHARGING")) return QStringLiteral("充电中");
    if (status == QStringLiteral("PENDING_PAYMENT")) return QStringLiteral("待支付");
    if (status == QStringLiteral("COMPLETED")) return QStringLiteral("已完成");
    return QStringLiteral("已取消");
}

void colorStatus(QTableWidgetItem *tableItem, const QString &status)
{
    if (status == QStringLiteral("ACTIVE") || status == QStringLiteral("IDLE")
        || status == QStringLiteral("COMPLETED")) {
        tableItem->setForeground(QColor(QStringLiteral("#15803d")));
    } else if (status == QStringLiteral("CHARGING")
               || status == QStringLiteral("RESERVED")
               || status == QStringLiteral("PENDING_PAYMENT")) {
        tableItem->setForeground(QColor(QStringLiteral("#c26908")));
    } else {
        tableItem->setForeground(QColor(QStringLiteral("#c33838")));
    }
}

}  // namespace

AdminWindow::AdminWindow(AdminFacade *facade,
                         bool tcpListening,
                         quint16 tcpPort,
                         bool sqliteRepository,
                         QWidget *parent)
    : QMainWindow(parent)
    , facade_(facade)
    , tcpListening_(tcpListening)
    , tcpPort_(tcpPort)
    , sqliteRepository_(sqliteRepository)
{
    setWindowTitle(QStringLiteral("BIT 充电桩应用管理平台 · 管理端"));
    resize(1380, 860);
    setMinimumSize(1080, 700);
    rootStack_ = new QStackedWidget(this);
    rootStack_->addWidget(buildLoginPage());
    rootStack_->addWidget(buildApplicationPage());
    setCentralWidget(rootStack_);

    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QStackedWidget { background: #f3f6fb; }
        QWidget { color: #172033; font-family: "Noto Sans CJK SC", "Microsoft YaHei", sans-serif; font-size: 14px; }
        QFrame#panel, QFrame#loginCard { background: white; border: 1px solid #e5eaf2; border-radius: 12px; }
        QFrame#brandPanel { background: #1746a2; border: none; border-radius: 12px; }
        QLabel[role="hero"] { color: white; font-size: 28px; font-weight: 700; }
        QLabel[role="heroSub"] { color: #cdddff; font-size: 14px; }
        QLabel[role="title"] { font-size: 24px; font-weight: 700; }
        QLabel[role="sectionTitle"] { font-size: 18px; font-weight: 700; }
        QLabel[role="muted"] { color: #748096; }
        QLabel[role="metric"] { font-size: 27px; font-weight: 700; color: #172033; }
        QLabel[role="error"] { color: #c33838; }
        QLabel[role="badgeOk"] { color: #157347; background: #e7f7ed; padding: 6px 10px; border-radius: 12px; }
        QLabel[role="badgeBad"] { color: #a61b1b; background: #ffe9e9; padding: 6px 10px; border-radius: 12px; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: white; border: 1px solid #d8dfeb; border-radius: 7px; padding: 8px 10px; min-height: 20px; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #2f6fed; }
        QPushButton { background: #edf2fa; border: none; border-radius: 7px; padding: 9px 16px; color: #244168; font-weight: 600; }
        QPushButton:hover { background: #dfe8f7; }
        QPushButton[primary="true"] { color: white; background: #2f6fed; }
        QPushButton[primary="true"]:hover { background: #2459c5; }
        QPushButton[danger="true"] { color: #a61b1b; background: #ffe9e9; }
        QListWidget#navigation { background: #102a56; border: none; color: #cbd9ee; outline: none; padding: 6px; }
        QListWidget#navigation::item { border-radius: 8px; padding: 13px 14px; margin: 3px 5px; }
        QListWidget#navigation::item:selected { background: #2f6fed; color: white; }
        QListWidget#navigation::item:hover:!selected { background: #193a70; }
        QTableWidget { background: white; border: 1px solid #e5eaf2; border-radius: 10px; gridline-color: #edf0f5; selection-background-color: #e8f0ff; selection-color: #172033; }
        QHeaderView::section { background: #f6f8fc; color: #59677e; border: none; border-bottom: 1px solid #e5eaf2; padding: 10px 8px; font-weight: 600; }
    )"));
}

QWidget *AdminWindow::buildLoginPage()
{
    auto *page = new QWidget(this);
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(90, 70, 90, 70);
    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("loginCard"));
    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto *brand = new QFrame(card);
    brand->setObjectName(QStringLiteral("brandPanel"));
    brand->setMinimumWidth(410);
    auto *brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(48, 54, 48, 54);
    brandLayout->setSpacing(20);
    auto *mark = new QLabel(QStringLiteral("⚡ BIT CHARGE"), brand);
    mark->setProperty("role", "hero");
    brandLayout->addWidget(mark);
    auto *brandTitle = new QLabel(QStringLiteral("电动汽车充电桩\n应用管理平台"), brand);
    brandTitle->setProperty("role", "hero");
    brandLayout->addWidget(brandTitle);
    auto *brandSub = new QLabel(
        QStringLiteral("站点运营 · 设备监控 · 用户管理 · 营收分析"), brand);
    brandSub->setProperty("role", "heroSub");
    brandSub->setWordWrap(true);
    brandLayout->addWidget(brandSub);
    brandLayout->addStretch();
    auto *mode = new QLabel(
        QStringLiteral("当前模式  ·  %1")
            .arg(sqliteRepository_ ? QStringLiteral("SQLite 本地数据库")
                                   : QStringLiteral("内存开发数据")),
        brand);
    mode->setStyleSheet(QStringLiteral(
        "color:#d9e6ff;background:#2459a8;border-radius:8px;padding:10px 12px;"));
    brandLayout->addWidget(mode);
    cardLayout->addWidget(brand, 5);

    auto *formArea = new QWidget(card);
    auto *formLayout = new QVBoxLayout(formArea);
    formLayout->setContentsMargins(64, 70, 64, 70);
    formLayout->setSpacing(14);
    formLayout->addStretch();
    formLayout->addWidget(heading(QStringLiteral("管理员登录"), formArea, "title"));
    auto *hint = new QLabel(QStringLiteral("登录后台，查看充电业务运行情况"), formArea);
    hint->setProperty("role", "muted");
    formLayout->addWidget(hint);
    formLayout->addSpacing(18);
    formLayout->addWidget(new QLabel(QStringLiteral("账号"), formArea));
    usernameEdit_ = new QLineEdit(QStringLiteral("admin"), formArea);
    usernameEdit_->setPlaceholderText(QStringLiteral("请输入管理员账号"));
    formLayout->addWidget(usernameEdit_);
    formLayout->addWidget(new QLabel(QStringLiteral("密码"), formArea));
    passwordEdit_ = new QLineEdit(QStringLiteral("123456"), formArea);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(QStringLiteral("请输入密码"));
    formLayout->addWidget(passwordEdit_);
    loginError_ = new QLabel(formArea);
    loginError_->setProperty("role", "error");
    loginError_->setMinimumHeight(22);
    formLayout->addWidget(loginError_);
    auto *loginButton = new QPushButton(QStringLiteral("登录管理后台"), formArea);
    loginButton->setProperty("primary", true);
    loginButton->setMinimumHeight(44);
    connect(loginButton, &QPushButton::clicked, this, &AdminWindow::attemptLogin);
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &AdminWindow::attemptLogin);
    formLayout->addWidget(loginButton);
    auto *demoHint = new QLabel(QStringLiteral("课程 Demo 账号：admin / 123456"), formArea);
    demoHint->setProperty("role", "muted");
    demoHint->setAlignment(Qt::AlignCenter);
    formLayout->addWidget(demoHint);
    formLayout->addStretch();
    cardLayout->addWidget(formArea, 6);
    outer->addWidget(card);
    return page;
}

QWidget *AdminWindow::buildApplicationPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *sidebar = new QFrame(page);
    sidebar->setFixedWidth(228);
    sidebar->setStyleSheet(QStringLiteral("background:#102a56;"));
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(12, 24, 12, 18);
    auto *logo = new QLabel(QStringLiteral("⚡ 充电桩管理平台"), sidebar);
    logo->setStyleSheet(QStringLiteral("color:white;font-size:17px;font-weight:700;padding:8px;"));
    sidebarLayout->addWidget(logo);
    auto *sub = new QLabel(QStringLiteral("SERVER ADMIN"), sidebar);
    sub->setStyleSheet(QStringLiteral("color:#7fa2d8;font-size:11px;padding-left:9px;"));
    sidebarLayout->addWidget(sub);
    navigation_ = new QListWidget(sidebar);
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({QStringLiteral("▦  数据概览"), QStringLiteral("⌂  充电站管理"),
                           QStringLiteral("ϟ  充电桩管理"), QStringLiteral("♙  用户管理"),
                           QStringLiteral("≡  订单管理")});
    sidebarLayout->addWidget(navigation_, 1);
    auto *admin = new QLabel(QStringLiteral("系统管理员\nadmin"), sidebar);
    admin->setStyleSheet(QStringLiteral(
        "color:#dce7f8;background:#193a70;border-radius:8px;padding:12px;"));
    sidebarLayout->addWidget(admin);
    layout->addWidget(sidebar);

    auto *mainArea = new QWidget(page);
    auto *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(26, 18, 26, 24);
    mainLayout->setSpacing(16);
    auto *topBar = new QHBoxLayout;
    pageTitle_ = heading(QStringLiteral("数据概览"), mainArea, "title");
    topBar->addWidget(pageTitle_);
    topBar->addStretch();
    auto *connection = new QLabel(
        tcpListening_ ? QStringLiteral("● TCP 127.0.0.1:%1").arg(tcpPort_)
                      : QStringLiteral("● TCP 未启动"), mainArea);
    connection->setProperty("role", tcpListening_ ? "badgeOk" : "badgeBad");
    topBar->addWidget(connection);
    auto *repositoryBadge = new QLabel(
        sqliteRepository_ ? QStringLiteral("SQLite 本地数据库")
                          : QStringLiteral("内存开发模式"),
        mainArea);
    repositoryBadge->setStyleSheet(
        sqliteRepository_
            ? QStringLiteral("color:#157347;background:#e7f7ed;padding:6px "
                             "10px;border-radius:12px;")
            : QStringLiteral("color:#8a5900;background:#fff3cd;padding:6px "
                             "10px;border-radius:12px;"));
    topBar->addWidget(repositoryBadge);
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), mainArea);
    connect(refreshButton, &QPushButton::clicked, this, &AdminWindow::refreshAll);
    topBar->addWidget(refreshButton);
    auto *logoutButton = new QPushButton(QStringLiteral("退出"), mainArea);
    connect(logoutButton, &QPushButton::clicked, this, [this] {
        rootStack_->setCurrentIndex(0);
        passwordEdit_->clear();
    });
    topBar->addWidget(logoutButton);
    mainLayout->addLayout(topBar);
    contentStack_ = new QStackedWidget(mainArea);
    contentStack_->addWidget(buildDashboardPage());
    contentStack_->addWidget(buildStationsPage());
    contentStack_->addWidget(buildPilesPage());
    contentStack_->addWidget(buildUsersPage());
    contentStack_->addWidget(buildOrdersPage());
    mainLayout->addWidget(contentStack_, 1);
    layout->addWidget(mainArea, 1);
    connect(navigation_, &QListWidget::currentRowChanged,
            this, &AdminWindow::selectPage);
    navigation_->setCurrentRow(0);
    return page;
}

QWidget *AdminWindow::buildDashboardPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *rangeRow = new QHBoxLayout;
    auto *subTitle = new QLabel(QStringLiteral("运营核心指标"), page);
    subTitle->setProperty("role", "muted");
    rangeRow->addWidget(subTitle);
    rangeRow->addStretch();
    dashboardDays_ = new QComboBox(page);
    dashboardDays_->addItem(QStringLiteral("近 7 日"), 7);
    dashboardDays_->addItem(QStringLiteral("近 30 日"), 30);
    connect(dashboardDays_, &QComboBox::currentIndexChanged,
            this, &AdminWindow::refreshDashboard);
    rangeRow->addWidget(dashboardDays_);
    layout->addLayout(rangeRow);
    auto *metrics = new QGridLayout;
    metrics->setSpacing(14);
    metrics->addWidget(metricCard(QStringLiteral("今日营收"), QStringLiteral("#2f6fed"), &todayRevenue_, page), 0, 0);
    metrics->addWidget(metricCard(QStringLiteral("本月营收"), QStringLiteral("#13a06f"), &monthRevenue_, page), 0, 1);
    metrics->addWidget(metricCard(QStringLiteral("累计营收"), QStringLiteral("#f29d38"), &totalRevenue_, page), 0, 2);
    metrics->addWidget(metricCard(QStringLiteral("站点 / 电桩"), QStringLiteral("#875bd8"), &resourceCount_, page), 0, 3);
    layout->addLayout(metrics);
    auto *lower = new QHBoxLayout;
    lower->setSpacing(14);
    auto *chartPanel = panel(page);
    auto *chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(20, 18, 20, 16);
    chartLayout->addWidget(heading(QStringLiteral("营收趋势（元）"), chartPanel));
    revenueChart_ = new RevenueChart(chartPanel);
    chartLayout->addWidget(revenueChart_, 1);
    lower->addWidget(chartPanel, 3);
    auto *statePanel = panel(page);
    auto *stateLayout = new QVBoxLayout(statePanel);
    stateLayout->setContentsMargins(22, 18, 22, 22);
    stateLayout->setSpacing(18);
    stateLayout->addWidget(heading(QStringLiteral("电桩状态"), statePanel));
    idlePiles_ = new QLabel(statePanel);
    inUsePiles_ = new QLabel(statePanel);
    faultPiles_ = new QLabel(statePanel);
    for (QLabel *label : {idlePiles_, inUsePiles_, faultPiles_}) {
        label->setProperty("role", "metric");
        stateLayout->addWidget(label);
    }
    stateLayout->addStretch();
    auto *note = new QLabel(QStringLiteral("状态由服务端数据源实时聚合。"), statePanel);
    note->setProperty("role", "muted");
    note->setWordWrap(true);
    stateLayout->addWidget(note);
    lower->addWidget(statePanel, 1);
    layout->addLayout(lower, 1);
    return page;
}

QWidget *AdminWindow::buildStationsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    stationSearch_ = new QLineEdit(page);
    stationSearch_->setPlaceholderText(QStringLiteral("搜索站名或地址"));
    stationSearch_->setMaximumWidth(300);
    stationRegion_ = new QComboBox(page);
    stationRegion_->addItems({QStringLiteral("全部区域"), QStringLiteral("浑南区"),
                              QStringLiteral("和平区"), QStringLiteral("沈北新区")});
    auto *searchButton = new QPushButton(QStringLiteral("查询"), page);
    connect(searchButton, &QPushButton::clicked, this, &AdminWindow::refreshStations);
    connect(stationSearch_, &QLineEdit::returnPressed, this, &AdminWindow::refreshStations);
    auto *createButton = new QPushButton(QStringLiteral("＋ 新增充电站"), page);
    createButton->setProperty("primary", true);
    connect(createButton, &QPushButton::clicked, this, &AdminWindow::showCreateStationDialog);
    auto *deleteButton = new QPushButton(QStringLiteral("删除选中站点"), page);
    deleteButton->setProperty("danger", true);
    connect(deleteButton, &QPushButton::clicked,
            this, &AdminWindow::deleteSelectedStation);
    controls->addWidget(stationSearch_);
    controls->addWidget(stationRegion_);
    controls->addWidget(searchButton);
    controls->addStretch();
    controls->addWidget(deleteButton);
    controls->addWidget(createButton);
    layout->addLayout(controls);
    stationsTable_ = new QTableWidget(page);
    prepareTable(stationsTable_, {QStringLiteral("ID"), QStringLiteral("站点名称"),
                                  QStringLiteral("区域"), QStringLiteral("地址"),
                                  QStringLiteral("经度"), QStringLiteral("纬度"),
                                  QStringLiteral("价格"), QStringLiteral("电桩"),
                                  QStringLiteral("在线率"), QStringLiteral("状态")});
    layout->addWidget(stationsTable_, 1);
    return page;
}

QWidget *AdminWindow::buildPilesPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    auto *hint = new QLabel(QStringLiteral("选中故障、离线或空闲电桩后可执行模拟重启"), page);
    hint->setProperty("role", "muted");
    controls->addWidget(hint);
    controls->addStretch();
    auto *restartButton = new QPushButton(QStringLiteral("模拟重启"), page);
    restartButton->setProperty("primary", true);
    connect(restartButton, &QPushButton::clicked, this, &AdminWindow::restartSelectedPile);
    controls->addWidget(restartButton);
    layout->addLayout(controls);
    pilesTable_ = new QTableWidget(page);
    prepareTable(pilesTable_, {QStringLiteral("ID"), QStringLiteral("电桩编号"),
                               QStringLiteral("站点 ID"), QStringLiteral("类型"),
                               QStringLiteral("功率 kW"), QStringLiteral("状态"),
                               QStringLiteral("累计次数"), QStringLiteral("累计时长")});
    layout->addWidget(pilesTable_, 1);
    return page;
}

QWidget *AdminWindow::buildUsersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    userSearch_ = new QLineEdit(page);
    userSearch_->setPlaceholderText(QStringLiteral("按手机号搜索"));
    userSearch_->setMaximumWidth(300);
    auto *searchButton = new QPushButton(QStringLiteral("查询"), page);
    connect(searchButton, &QPushButton::clicked, this, &AdminWindow::refreshUsers);
    connect(userSearch_, &QLineEdit::returnPressed, this, &AdminWindow::refreshUsers);
    controls->addWidget(userSearch_);
    controls->addWidget(searchButton);
    controls->addStretch();
    auto *toggleButton = new QPushButton(QStringLiteral("冻结 / 解冻"), page);
    toggleButton->setProperty("danger", true);
    connect(toggleButton, &QPushButton::clicked, this, &AdminWindow::toggleSelectedUserStatus);
    controls->addWidget(toggleButton);
    layout->addLayout(controls);
    usersTable_ = new QTableWidget(page);
    prepareTable(usersTable_, {QStringLiteral("ID"), QStringLiteral("手机号"),
                               QStringLiteral("昵称"), QStringLiteral("余额"),
                               QStringLiteral("注册时间 UTC"), QStringLiteral("状态")});
    layout->addWidget(usersTable_, 1);
    return page;
}

QWidget *AdminWindow::buildOrdersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *hint = new QLabel(QStringLiteral("订单金额、状态和计量数据由服务端维护，管理端仅查看。"), page);
    hint->setProperty("role", "muted");
    layout->addWidget(hint);
    ordersTable_ = new QTableWidget(page);
    prepareTable(ordersTable_, {QStringLiteral("订单 ID"), QStringLiteral("订单号"),
                                QStringLiteral("用户 ID"), QStringLiteral("站点"),
                                QStringLiteral("电桩"), QStringLiteral("模式"),
                                QStringLiteral("状态"), QStringLiteral("时长"),
                                QStringLiteral("电量"), QStringLiteral("金额"),
                                QStringLiteral("创建时间 UTC")});
    layout->addWidget(ordersTable_, 1);
    return page;
}

void AdminWindow::attemptLogin()
{
    if (facade_ == nullptr) {
        loginError_->setText(QStringLiteral("管理服务尚未初始化"));
        return;
    }
    const ServiceResult result = facade_->login(usernameEdit_->text().trimmed(), passwordEdit_->text());
    if (!result.ok()) {
        loginError_->setText(QStringLiteral("账号或密码错误，请重试"));
        passwordEdit_->selectAll();
        passwordEdit_->setFocus();
        return;
    }
    loginError_->clear();
    rootStack_->setCurrentIndex(1);
    navigation_->setCurrentRow(0);
    refreshAll();
}

void AdminWindow::selectPage(int index)
{
    if (index < 0 || contentStack_ == nullptr) return;
    static const QStringList titles{QStringLiteral("数据概览"), QStringLiteral("充电站管理"),
                                    QStringLiteral("充电桩管理"), QStringLiteral("用户管理"),
                                    QStringLiteral("订单管理")};
    contentStack_->setCurrentIndex(index);
    pageTitle_->setText(titles.value(index));
    switch (index) {
    case 0: refreshDashboard(); break;
    case 1: refreshStations(); break;
    case 2: refreshPiles(); break;
    case 3: refreshUsers(); break;
    case 4: refreshOrders(); break;
    default: break;
    }
}

void AdminWindow::refreshAll()
{
    refreshDashboard();
    refreshStations();
    refreshPiles();
    refreshUsers();
    refreshOrders();
}

void AdminWindow::refreshDashboard()
{
    if (facade_ == nullptr || dashboardDays_ == nullptr) return;
    const ServiceResult result = facade_->getDashboard(dashboardDays_->currentData().toInt());
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonObject &data = result.data;
    todayRevenue_->setText(moneyText(data.value(QStringLiteral("todayRevenueCents")).toInteger()));
    monthRevenue_->setText(moneyText(data.value(QStringLiteral("monthRevenueCents")).toInteger()));
    totalRevenue_->setText(moneyText(data.value(QStringLiteral("totalRevenueCents")).toInteger()));
    resourceCount_->setText(QStringLiteral("%1 / %2")
        .arg(data.value(QStringLiteral("stationCount")).toInt())
        .arg(data.value(QStringLiteral("pileCount")).toInt()));
    const QJsonObject states = data.value(QStringLiteral("pileStates")).toObject();
    idlePiles_->setText(QStringLiteral("● 空闲  %1").arg(states.value(QStringLiteral("idle")).toInt()));
    idlePiles_->setStyleSheet(QStringLiteral("color:#15803d;"));
    inUsePiles_->setText(QStringLiteral("● 在用  %1").arg(states.value(QStringLiteral("inUse")).toInt()));
    inUsePiles_->setStyleSheet(QStringLiteral("color:#c26908;"));
    faultPiles_->setText(QStringLiteral("● 故障/离线  %1").arg(states.value(QStringLiteral("fault")).toInt()));
    faultPiles_->setStyleSheet(QStringLiteral("color:#c33838;"));
    QList<RevenuePoint> points;
    for (const QJsonValue &value : data.value(QStringLiteral("revenuePoints")).toArray()) {
        const QJsonObject point = value.toObject();
        points.append({point.value(QStringLiteral("date")).toString(),
                       point.value(QStringLiteral("revenueCents")).toInteger()});
    }
    revenueChart_->setPoints(std::move(points));
}

void AdminWindow::refreshStations()
{
    if (facade_ == nullptr || stationsTable_ == nullptr) return;
    const QString region = stationRegion_->currentIndex() == 0 ? QString{} : stationRegion_->currentText();
    const ServiceResult result = facade_->listStations(region, stationSearch_->text().trimmed());
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    stationsTable_->setRowCount(rows.size());
    for (qsizetype row = 0; row < rows.size(); ++row) {
        const QJsonObject station = rows.at(row).toObject();
        const QString status = station.value(QStringLiteral("status")).toString();
        stationsTable_->setItem(row, 0, numberItem(station.value(QStringLiteral("stationId")).toInteger()));
        stationsTable_->setItem(row, 1, item(station.value(QStringLiteral("name")).toString()));
        stationsTable_->setItem(row, 2, item(station.value(QStringLiteral("region")).toString()));
        stationsTable_->setItem(row, 3, item(station.value(QStringLiteral("address")).toString()));
        stationsTable_->setItem(row, 4, item(QString::number(station.value(QStringLiteral("longitude")).toDouble(), 'f', 4)));
        stationsTable_->setItem(row, 5, item(QString::number(station.value(QStringLiteral("latitude")).toDouble(), 'f', 4)));
        stationsTable_->setItem(row, 6, item(QStringLiteral("¥ %1/度").arg(station.value(QStringLiteral("priceCentsPerKwh")).toInteger() / 100.0, 0, 'f', 2)));
        stationsTable_->setItem(row, 7, numberItem(station.value(QStringLiteral("totalPileCount")).toInteger()));
        stationsTable_->setItem(row, 8, item(QStringLiteral("%1%").arg(station.value(QStringLiteral("onlineRatePercent")).toDouble(), 0, 'f', 0)));
        auto *statusItem = item(stationStatusText(status));
        colorStatus(statusItem, status);
        stationsTable_->setItem(row, 9, statusItem);
    }
}

void AdminWindow::refreshPiles()
{
    if (facade_ == nullptr || pilesTable_ == nullptr) return;
    const ServiceResult result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    pilesTable_->setRowCount(rows.size());
    for (qsizetype row = 0; row < rows.size(); ++row) {
        const QJsonObject pile = rows.at(row).toObject();
        const QString status = pile.value(QStringLiteral("status")).toString();
        pilesTable_->setItem(row, 0, numberItem(pile.value(QStringLiteral("pileId")).toInteger()));
        pilesTable_->setItem(row, 1, item(pile.value(QStringLiteral("pileCode")).toString()));
        pilesTable_->setItem(row, 2, numberItem(pile.value(QStringLiteral("stationId")).toInteger()));
        pilesTable_->setItem(row, 3, item(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充")));
        pilesTable_->setItem(row, 4, item(QString::number(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 'f', 1)));
        auto *statusItem = item(pileStatusText(status));
        colorStatus(statusItem, status);
        pilesTable_->setItem(row, 5, statusItem);
        pilesTable_->setItem(row, 6, numberItem(pile.value(QStringLiteral("chargeCount")).toInteger()));
        pilesTable_->setItem(row, 7, item(QStringLiteral("%1 小时").arg(pile.value(QStringLiteral("totalChargeSeconds")).toInteger() / 3600.0, 0, 'f', 1)));
    }
}

void AdminWindow::refreshUsers()
{
    if (facade_ == nullptr || usersTable_ == nullptr) return;
    const ServiceResult result = facade_->listUsers(userSearch_->text().trimmed());
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    usersTable_->setRowCount(rows.size());
    for (qsizetype row = 0; row < rows.size(); ++row) {
        const QJsonObject user = rows.at(row).toObject();
        const QString status = user.value(QStringLiteral("status")).toString();
        usersTable_->setItem(row, 0, numberItem(user.value(QStringLiteral("userId")).toInteger()));
        usersTable_->setItem(row, 1, item(user.value(QStringLiteral("phone")).toString()));
        usersTable_->setItem(row, 2, item(user.value(QStringLiteral("nickname")).toString()));
        usersTable_->setItem(row, 3, item(moneyText(user.value(QStringLiteral("balanceCents")).toInteger())));
        usersTable_->setItem(row, 4, item(user.value(QStringLiteral("createdAt")).toString()));
        auto *statusItem = item(userStatusText(status));
        statusItem->setData(Qt::UserRole, status);
        colorStatus(statusItem, status);
        usersTable_->setItem(row, 5, statusItem);
    }
}

void AdminWindow::refreshOrders()
{
    if (facade_ == nullptr || ordersTable_ == nullptr) return;
    const ServiceResult result = facade_->listOrders();
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    ordersTable_->setRowCount(rows.size());
    for (qsizetype row = 0; row < rows.size(); ++row) {
        const QJsonObject order = rows.at(row).toObject();
        const QString status = order.value(QStringLiteral("status")).toString();
        ordersTable_->setItem(row, 0, numberItem(order.value(QStringLiteral("orderId")).toInteger()));
        ordersTable_->setItem(row, 1, item(order.value(QStringLiteral("orderNo")).toString()));
        ordersTable_->setItem(row, 2, numberItem(order.value(QStringLiteral("userId")).toInteger()));
        ordersTable_->setItem(row, 3, item(order.value(QStringLiteral("stationName")).toString()));
        ordersTable_->setItem(row, 4, item(order.value(QStringLiteral("pileCode")).toString()));
        ordersTable_->setItem(row, 5, item(order.value(QStringLiteral("mode")).toString() == QStringLiteral("DIRECT") ? QStringLiteral("直接充电") : QStringLiteral("预约")));
        auto *statusItem = item(orderStatusText(status));
        colorStatus(statusItem, status);
        ordersTable_->setItem(row, 6, statusItem);
        ordersTable_->setItem(row, 7, item(QStringLiteral("%1 分钟").arg(order.value(QStringLiteral("durationSeconds")).toInteger() / 60)));
        ordersTable_->setItem(row, 8, item(QStringLiteral("%1 kWh").arg(order.value(QStringLiteral("energyWh")).toInteger() / 1000.0, 0, 'f', 2)));
        ordersTable_->setItem(row, 9, item(moneyText(order.value(QStringLiteral("amountCents")).toInteger())));
        ordersTable_->setItem(row, 10, item(order.value(QStringLiteral("createdAt")).toString()));
    }
}

void AdminWindow::showCreateStationDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增充电站"));
    dialog.setMinimumWidth(460);
    auto *layout = new QFormLayout(&dialog);
    auto *name = new QLineEdit(&dialog);
    auto *region = new QComboBox(&dialog);
    region->addItems({QStringLiteral("浑南区"), QStringLiteral("和平区"),
                      QStringLiteral("沈北新区"), QStringLiteral("铁西区")});
    auto *address = new QLineEdit(&dialog);
    auto *longitude = new QDoubleSpinBox(&dialog);
    longitude->setRange(-180.0, 180.0);
    longitude->setDecimals(6);
    longitude->setValue(123.43);
    auto *latitude = new QDoubleSpinBox(&dialog);
    latitude->setRange(-90.0, 90.0);
    latitude->setDecimals(6);
    latitude->setValue(41.71);
    auto *price = new QSpinBox(&dialog);
    price->setRange(1, 10000);
    price->setValue(135);
    auto *pileCount = new QSpinBox(&dialog);
    pileCount->setRange(0, 100);
    pileCount->setValue(4);
    layout->addRow(QStringLiteral("站点名称"), name);
    layout->addRow(QStringLiteral("区域"), region);
    layout->addRow(QStringLiteral("详细地址"), address);
    layout->addRow(QStringLiteral("经度"), longitude);
    layout->addRow(QStringLiteral("纬度"), latitude);
    layout->addRow(QStringLiteral("单价（分/kWh）"), price);
    layout->addRow(QStringLiteral("初始电桩数"), pileCount);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const ServiceResult result = facade_->createStation({
        {QStringLiteral("name"), name->text().trimmed()}, {QStringLiteral("region"), region->currentText()},
        {QStringLiteral("address"), address->text().trimmed()}, {QStringLiteral("longitude"), longitude->value()},
        {QStringLiteral("latitude"), latitude->value()}, {QStringLiteral("priceCentsPerKwh"), price->value()},
        {QStringLiteral("pileCount"), pileCount->value()},
    });
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAll();
}

void AdminWindow::deleteSelectedStation()
{
    const int row = stationsTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("删除站点"),
                                 QStringLiteral("请先选择一个充电站。"));
        return;
    }

    const qint64 stationId =
        stationsTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const QString stationName = stationsTable_->item(row, 1)->text();
    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除站点"),
        QStringLiteral("确定删除“%1”及其所有无订单电桩吗？\n"
                       "已存在历史或进行中订单的站点不允许删除。")
            .arg(stationName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    const ServiceResult result = facade_->deleteStation(stationId);
    if (!result.ok()) {
        if (result.code == ErrorCode::IllegalOrderState) {
            QMessageBox::warning(
                this, QStringLiteral("无法删除"),
                QStringLiteral("该站点已有历史或进行中订单，"
                               "为保留订单数据不能物理删除。"));
            return;
        }
        return showServiceError(result.code, result.message);
    }

    refreshAll();
    QMessageBox::information(this, QStringLiteral("删除站点"),
                             QStringLiteral("站点及其无订单电桩已删除。"));
}

void AdminWindow::restartSelectedPile()
{
    const int row = pilesTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("模拟重启"), QStringLiteral("请先选择一个电桩。"));
        return;
    }
    const qint64 pileId = pilesTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const ServiceResult result = facade_->restartPile(pileId);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAll();
    QMessageBox::information(this, QStringLiteral("模拟重启"), QStringLiteral("电桩已恢复为空闲状态。"));
}

void AdminWindow::toggleSelectedUserStatus()
{
    const int row = usersTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("用户状态"), QStringLiteral("请先选择一个用户。"));
        return;
    }
    const qint64 userId = usersTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const QString current = usersTable_->item(row, 5)->data(Qt::UserRole).toString();
    const UserStatus target = current == QStringLiteral("ACTIVE") ? UserStatus::Frozen : UserStatus::Active;
    const ServiceResult result = facade_->setUserStatus(userId, target);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshUsers();
}

void AdminWindow::showServiceError(int code, const QString &message)
{
    QString detail = message;
    if (code == ErrorCode::CurrentOrderExists) detail = QStringLiteral("该用户存在未结束订单，暂时不能冻结。");
    else if (code == ErrorCode::IllegalOrderState) detail = QStringLiteral("充电中或已预约的电桩不能重启。");
    else if (code == ErrorCode::InvalidRequest) detail = QStringLiteral("输入内容不完整或格式不正确。");
    QMessageBox::warning(this, QStringLiteral("操作失败"),
                         QStringLiteral("%1\n错误码：%2").arg(detail).arg(code));
}

void AdminWindow::prepareTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
}

QString AdminWindow::moneyText(qint64 cents)
{
    return QStringLiteral("¥ %1").arg(cents / 100.0, 0, 'f', 2);
}

}  // namespace charging::server
