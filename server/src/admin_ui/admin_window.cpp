#include "admin_window.h"

#include "admin_facade.h"
#include "revenue_chart.h"

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QMenu>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QTextBrowser>

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
    detailsDock_ = new QDockWidget(this);
    detailsDock_->setAllowedAreas(Qt::RightDockWidgetArea);
    detailsDock_->setFeatures(QDockWidget::DockWidgetClosable);
    detailsDock_->setMinimumWidth(360);
    detailsDock_->hide();
    addDockWidget(Qt::RightDockWidgetArea, detailsDock_);

    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QStackedWidget { background: #f3f6fb; }
        QWidget { color: #243044; font-family: "Noto Sans CJK SC", "Microsoft YaHei", sans-serif; font-size: 14px; }
        QFrame#panel, QFrame#loginCard { background: white; border: 1px solid #e7ebf1; border-radius: 9px; }
        QFrame#brandPanel { background: #1746a2; border: none; border-radius: 12px; }
        QLabel[role="hero"] { color: white; font-size: 27px; font-weight: 600; }
        QLabel[role="heroSub"] { color: #cdddff; font-size: 14px; }
        QLabel[role="title"] { font-size: 23px; font-weight: 600; }
        QLabel[role="sectionTitle"] { font-size: 17px; font-weight: 600; }
        QLabel[role="muted"] { color: #748096; }
        QLabel[role="metric"] { font-size: 25px; font-weight: 600; color: #172033; }
        QLabel[role="error"] { color: #c33838; }
        QLabel[role="badgeOk"] { color: #157347; background: #e7f7ed; padding: 6px 10px; border-radius: 12px; }
        QLabel[role="badgeBad"] { color: #a61b1b; background: #ffe9e9; padding: 6px 10px; border-radius: 12px; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit { background: white; border: 1px solid #d8dfeb; border-radius: 6px; padding: 6px 9px; min-height: 20px; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #2f6fed; }
        QPushButton { background: #eef3f9; border: none; border-radius: 6px; padding: 7px 13px; color: #244168; font-weight: 500; }
        QPushButton:hover { background: #dfe8f7; }
        QPushButton[primary="true"] { color: white; background: #2f6fed; }
        QPushButton[primary="true"]:hover { background: #2459c5; }
        QPushButton[danger="true"] { color: #a61b1b; background: #ffe9e9; }
        QListWidget#navigation { background: #102a56; border: none; color: #cbd9ee; outline: none; padding: 6px; }
        QListWidget#navigation::item { border-radius: 8px; padding: 13px 14px; margin: 3px 5px; }
        QListWidget#navigation::item:selected { background: #2f6fed; color: white; }
        QListWidget#navigation::item:hover:!selected { background: #193a70; }
        QTableWidget, QTreeWidget { background: white; border: 1px solid #e7ebf1; border-radius: 8px; gridline-color: #edf0f5; selection-background-color: #edf3ff; selection-color: #172033; alternate-background-color: #fafbfd; }
        QHeaderView::section { background: #f6f8fc; color: #59677e; border: none; border-bottom: 1px solid #e5eaf2; padding: 10px 8px; font-weight: 600; }
    )"));
}

void AdminWindow::showDetails(const QString &title, const QString &content)
{
    auto *viewer = new QTextBrowser(detailsDock_);
    viewer->setPlainText(content);
    viewer->setFrameShape(QFrame::NoFrame);
    viewer->setReadOnly(true);
    viewer->setStyleSheet(QStringLiteral("QTextBrowser { background:white; padding:18px; line-height:1.5; }"));
    detailsDock_->setWindowTitle(title);
    detailsDock_->setWidget(viewer);
    detailsDock_->show();
    detailsDock_->raise();
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
    brandLayout->addStretch();
    cardLayout->addWidget(brand, 5);

    auto *formArea = new QWidget(card);
    auto *formLayout = new QVBoxLayout(formArea);
    formLayout->setContentsMargins(64, 70, 64, 70);
    formLayout->setSpacing(14);
    formLayout->addStretch();
    formLayout->addWidget(heading(QStringLiteral("管理员登录"), formArea, "title"));
    formLayout->addSpacing(18);
    formLayout->addWidget(new QLabel(QStringLiteral("账号"), formArea));
    usernameEdit_ = new QLineEdit(formArea);
    usernameEdit_->setPlaceholderText(QStringLiteral("请输入管理员账号"));
    formLayout->addWidget(usernameEdit_);
    formLayout->addWidget(new QLabel(QStringLiteral("密码"), formArea));
    passwordEdit_ = new QLineEdit(formArea);
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
    rangeRow->addStretch();
    dashboardDays_ = new QComboBox(page);
    dashboardDays_->addItem(QStringLiteral("近 7 日"), 7);
    dashboardDays_->addItem(QStringLiteral("近 30 日"), 30);
    dashboardDays_->addItem(QStringLiteral("自定义"), -1);
    dashboardStartDate_ = new QDateEdit(QDate::currentDate().addDays(-6), page);
    dashboardStartDate_->setCalendarPopup(true);
    dashboardStartDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dashboardEndDate_ = new QDateEdit(QDate::currentDate(), page);
    dashboardEndDate_->setCalendarPopup(true);
    dashboardEndDate_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dashboardApplyButton_ = new QPushButton(QStringLiteral("应用"), page);
    auto *startLabel = new QLabel(QStringLiteral("起始"), page);
    auto *endLabel = new QLabel(QStringLiteral("终止"), page);
    for (QWidget *control : {static_cast<QWidget *>(startLabel),
                             static_cast<QWidget *>(dashboardStartDate_),
                             static_cast<QWidget *>(endLabel),
                             static_cast<QWidget *>(dashboardEndDate_),
                             static_cast<QWidget *>(dashboardApplyButton_)}) control->hide();
    connect(dashboardDays_, &QComboBox::currentIndexChanged,
            this, [this, startLabel, endLabel] {
                const bool custom = dashboardDays_->currentData().toInt() < 0;
                startLabel->setVisible(custom);
                dashboardStartDate_->setVisible(custom);
                endLabel->setVisible(custom);
                dashboardEndDate_->setVisible(custom);
                dashboardApplyButton_->setVisible(custom);
                if (!custom) refreshDashboard();
            });
    connect(dashboardApplyButton_, &QPushButton::clicked,
            this, &AdminWindow::refreshDashboard);
    rangeRow->addWidget(dashboardDays_);
    rangeRow->addWidget(startLabel);
    rangeRow->addWidget(dashboardStartDate_);
    rangeRow->addWidget(endLabel);
    rangeRow->addWidget(dashboardEndDate_);
    rangeRow->addWidget(dashboardApplyButton_);
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
    controls->addWidget(new QLabel(QStringLiteral("关键词"), page));
    stationSearch_ = new QLineEdit(page);
    stationSearch_->setMaximumWidth(300);
    stationRegion_ = new QComboBox(page);
    stationRegion_->addItems({QStringLiteral("全部区域"), QStringLiteral("浑南区"),
                              QStringLiteral("和平区"), QStringLiteral("沈北新区"),
                              QStringLiteral("沈河区"), QStringLiteral("铁西区")});
    stationStatus_ = new QComboBox(page);
    stationStatus_->addItem(QStringLiteral("全部状态"), QString{});
    stationStatus_->addItem(QStringLiteral("启用"), QStringLiteral("ACTIVE"));
    stationStatus_->addItem(QStringLiteral("停用"), QStringLiteral("DISABLED"));
    auto *searchButton = new QPushButton(QStringLiteral("搜索"), page);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    const auto applyStationSearch = [this] { appliedStationSearch_ = stationSearch_->text().trimmed(); refreshStations(); };
    connect(searchButton, &QPushButton::clicked, this, applyStationSearch);
    connect(stationSearch_, &QLineEdit::returnPressed, this, applyStationSearch);
    connect(stationRegion_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshStations);
    connect(stationStatus_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshStations);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        stationSearch_->clear(); appliedStationSearch_.clear();
        stationRegion_->setCurrentIndex(0); stationStatus_->setCurrentIndex(0); refreshStations();
    });
    auto *createButton = new QPushButton(QStringLiteral("＋ 新增充电站"), page);
    createButton->setProperty("primary", true);
    connect(createButton, &QPushButton::clicked, this, &AdminWindow::showCreateStationDialog);
    controls->addWidget(stationSearch_);
    controls->addWidget(stationRegion_);
    controls->addWidget(stationStatus_);
    controls->addWidget(searchButton);
    controls->addWidget(resetButton);
    controls->addStretch();
    controls->addWidget(createButton);
    layout->addLayout(controls);
    stationsTable_ = new QTreeWidget(page);
    stationsTable_->setColumnCount(9);
    stationsTable_->setHeaderLabels({QStringLiteral("站点"), QStringLiteral("区域"),
                                     QStringLiteral("地址"), QStringLiteral("可用 / 总数"),
                                     QStringLiteral("在线率"), QStringLiteral("电价"),
                                     QStringLiteral("状态"), QStringLiteral("ID"),
                                     QStringLiteral("操作")});
    stationsTable_->setRootIsDecorated(true);
    stationsTable_->setAlternatingRowColors(true);
    stationsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    stationsTable_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    stationsTable_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    stationsTable_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    stationsTable_->header()->setSectionResizeMode(8, QHeaderView::Fixed);
    stationsTable_->setColumnWidth(8, 128);
    layout->addWidget(stationsTable_, 1);
    return page;
}

QWidget *AdminWindow::buildPilesPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("关键词"), page));
    pileSearch_ = new QLineEdit(page);
    pileSearch_->setMaximumWidth(240);
    pileStation_ = new QComboBox(page);
    pileStation_->addItem(QStringLiteral("全部站点"), QVariant{});
    pileStatus_ = new QComboBox(page);
    pileStatus_->addItem(QStringLiteral("全部状态"), QString{});
    for (const QString &status : {QStringLiteral("IDLE"), QStringLiteral("RESERVED"), QStringLiteral("CHARGING"), QStringLiteral("FAULT"), QStringLiteral("OFFLINE")}) {
        pileStatus_->addItem(pileStatusText(status), status);
    }
    auto *searchButton = new QPushButton(QStringLiteral("搜索"), page);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    const auto applyPileSearch = [this] { appliedPileSearch_ = pileSearch_->text().trimmed(); refreshPiles(); };
    connect(searchButton, &QPushButton::clicked, this, applyPileSearch);
    connect(pileSearch_, &QLineEdit::returnPressed, this, applyPileSearch);
    connect(pileStation_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshPiles);
    connect(pileStatus_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshPiles);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        pileSearch_->clear(); appliedPileSearch_.clear();
        pileStation_->setCurrentIndex(0); pileStatus_->setCurrentIndex(0); refreshPiles();
    });
    controls->addWidget(pileSearch_);
    controls->addWidget(searchButton);
    controls->addWidget(resetButton);
    controls->addWidget(pileStation_);
    controls->addWidget(pileStatus_);
    controls->addStretch();
    auto *createButton = new QPushButton(QStringLiteral("＋ 新增电桩"), page);
    createButton->setProperty("primary", true);
    connect(createButton, &QPushButton::clicked, this, &AdminWindow::showCreatePileDialog);
    controls->addWidget(createButton);
    layout->addLayout(controls);
    pilesTable_ = new QTableWidget(page);
    prepareTable(pilesTable_, {QStringLiteral("ID"), QStringLiteral("电桩编号"),
                               QStringLiteral("所属站点"), QStringLiteral("类型"),
                               QStringLiteral("功率 kW"), QStringLiteral("状态"),
                               QStringLiteral("累计次数"), QStringLiteral("累计时长"),
                               QStringLiteral("操作")});
    pilesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pilesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(pilesTable_, 1);
    return page;
}

QWidget *AdminWindow::buildUsersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("关键词"), page));
    userSearch_ = new QLineEdit(page);
    userSearch_->setMaximumWidth(300);
    auto *searchButton = new QPushButton(QStringLiteral("搜索"), page);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    const auto applyUserSearch = [this] { appliedUserSearch_ = userSearch_->text().trimmed(); refreshUsers(); };
    connect(searchButton, &QPushButton::clicked, this, applyUserSearch);
    connect(userSearch_, &QLineEdit::returnPressed, this, applyUserSearch);
    controls->addWidget(userSearch_);
    controls->addWidget(searchButton);
    userStatus_ = new QComboBox(page);
    userStatus_->addItem(QStringLiteral("全部状态"), QString{});
    userStatus_->addItem(QStringLiteral("正常"), QStringLiteral("ACTIVE"));
    userStatus_->addItem(QStringLiteral("已冻结"), QStringLiteral("FROZEN"));
    connect(userStatus_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshUsers);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        userSearch_->clear(); appliedUserSearch_.clear(); userStatus_->setCurrentIndex(0); refreshUsers();
    });
    controls->addWidget(resetButton);
    controls->addWidget(userStatus_);
    controls->addStretch();
    layout->addLayout(controls);
    usersTable_ = new QTableWidget(page);
    prepareTable(usersTable_, {QStringLiteral("ID"), QStringLiteral("手机号"),
                               QStringLiteral("昵称"), QStringLiteral("余额"),
                               QStringLiteral("注册时间"), QStringLiteral("状态"),
                               QStringLiteral("操作")});
    usersTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(usersTable_, 1);
    return page;
}

QWidget *AdminWindow::buildOrdersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("关键词"), page));
    orderSearch_ = new QLineEdit(page);
    orderSearch_->setMaximumWidth(260);
    auto *searchButton = new QPushButton(QStringLiteral("搜索"), page);
    auto *resetButton = new QPushButton(QStringLiteral("重置"), page);
    orderStatus_ = new QComboBox(page);
    orderStatus_->addItem(QStringLiteral("全部状态"), QString{});
    for (const QString &status : {QStringLiteral("RESERVED"), QStringLiteral("CHARGING"), QStringLiteral("PENDING_PAYMENT"), QStringLiteral("COMPLETED"), QStringLiteral("CANCELLED")}) {
        orderStatus_->addItem(orderStatusText(status), status);
    }
    orderMode_ = new QComboBox(page);
    orderMode_->addItem(QStringLiteral("全部模式"), QString{});
    orderMode_->addItem(QStringLiteral("直接充电"), QStringLiteral("DIRECT"));
    orderMode_->addItem(QStringLiteral("预约"), QStringLiteral("RESERVATION"));
    const auto applyOrderSearch = [this] { appliedOrderSearch_ = orderSearch_->text().trimmed(); refreshOrders(); };
    connect(searchButton, &QPushButton::clicked, this, applyOrderSearch);
    connect(orderSearch_, &QLineEdit::returnPressed, this, applyOrderSearch);
    connect(orderStatus_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshOrders);
    connect(orderMode_, &QComboBox::currentIndexChanged, this, &AdminWindow::refreshOrders);
    connect(resetButton, &QPushButton::clicked, this, [this] {
        orderSearch_->clear(); appliedOrderSearch_.clear();
        orderStatus_->setCurrentIndex(0); orderMode_->setCurrentIndex(0); refreshOrders();
    });
    controls->addWidget(orderSearch_);
    controls->addWidget(searchButton);
    controls->addWidget(resetButton);
    controls->addWidget(orderStatus_);
    controls->addWidget(orderMode_);
    controls->addStretch();
    layout->addLayout(controls);
    ordersTable_ = new QTableWidget(page);
    prepareTable(ordersTable_, {QStringLiteral("订单 ID"), QStringLiteral("订单号"),
                                QStringLiteral("用户"), QStringLiteral("站点"),
                                QStringLiteral("电桩"), QStringLiteral("模式"),
                                QStringLiteral("状态"), QStringLiteral("时长"),
                                QStringLiteral("电量"), QStringLiteral("金额"),
                                QStringLiteral("创建时间"), QStringLiteral("操作")});
    ordersTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
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
    const int days = dashboardDays_->currentData().toInt();
    const ServiceResult result = days < 0
        ? facade_->getDashboard(dashboardStartDate_->date(), dashboardEndDate_->date())
        : facade_->getDashboard(days);
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
    QSet<qint64> expandedIds;
    for (int index = 0; index < stationsTable_->topLevelItemCount(); ++index) {
        auto *item = stationsTable_->topLevelItem(index);
        if (item->isExpanded()) expandedIds.insert(item->data(0, Qt::UserRole).toLongLong());
    }
    const QString region = stationRegion_->currentIndex() == 0 ? QString{} : stationRegion_->currentText();
    const ServiceResult result = facade_->listStations(region, appliedStationSearch_);
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    stationsTable_->clear();
    for (const QJsonValue &value : rows) {
        const QJsonObject station = value.toObject();
        const QString status = station.value(QStringLiteral("status")).toString();
        if (stationStatus_ != nullptr && !stationStatus_->currentData().toString().isEmpty()
            && status != stationStatus_->currentData().toString()) continue;
        const qint64 stationId = station.value(QStringLiteral("stationId")).toInteger();
        auto *stationItem = new QTreeWidgetItem(stationsTable_);
        stationItem->setData(0, Qt::UserRole, stationId);
        stationItem->setText(0, station.value(QStringLiteral("name")).toString());
        stationItem->setText(1, station.value(QStringLiteral("region")).toString());
        stationItem->setText(2, station.value(QStringLiteral("address")).toString());
        stationItem->setText(3, QStringLiteral("%1 / %2")
            .arg(station.value(QStringLiteral("availablePileCount")).toInteger())
            .arg(station.value(QStringLiteral("totalPileCount")).toInteger()));
        stationItem->setText(4, QStringLiteral("%1%").arg(station.value(QStringLiteral("onlineRatePercent")).toDouble(), 0, 'f', 0));
        stationItem->setText(5, QStringLiteral("¥%1/度").arg(station.value(QStringLiteral("priceCentsPerKwh")).toInteger() / 100.0, 0, 'f', 2));
        stationItem->setText(6, stationStatusText(status));
        stationItem->setText(7, QString::number(stationId));
        stationItem->setForeground(6, status == QStringLiteral("ACTIVE") ? QColor(QStringLiteral("#15803d")) : QColor(QStringLiteral("#667085")));

        const ServiceResult pileResult = facade_->listPiles(stationId);
        if (pileResult.ok()) {
            for (const QJsonValue &pileValue : pileResult.data.value(QStringLiteral("items")).toArray()) {
                const QJsonObject pile = pileValue.toObject();
                auto *child = new QTreeWidgetItem(stationItem);
                child->setData(0, Qt::UserRole, stationId);
                child->setData(0, Qt::UserRole + 1, pile.value(QStringLiteral("pileId")).toInteger());
                child->setText(0, QStringLiteral("电桩 · %1").arg(pile.value(QStringLiteral("pileCode")).toString()));
                child->setText(1, pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充"));
                child->setText(2, QStringLiteral("额定功率 %1 kW").arg(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 0, 'f', 1));
                child->setText(3, QStringLiteral("累计 %1 次").arg(pile.value(QStringLiteral("chargeCount")).toInteger()));
                child->setText(4, QStringLiteral("%1 小时").arg(pile.value(QStringLiteral("totalChargeSeconds")).toInteger() / 3600.0, 0, 'f', 1));
                child->setText(6, pileStatusText(pile.value(QStringLiteral("status")).toString()));
                child->setText(7, QString::number(pile.value(QStringLiteral("pileId")).toInteger()));
                const qint64 pileId = pile.value(QStringLiteral("pileId")).toInteger();
                auto *pileDetail = new QPushButton(QStringLiteral("详情"), stationsTable_);
                pileDetail->setMaximumWidth(56);
                connect(pileDetail, &QPushButton::clicked, this, [this, pileId] { showPileDetails(pileId); });
                stationsTable_->setItemWidget(child, 8, pileDetail);
            }
        }
        auto *actions = new QWidget(stationsTable_);
        auto *actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(0, 1, 0, 1);
        actionLayout->setSpacing(4);
        auto *detail = new QPushButton(QStringLiteral("详情"), actions);
        auto *more = new QPushButton(QStringLiteral("更多"), actions);
        detail->setMaximumWidth(56);
        more->setMaximumWidth(56);
        connect(detail, &QPushButton::clicked, this, [this, stationId] { showStationDetails(stationId); });
        auto *menu = new QMenu(more);
        auto *deleteAction = menu->addAction(QStringLiteral("删除站点"));
        connect(deleteAction, &QAction::triggered, this, [this, stationItem] {
            stationsTable_->setCurrentItem(stationItem);
            deleteSelectedStation();
        });
        more->setMenu(menu);
        actionLayout->addWidget(detail);
        actionLayout->addWidget(more);
        stationsTable_->setItemWidget(stationItem, 8, actions);
        stationItem->setExpanded(expandedIds.contains(stationId)
                                 || expandStationAfterRefresh_ == stationId);
    }
    expandStationAfterRefresh_ = 0;
}

void AdminWindow::refreshPiles()
{
    if (facade_ == nullptr || pilesTable_ == nullptr) return;
    QHash<qint64, QString> stationNames;
    if (pileStation_ != nullptr) {
        const QVariant current = pileStation_->currentData();
        const ServiceResult stations = facade_->listStations({}, {});
        QSignalBlocker blocker(pileStation_);
        pileStation_->clear();
        pileStation_->addItem(QStringLiteral("全部站点"), QVariant{});
        for (const QJsonValue &value : stations.data.value(QStringLiteral("items")).toArray()) {
            const QJsonObject station = value.toObject();
            stationNames.insert(station.value(QStringLiteral("stationId")).toInteger(),
                                station.value(QStringLiteral("name")).toString());
            pileStation_->addItem(station.value(QStringLiteral("name")).toString(), station.value(QStringLiteral("stationId")).toInteger());
        }
        const int restored = pileStation_->findData(current);
        pileStation_->setCurrentIndex(restored >= 0 ? restored : 0);
    }
    const ServiceResult result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    pilesTable_->setRowCount(0);
    for (const QJsonValue &value : rows) {
        const QJsonObject pile = value.toObject();
        const QString status = pile.value(QStringLiteral("status")).toString();
        if (!appliedPileSearch_.isEmpty()
            && !pile.value(QStringLiteral("pileCode")).toString().contains(appliedPileSearch_, Qt::CaseInsensitive)) continue;
        if (pileStation_ != nullptr && pileStation_->currentIndex() > 0
            && pile.value(QStringLiteral("stationId")).toInteger() != pileStation_->currentData().toLongLong()) continue;
        if (pileStatus_ != nullptr && !pileStatus_->currentData().toString().isEmpty()
            && status != pileStatus_->currentData().toString()) continue;
        const int row = pilesTable_->rowCount();
        pilesTable_->insertRow(row);
        pilesTable_->setItem(row, 0, numberItem(pile.value(QStringLiteral("pileId")).toInteger()));
        pilesTable_->setItem(row, 1, item(pile.value(QStringLiteral("pileCode")).toString()));
        pilesTable_->setItem(row, 2, item(stationNames.value(pile.value(QStringLiteral("stationId")).toInteger(), QStringLiteral("—"))));
        pilesTable_->setItem(row, 3, item(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充")));
        pilesTable_->setItem(row, 4, item(QString::number(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 'f', 1)));
        auto *statusItem = item(pileStatusText(status));
        colorStatus(statusItem, status);
        pilesTable_->setItem(row, 5, statusItem);
        pilesTable_->setItem(row, 6, numberItem(pile.value(QStringLiteral("chargeCount")).toInteger()));
        pilesTable_->setItem(row, 7, item(QStringLiteral("%1 小时").arg(pile.value(QStringLiteral("totalChargeSeconds")).toInteger() / 3600.0, 0, 'f', 1)));
        auto *detail = new QPushButton(QStringLiteral("详情"), pilesTable_);
        auto *more = new QPushButton(QStringLiteral("更多"), pilesTable_);
        const qint64 pileId = pile.value(QStringLiteral("pileId")).toInteger();
        connect(detail, &QPushButton::clicked, this, [this, pileId] { showPileDetails(pileId); });
        auto *menu = new QMenu(more);
        auto selectRow = [this, row] { pilesTable_->selectRow(row); };
        auto *powerOnAction = menu->addAction(QStringLiteral("开机/上线"));
        auto *powerOffAction = menu->addAction(QStringLiteral("关机/下线"));
        auto *faultAction = menu->addAction(QStringLiteral("标记故障"));
        auto *restartAction = menu->addAction(QStringLiteral("重启"));
        auto *deleteAction = menu->addAction(QStringLiteral("删除"));
        powerOnAction->setEnabled(status == QStringLiteral("OFFLINE"));
        powerOffAction->setEnabled(status == QStringLiteral("IDLE"));
        faultAction->setEnabled(status == QStringLiteral("IDLE") || status == QStringLiteral("OFFLINE"));
        restartAction->setEnabled(status == QStringLiteral("IDLE") || status == QStringLiteral("OFFLINE"));
        deleteAction->setEnabled(status == QStringLiteral("IDLE") || status == QStringLiteral("OFFLINE"));
        connect(powerOnAction, &QAction::triggered, this, [this, pileId] { const auto r = facade_->setPileStatus(pileId, PileStatus::Idle); if (!r.ok()) showServiceError(r.code, r.message); else refreshAll(); });
        connect(powerOffAction, &QAction::triggered, this, [this, pileId] { const auto r = facade_->setPileStatus(pileId, PileStatus::Offline); if (!r.ok()) showServiceError(r.code, r.message); else refreshAll(); });
        connect(faultAction, &QAction::triggered, this, [this, pileId] { const auto r = facade_->setPileStatus(pileId, PileStatus::Fault); if (!r.ok()) showServiceError(r.code, r.message); else refreshAll(); });
        connect(restartAction, &QAction::triggered, this, [this, selectRow] { selectRow(); restartSelectedPile(); });
        connect(deleteAction, &QAction::triggered, this, [this, selectRow] { selectRow(); deleteSelectedPile(); });
        more->setMenu(menu);
        auto *actions = new QWidget(pilesTable_);
        auto *actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(0, 1, 0, 1);
        actionLayout->setSpacing(4);
        actionLayout->addWidget(detail);
        actionLayout->addWidget(more);
        pilesTable_->setCellWidget(row, 8, actions);
    }
}

void AdminWindow::refreshUsers()
{
    if (facade_ == nullptr || usersTable_ == nullptr) return;
    const ServiceResult result = facade_->listUsers(appliedUserSearch_);
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    usersTable_->setRowCount(0);
    for (const QJsonValue &value : rows) {
        const QJsonObject user = value.toObject();
        const QString status = user.value(QStringLiteral("status")).toString();
        if (userStatus_ != nullptr && !userStatus_->currentData().toString().isEmpty()
            && status != userStatus_->currentData().toString()) continue;
        const int row = usersTable_->rowCount();
        usersTable_->insertRow(row);
        usersTable_->setItem(row, 0, numberItem(user.value(QStringLiteral("userId")).toInteger()));
        usersTable_->setItem(row, 1, item(user.value(QStringLiteral("phone")).toString()));
        usersTable_->setItem(row, 2, item(user.value(QStringLiteral("nickname")).toString()));
        usersTable_->setItem(row, 3, item(moneyText(user.value(QStringLiteral("balanceCents")).toInteger())));
        usersTable_->setItem(row, 4, item(user.value(QStringLiteral("createdAt")).toString()));
        auto *statusItem = item(userStatusText(status));
        statusItem->setData(Qt::UserRole, status);
        colorStatus(statusItem, status);
        usersTable_->setItem(row, 5, statusItem);
        auto *detail = new QPushButton(QStringLiteral("详情"), usersTable_);
        auto *more = new QPushButton(QStringLiteral("更多"), usersTable_);
        const qint64 userId = user.value(QStringLiteral("userId")).toInteger();
        connect(detail, &QPushButton::clicked, this, [this, userId] { showUserDetails(userId); });
        auto *menu = new QMenu(more);
        auto *toggle = menu->addAction(status == QStringLiteral("ACTIVE") ? QStringLiteral("冻结用户") : QStringLiteral("解除冻结"));
        connect(toggle, &QAction::triggered, this, [this, row] { usersTable_->selectRow(row); toggleSelectedUserStatus(); });
        more->setMenu(menu);
        auto *actions = new QWidget(usersTable_);
        auto *actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(0, 1, 0, 1);
        actionLayout->setSpacing(4);
        actionLayout->addWidget(detail);
        actionLayout->addWidget(more);
        usersTable_->setCellWidget(row, 6, actions);
    }
}

void AdminWindow::refreshOrders()
{
    if (facade_ == nullptr || ordersTable_ == nullptr) return;
    const ServiceResult result = facade_->listOrders();
    if (!result.ok()) return showServiceError(result.code, result.message);
    const QJsonArray rows = result.data.value(QStringLiteral("items")).toArray();
    QHash<qint64, QString> userNames;
    const ServiceResult userResult = facade_->listUsers();
    if (userResult.ok()) {
        for (const QJsonValue &value : userResult.data.value(QStringLiteral("items")).toArray()) {
            const QJsonObject user = value.toObject();
            userNames.insert(user.value(QStringLiteral("userId")).toInteger(),
                             QStringLiteral("%1 · %2").arg(user.value(QStringLiteral("nickname")).toString(),
                                                          user.value(QStringLiteral("phone")).toString()));
        }
    }
    ordersTable_->setRowCount(0);
    for (const QJsonValue &value : rows) {
        const QJsonObject order = value.toObject();
        const QString status = order.value(QStringLiteral("status")).toString();
        const QString keyword = appliedOrderSearch_;
        if (!keyword.isEmpty() && !order.value(QStringLiteral("orderNo")).toString().contains(keyword, Qt::CaseInsensitive)
            && !order.value(QStringLiteral("pileCode")).toString().contains(keyword, Qt::CaseInsensitive)) continue;
        if (orderStatus_ != nullptr && !orderStatus_->currentData().toString().isEmpty()
            && status != orderStatus_->currentData().toString()) continue;
        if (orderMode_ != nullptr && !orderMode_->currentData().toString().isEmpty()
            && order.value(QStringLiteral("mode")).toString() != orderMode_->currentData().toString()) continue;
        const int row = ordersTable_->rowCount();
        ordersTable_->insertRow(row);
        ordersTable_->setItem(row, 0, numberItem(order.value(QStringLiteral("orderId")).toInteger()));
        ordersTable_->setItem(row, 1, item(order.value(QStringLiteral("orderNo")).toString()));
        ordersTable_->setItem(row, 2, item(userNames.value(order.value(QStringLiteral("userId")).toInteger(),
                                                       QStringLiteral("用户 %1").arg(order.value(QStringLiteral("userId")).toInteger()))));
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
        auto *detail = new QPushButton(QStringLiteral("详情"), ordersTable_);
        const qint64 orderId = order.value(QStringLiteral("orderId")).toInteger();
        connect(detail, &QPushButton::clicked, this, [this, orderId] { showOrderDetails(orderId); });
        ordersTable_->setCellWidget(row, 11, detail);
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
    pileCount->setValue(0);
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
    expandStationAfterRefresh_ = result.data.value(QStringLiteral("station")).toObject()
                                     .value(QStringLiteral("stationId")).toInteger();
    stationSearch_->clear();
    appliedStationSearch_.clear();
    stationRegion_->setCurrentIndex(0);
    stationStatus_->setCurrentIndex(0);
    refreshAll();
}

void AdminWindow::showCreatePileDialog()
{
    const ServiceResult stationResult = facade_->listStations({}, {});
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增电桩"));
    dialog.setMinimumWidth(420);
    auto *layout = new QFormLayout(&dialog);
    auto *station = new QComboBox(&dialog);
    station->addItem(QStringLiteral("请选择充电站"), QVariant{});
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("status")).toString() == QStringLiteral("ACTIVE")) {
            station->addItem(item.value(QStringLiteral("name")).toString(),
                             item.value(QStringLiteral("stationId")).toInteger());
        }
    }
    auto *code = new QLineEdit(&dialog);
    code->setPlaceholderText(QStringLiteral("例如 PILE-D-01"));
    auto *type = new QComboBox(&dialog);
    type->addItem(QStringLiteral("快充"), QStringLiteral("FAST"));
    type->addItem(QStringLiteral("慢充"), QStringLiteral("SLOW"));
    auto *power = new QDoubleSpinBox(&dialog);
    power->setRange(0.1, 1000.0);
    power->setDecimals(1);
    power->setValue(60.0);
    connect(type, &QComboBox::currentIndexChanged, power, [type, power] {
        power->setValue(type->currentData().toString() == QStringLiteral("FAST") ? 60.0 : 7.0);
    });
    layout->addRow(QStringLiteral("所属站点"), station);
    layout->addRow(QStringLiteral("电桩编号"), code);
    layout->addRow(QStringLiteral("类型"), type);
    layout->addRow(QStringLiteral("额定功率（kW）"), power);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (station->currentIndex() == 0) {
        QMessageBox::warning(this, QStringLiteral("无法创建"), QStringLiteral("请选择有效的充电站。"));
        return;
    }
    const ServiceResult result = facade_->createPile({
        {QStringLiteral("stationId"), station->currentData().toLongLong()},
        {QStringLiteral("pileCode"), code->text().trimmed()},
        {QStringLiteral("pileType"), type->currentData().toString()},
        {QStringLiteral("ratedPowerKw"), power->value()},
    });
    if (!result.ok()) return showServiceError(result.code, result.message);
    expandStationAfterRefresh_ = station->currentData().toLongLong();
    refreshAll();
}

void AdminWindow::showStationDetails(qint64 stationId)
{
    const ServiceResult stationResult = facade_->listStations({}, {});
    const ServiceResult pileResult = facade_->listPiles(stationId);
    if (!stationResult.ok()) return showServiceError(stationResult.code, stationResult.message);
    if (!pileResult.ok()) return showServiceError(pileResult.code, pileResult.message);
    QJsonObject station;
    for (const QJsonValue &value : stationResult.data.value(QStringLiteral("items")).toArray()) {
        if (value.toObject().value(QStringLiteral("stationId")).toInteger() == stationId) station = value.toObject();
    }
    if (station.isEmpty()) return;
    QString text = QStringLiteral("站点：%1\nID：%2\n区域：%3\n地址：%4\n坐标：%5, %6\n电价：¥%7/度\n状态：%8\n可用/总数：%9/%10\n在线率：%11%\n\n所属电桩：")
        .arg(station.value(QStringLiteral("name")).toString()).arg(stationId)
        .arg(station.value(QStringLiteral("region")).toString()).arg(station.value(QStringLiteral("address")).toString())
        .arg(station.value(QStringLiteral("longitude")).toDouble(), 0, 'f', 6)
        .arg(station.value(QStringLiteral("latitude")).toDouble(), 0, 'f', 6)
        .arg(station.value(QStringLiteral("priceCentsPerKwh")).toInteger() / 100.0, 0, 'f', 2)
        .arg(stationStatusText(station.value(QStringLiteral("status")).toString()))
        .arg(station.value(QStringLiteral("availablePileCount")).toInteger())
        .arg(station.value(QStringLiteral("totalPileCount")).toInteger())
        .arg(station.value(QStringLiteral("onlineRatePercent")).toDouble(), 0, 'f', 0);
    for (const QJsonValue &value : pileResult.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject pile = value.toObject();
        text += QStringLiteral("\n• %1  %2  %3 kW  %4")
            .arg(pile.value(QStringLiteral("pileCode")).toString())
            .arg(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充"))
            .arg(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 0, 'f', 1)
            .arg(pileStatusText(pile.value(QStringLiteral("status")).toString()));
    }
    showDetails(QStringLiteral("站点详情"), text);
}

void AdminWindow::showPileDetails(qint64 pileId)
{
    const ServiceResult result = facade_->listPiles();
    if (!result.ok()) return showServiceError(result.code, result.message);
    for (const QJsonValue &value : result.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject pile = value.toObject();
        if (pile.value(QStringLiteral("pileId")).toInteger() != pileId) continue;
        showDetails(QStringLiteral("电桩详情"),
            QStringLiteral("编号：%1\nID：%2\n所属站点 ID：%3\n类型：%4\n额定功率：%5 kW\n状态：%6\n累计充电：%7 次\n累计时长：%8 小时")
                .arg(pile.value(QStringLiteral("pileCode")).toString()).arg(pileId)
                .arg(pile.value(QStringLiteral("stationId")).toInteger())
                .arg(pile.value(QStringLiteral("pileType")).toString() == QStringLiteral("FAST") ? QStringLiteral("快充") : QStringLiteral("慢充"))
                .arg(pile.value(QStringLiteral("ratedPowerKw")).toDouble(), 0, 'f', 1)
                .arg(pileStatusText(pile.value(QStringLiteral("status")).toString()))
                .arg(pile.value(QStringLiteral("chargeCount")).toInteger())
                .arg(pile.value(QStringLiteral("totalChargeSeconds")).toInteger() / 3600.0, 0, 'f', 1));
        return;
    }
}

void AdminWindow::showUserDetails(qint64 userId)
{
    const ServiceResult users = facade_->listUsers();
    const ServiceResult orders = facade_->listOrders();
    if (!users.ok()) return showServiceError(users.code, users.message);
    if (!orders.ok()) return showServiceError(orders.code, orders.message);
    QJsonObject user;
    for (const QJsonValue &value : users.data.value(QStringLiteral("items")).toArray()) {
        if (value.toObject().value(QStringLiteral("userId")).toInteger() == userId) user = value.toObject();
    }
    if (user.isEmpty()) return;
    int count = 0;
    qint64 spent = 0;
    for (const QJsonValue &value : orders.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject order = value.toObject();
        if (order.value(QStringLiteral("userId")).toInteger() == userId) {
            ++count;
            if (order.value(QStringLiteral("status")).toString() == QStringLiteral("COMPLETED")) spent += order.value(QStringLiteral("amountCents")).toInteger();
        }
    }
    showDetails(QStringLiteral("用户详情"),
        QStringLiteral("手机号：%1\n昵称：%2\n状态：%3\n余额：%4\n注册时间：%5\n用户 ID：%6\n\n订单总数：%7\n累计消费：%8")
            .arg(user.value(QStringLiteral("phone")).toString())
            .arg(user.value(QStringLiteral("nickname")).toString())
            .arg(userStatusText(user.value(QStringLiteral("status")).toString()))
            .arg(moneyText(user.value(QStringLiteral("balanceCents")).toInteger()))
            .arg(user.value(QStringLiteral("createdAt")).toString()).arg(userId).arg(count).arg(moneyText(spent)));
}

void AdminWindow::showOrderDetails(qint64 orderId)
{
    const ServiceResult result = facade_->listOrders();
    if (!result.ok()) return showServiceError(result.code, result.message);
    for (const QJsonValue &value : result.data.value(QStringLiteral("items")).toArray()) {
        const QJsonObject order = value.toObject();
        if (order.value(QStringLiteral("orderId")).toInteger() != orderId) continue;
        const auto timeText = [&order](const char *key) {
            const QJsonValue value = order.value(QLatin1String(key));
            return value.isString() ? value.toString() : QStringLiteral("—");
        };
        const QJsonValue unitPrice = order.value(QStringLiteral("unitPriceCentsPerKwh"));
        showDetails(QStringLiteral("订单详情"),
            QStringLiteral("订单号：%1\n状态：%2\n用户 ID：%3\n站点：%4（ID %5）\n电桩：%6（ID %7）\n模式：%8\n\n创建：%9\n预约：%10\n开始：%11\n结束：%12\n支付：%13\n\n时长：%14 分钟\n电量：%15 kWh\n价格快照：%16\n金额：%17")
                .arg(order.value(QStringLiteral("orderNo")).toString())
                .arg(orderStatusText(order.value(QStringLiteral("status")).toString()))
                .arg(order.value(QStringLiteral("userId")).toInteger())
                .arg(order.value(QStringLiteral("stationName")).toString())
                .arg(order.value(QStringLiteral("stationId")).toInteger())
                .arg(order.value(QStringLiteral("pileCode")).toString())
                .arg(order.value(QStringLiteral("pileId")).toInteger())
                .arg(order.value(QStringLiteral("mode")).toString() == QStringLiteral("DIRECT") ? QStringLiteral("直接充电") : QStringLiteral("预约"))
                .arg(timeText("createdAt")).arg(timeText("reservedAt")).arg(timeText("startedAt"))
                .arg(timeText("endedAt")).arg(timeText("paidAt"))
                .arg(order.value(QStringLiteral("durationSeconds")).toInteger() / 60)
                .arg(order.value(QStringLiteral("energyWh")).toInteger() / 1000.0, 0, 'f', 2)
                .arg(unitPrice.isDouble() ? QStringLiteral("¥%1/度").arg(unitPrice.toInteger() / 100.0, 0, 'f', 2) : QStringLiteral("—"))
                .arg(moneyText(order.value(QStringLiteral("amountCents")).toInteger())));
        return;
    }
}

void AdminWindow::deleteSelectedStation()
{
    QTreeWidgetItem *selected = stationsTable_->currentItem();
    if (selected == nullptr) {
        QMessageBox::information(this, QStringLiteral("删除站点"),
                                 QStringLiteral("请先选择一个充电站。"));
        return;
    }
    if (selected->parent() != nullptr) selected = selected->parent();
    const qint64 stationId = selected->data(0, Qt::UserRole).toLongLong();
    const QString stationName = selected->text(0);
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
        QMessageBox::information(this, QStringLiteral("重启电桩"), QStringLiteral("请先选择一个电桩。"));
        return;
    }
    const qint64 pileId = pilesTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const ServiceResult result = facade_->restartPile(pileId);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAll();
    QMessageBox::information(this, QStringLiteral("重启电桩"), QStringLiteral("电桩已恢复为空闲状态。"));
}

void AdminWindow::deleteSelectedPile()
{
    const int row = pilesTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("删除电桩"), QStringLiteral("请先选择一个电桩。"));
        return;
    }
    const qint64 pileId = pilesTable_->item(row, 0)->data(Qt::UserRole).toLongLong();
    const QString code = pilesTable_->item(row, 1)->text();
    if (QMessageBox::question(this, QStringLiteral("删除电桩"),
            QStringLiteral("确定删除电桩“%1”吗？\n存在订单或正在使用时不能删除。").arg(code),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const ServiceResult result = facade_->deletePile(pileId);
    if (!result.ok()) return showServiceError(result.code, result.message);
    refreshAll();
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
    Q_UNUSED(code)
    QString detail = message;
    if (code == ErrorCode::CurrentOrderExists) detail = QStringLiteral("该用户存在未结束订单，暂时不能冻结。");
    else if (code == ErrorCode::IllegalOrderState) detail = QStringLiteral("充电中或已预约的电桩不能重启。");
    else if (code == ErrorCode::InvalidRequest) detail = QStringLiteral("输入内容不完整或格式不正确。");
    if (message == QStringLiteral("INVALID_STATION")) detail = QStringLiteral("请选择一个已启用的充电站。");
    else if (message == QStringLiteral("PILE_CODE_EXISTS")) detail = QStringLiteral("电桩编号已存在，请使用其他编号。");
    QMessageBox::warning(this, QStringLiteral("操作失败"), detail);
}

void AdminWindow::prepareTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(false);
    if (!headers.isEmpty()) {
        table->horizontalHeader()->setSectionResizeMode(headers.size() - 1, QHeaderView::Fixed);
        table->setColumnWidth(headers.size() - 1, 132);
    }
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
