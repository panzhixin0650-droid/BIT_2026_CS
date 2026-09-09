// 服务端管理台主窗口声明：登录、各业务页面与导航历史
#pragma once

#include <QMainWindow>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QJsonObject>
#include <QSet>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace charging::server {

class AdminFacade;
class SupportTicketsPage;
class RevenueChart;
class AnalysisBarChart;
class PileStatusChart;

// 管理端主窗口，聚合仪表盘、站桩、用户、订单等页面
class AdminWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit AdminWindow(AdminFacade *facade,
                         bool tcpListening,
                         quint16 tcpPort,
                         bool sqliteRepository,
                         QWidget *parent = nullptr);

private:
    // PageState 保存某一页的筛选与选中项，用于前进后退还原
    struct PageState {
        int pageIndex = 0;
        int analysisScroll = 0;
        int stationOccupancy = -1;
        QString stationSearch;
        int stationSearchField = 2;
        QSet<QString> stationRegions;
        QSet<QString> stationStatuses;
        QSet<qint64> expandedStations;
        QString pileSearch;
        int pileSearchField = 2;
        QSet<qint64> pileStations;
        QSet<QString> pileStatuses;
        bool pilesActiveStationsOnly = false;
        QString userSearch;
        int userSearchField = 2;
        QSet<QString> userStatuses;
        QString orderSearch;
        int orderSearchField = 3;
        QSet<QString> orderStatuses;
        QSet<QString> orderModes;
        QSet<qint64> orderStations;
        int orderStartPeriod = -1;
        int orderTimeHours = 0;
        QDateTime orderStartTime;
        QDateTime orderEndTime;
        int dashboardDays = 7;
        QDate dashboardStartDate;
        QDate dashboardEndDate;
        qint64 selectedStationId = 0;
        qint64 selectedStationPileId = 0;
        qint64 selectedTicketId = 0;
        qint64 selectedPileId = 0;
        qint64 selectedUserId = 0;
        qint64 selectedOrderId = 0;
        QString adminSearch;
        QSet<QString> adminStatuses;
        QSet<QString> adminRoles;
        qint64 selectedAdminId = 0;
    };

    // 以下为各页面的构建函数，分别返回对应的页面控件
    QWidget *buildLoginPage();
    QWidget *buildApplicationPage();
    QWidget *buildDashboardPage();
    QWidget *buildOperationsPage();
    QWidget *buildStationsPage();
    QWidget *buildPilesPage();
    QWidget *buildUsersPage();
    QWidget *buildOrdersPage();
    QWidget *buildAdminsPage();

    // 登录、切页与整体刷新的入口
    void attemptLogin();
    void setLoginError(const QString &message);
    void selectPage(int index);
    void refreshAll();
    // Reset only the current page's view state and reload its data. This is
    // deliberately separate from refreshAll(): browser-style refresh must not
    // touch navigation history or refresh unrelated pages.
    void refreshCurrentPage();
    void refreshDashboard();
    void playAnalysisIntro(int page);
    void wireAnalysisActions();
    void navigateToAnalysisPage(const PageState &state);
    void openRevenueOrders(const QDate &start, const QDate &end, qint64 stationId = 0,
                           const QString &stationName = {}, const QString &mode = {}, int startPeriod = -1);
    void openAnalysisPiles(qint64 stationId = 0, const QSet<QString> &statuses = {}, bool activeOnly = false);
    QWidget *refreshFeedback_ = nullptr;
    QSet<int> visitedAnalysisPages_;
    // 各业务页的数据刷新函数，按当前筛选条件重新加载
    void refreshOperations();
    void refreshStations();
    void refreshPiles();
    void refreshUsers();
    void refreshOrders();
    void resetOrderTimeFilter();
    void showOrderTimeFilter();
    void updateOrderTimeFilterButton();
    void refreshAdmins();
    void applyAdminPermissions(const QJsonObject &admin);
    bool showChangePasswordDialog(bool required);
    void showAdminDetails(qint64 adminId);
    void showCreateAdminDialog();
    void showEditAdminDialog(qint64 adminId);
    // 新增/编辑对话框与跨页跳转，用于定位到具体站或桩
    void showCreateStationDialog();
    void showEditStationDialog(qint64 stationId);
    void showCreatePileDialog(qint64 fixedStationId = 0);
    void showEditPileDialog(qint64 pileId);
    void navigateToPile(qint64 pileId, qint64 stationId);
    void navigateToTicketPile(const QString &pileCode);
    void navigateToPileStation(qint64 pileId);
    void navigateToStationPiles(qint64 stationId);
    void navigateToPileStatus(const QString &statusKey);
    void navigateBack();
    void navigateForward();
    // 记录并还原页面状态，实现浏览器式的前进后退
    [[nodiscard]] PageState capturePageState() const;
    void restorePageState(const PageState &state);
    void pushNavigationHistory();
    void updateNavigationButtons();
    void showStationDetails(qint64 stationId);
    void showPileDetails(qint64 pileId);
    void showUserDetails(qint64 userId);
    void showOrderDetails(qint64 orderId);
    void showDetails(const QString &title, const QString &content);
    void deleteSelectedStation();
    void toggleStationStatus(qint64 stationId, bool currentlyActive);
    void deleteSelectedPile();
    void restartSelectedPile();
    void toggleSelectedUserStatus();
    void showServiceError(int code, const QString &message);

    static void prepareTable(QTableWidget *table, const QStringList &headers);
    static QString moneyText(qint64 cents);

    // 以下为窗口持有的门面指针与各控件成员
    AdminFacade *facade_ = nullptr;
    bool tcpListening_ = false;
    quint16 tcpPort_ = 0;
    bool sqliteRepository_ = false;
    QStackedWidget *rootStack_ = nullptr;
    QStackedWidget *contentStack_ = nullptr;
    SupportTicketsPage *supportTicketsPage_ = nullptr;
    QToolButton *backButton_ = nullptr;
    QToolButton *refreshButton_ = nullptr;
    QToolButton *forwardButton_ = nullptr;
    QListWidget *navigation_ = nullptr;
    QLabel *dashboardPeriod_ = nullptr;
    QDate revenueStartDate_;
    QDate revenueEndDate_;
    QDate revenueSnapshotDate_;
    QWidget *dashboardCustomRange_ = nullptr;
    QLabel *pageTitle_ = nullptr;
    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLabel *loginError_ = nullptr;
    QLabel *accountIdentity_ = nullptr;
    QLabel *todayRevenue_ = nullptr;
    QLabel *monthRevenue_ = nullptr;
    QLabel *totalRevenue_ = nullptr;
    QLabel *resourceCount_ = nullptr;
    QComboBox *dashboardDays_ = nullptr;
    QDateEdit *dashboardStartDate_ = nullptr;
    QDateEdit *dashboardEndDate_ = nullptr;
    QLabel *dashboardStartLabel_ = nullptr;
    QLabel *dashboardEndLabel_ = nullptr;
    QPushButton *dashboardApplyButton_ = nullptr;
    RevenueChart *revenueChart_ = nullptr;
    RevenueChart *paidOrdersChart_ = nullptr;
    RevenueChart *energyChart_ = nullptr;
    AnalysisBarChart *stationRevenueChart_ = nullptr;
    PileStatusChart *modeRevenueChart_ = nullptr;
    AnalysisBarChart *startPeriodChart_ = nullptr;
    QLabel *rangeRevenue_ = nullptr;
    QLabel *rangeOrders_ = nullptr;
    QLabel *rangeEnergy_ = nullptr;
    QLabel *rangeAverage_ = nullptr;
    QLabel *analysisSummary_ = nullptr;
    QLabel *operationsStations_ = nullptr;
    QLabel *operationsIdle_ = nullptr;
    QLabel *operationsInUse_ = nullptr;
    QLabel *operationsAbnormal_ = nullptr;
    QLabel *operationsSummary_ = nullptr;
    QLabel *operationsClock_ = nullptr;
    PileStatusChart *stationOccupancyChart_ = nullptr;
    AnalysisBarChart *stationFaultChart_ = nullptr;
    PileStatusChart *orderStatesChart_ = nullptr;
    PileStatusChart *pileStatusChart_ = nullptr;
    QTableWidget *operationsTable_ = nullptr;
    QLineEdit *stationSearch_ = nullptr;
    QComboBox *stationSearchField_ = nullptr;
    QComboBox *stationRegion_ = nullptr;
    QComboBox *stationStatus_ = nullptr;
    QPushButton *stationRegionFilter_ = nullptr;
    QPushButton *stationStatusFilter_ = nullptr;
    QTreeWidget *stationsTable_ = nullptr;
    QPushButton *stationExpandToggle_ = nullptr;
    QLineEdit *pileSearch_ = nullptr;
    QComboBox *pileSearchField_ = nullptr;
    QComboBox *pileStation_ = nullptr;
    QComboBox *pileStatus_ = nullptr;
    QPushButton *pileStationFilter_ = nullptr;
    QPushButton *pileStatusFilter_ = nullptr;
    QTableWidget *pilesTable_ = nullptr;
    QLineEdit *userSearch_ = nullptr;
    QComboBox *userSearchField_ = nullptr;
    QComboBox *userStatus_ = nullptr;
    QPushButton *userStatusFilter_ = nullptr;
    QTableWidget *usersTable_ = nullptr;
    QLineEdit *orderSearch_ = nullptr;
    QComboBox *orderSearchField_ = nullptr;
    QComboBox *orderStatus_ = nullptr;
    QComboBox *orderMode_ = nullptr;
    QPushButton *orderStatusFilter_ = nullptr;
    QPushButton *orderModeFilter_ = nullptr;
    QTableWidget *ordersTable_ = nullptr;
    QComboBox *orderStation_ = nullptr;
    QPushButton *orderStationFilter_ = nullptr;
    QPushButton *orderPeriodFilter_ = nullptr;
    QSet<qint64> selectedOrderStations_;
    int orderStartPeriod_ = -1;
    QPushButton *orderTimeFilter_ = nullptr;
    int orderTimeHours_ = 0; // 0: unrestricted; -1: custom; positive: rolling hours.
    QDateTime orderStartTime_;
    QDateTime orderEndTime_;
    QLineEdit *adminSearch_ = nullptr;
    QComboBox *adminStatus_ = nullptr;
    QComboBox *adminRole_ = nullptr;
    QPushButton *adminStatusFilter_ = nullptr;
    QPushButton *adminRoleFilter_ = nullptr;
    QTableWidget *adminsTable_ = nullptr;
    QString appliedStationSearch_;
    QString appliedPileSearch_;
    QString appliedUserSearch_;
    QString appliedOrderSearch_;
    QString appliedAdminSearch_;
    QSet<QString> selectedStationRegions_;
    QSet<QString> selectedStationStatuses_;
    int stationOccupancy_ = -1;
    QPushButton *stationOccupancyFilter_ = nullptr;
    QSet<qint64> selectedPileStations_;
    QSet<QString> selectedPileStatuses_;
    bool pilesActiveStationsOnly_ = false;
    QPushButton *pileActiveScope_ = nullptr;
    QSet<QString> selectedUserStatuses_;
    QSet<QString> selectedOrderStatuses_;
    QSet<QString> selectedOrderModes_;
    QSet<QString> selectedAdminStatuses_;
    QSet<QString> selectedAdminRoles_;
    // 当前登录管理员的身份信息，用于按角色控制可用操作
    qint64 currentAdminId_ = 0;
    QString currentAdminRole_;
    bool currentAdminAccountsAvailable_ = false;
    QPushButton *changePasswordButton_ = nullptr;
    qint64 expandStationAfterRefresh_ = 0;
    qint64 focusPileAfterRefresh_ = 0;
    QTimer *stationClickTimer_ = nullptr;
    QTreeWidgetItem *pendingStationClick_ = nullptr;
    // 前进后退历史栈及其还原过程中的标志位
    QList<PageState> backHistory_;
    QList<PageState> forwardHistory_;
    bool historyReady_ = false;
    bool restoringHistory_ = false;
    bool skipNextNavigationHistory_ = false;
    bool restoreExpandedStationsPending_ = false;
    QSet<qint64> pendingExpandedStations_;
};

}  // namespace charging::server
