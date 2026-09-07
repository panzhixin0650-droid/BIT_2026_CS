#pragma once

#include <QMainWindow>
#include <QDate>
#include <QList>
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
class RevenueChart;
class PileStatusChart;

class AdminWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit AdminWindow(AdminFacade *facade,
                         bool tcpListening,
                         quint16 tcpPort,
                         bool sqliteRepository,
                         QWidget *parent = nullptr);

private:
    struct PageState {
        int pageIndex = 0;
        QString stationSearch;
        int stationSearchField = 2;
        QSet<QString> stationRegions;
        QSet<QString> stationStatuses;
        QSet<qint64> expandedStations;
        QString pileSearch;
        int pileSearchField = 2;
        QSet<qint64> pileStations;
        QSet<QString> pileStatuses;
        QString userSearch;
        int userSearchField = 2;
        QSet<QString> userStatuses;
        QString orderSearch;
        int orderSearchField = 3;
        QSet<QString> orderStatuses;
        QSet<QString> orderModes;
        int dashboardDays = 7;
        QDate dashboardStartDate;
        QDate dashboardEndDate;
        qint64 selectedStationId = 0;
        qint64 selectedPileId = 0;
        qint64 selectedUserId = 0;
        qint64 selectedOrderId = 0;
    };

    QWidget *buildLoginPage();
    QWidget *buildApplicationPage();
    QWidget *buildDashboardPage();
    QWidget *buildOperationsPage();
    QWidget *buildStationsPage();
    QWidget *buildPilesPage();
    QWidget *buildUsersPage();
    QWidget *buildOrdersPage();

    void attemptLogin();
    void setLoginError(const QString &message);
    void selectPage(int index);
    void refreshAll();
    // Reset only the current page's view state and reload its data. This is
    // deliberately separate from refreshAll(): browser-style refresh must not
    // touch navigation history or refresh unrelated pages.
    void refreshCurrentPage();
    void refreshDashboard();
    void refreshOperations();
    void refreshStations();
    void refreshPiles();
    void refreshUsers();
    void refreshOrders();
    void showCreateStationDialog();
    void showEditStationDialog(qint64 stationId);
    void showCreatePileDialog(qint64 fixedStationId = 0);
    void showEditPileDialog(qint64 pileId);
    void navigateToPile(qint64 pileId, qint64 stationId);
    void navigateToStationPiles(qint64 stationId);
    void navigateToPileStatus(const QString &statusKey);
    void navigateBack();
    void navigateForward();
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

    AdminFacade *facade_ = nullptr;
    bool tcpListening_ = false;
    quint16 tcpPort_ = 0;
    bool sqliteRepository_ = false;
    QStackedWidget *rootStack_ = nullptr;
    QStackedWidget *contentStack_ = nullptr;
    QToolButton *backButton_ = nullptr;
    QToolButton *refreshButton_ = nullptr;
    QToolButton *forwardButton_ = nullptr;
    QListWidget *navigation_ = nullptr;
    QLabel *pageTitle_ = nullptr;
    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLabel *loginError_ = nullptr;
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
    QString appliedStationSearch_;
    QString appliedPileSearch_;
    QString appliedUserSearch_;
    QString appliedOrderSearch_;
    QSet<QString> selectedStationRegions_;
    QSet<QString> selectedStationStatuses_;
    QSet<qint64> selectedPileStations_;
    QSet<QString> selectedPileStatuses_;
    QSet<QString> selectedUserStatuses_;
    QSet<QString> selectedOrderStatuses_;
    QSet<QString> selectedOrderModes_;
    qint64 expandStationAfterRefresh_ = 0;
    qint64 focusPileAfterRefresh_ = 0;
    QTimer *stationClickTimer_ = nullptr;
    QTreeWidgetItem *pendingStationClick_ = nullptr;
    QList<PageState> backHistory_;
    QList<PageState> forwardHistory_;
    bool historyReady_ = false;
    bool restoringHistory_ = false;
    bool skipNextNavigationHistory_ = false;
    bool restoreExpandedStationsPending_ = false;
    QSet<qint64> pendingExpandedStations_;
};

}  // namespace charging::server
