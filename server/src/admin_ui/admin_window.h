#pragma once

#include <QMainWindow>

class QComboBox;
class QDateEdit;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;

namespace charging::server {

class AdminFacade;
class RevenueChart;

class AdminWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit AdminWindow(AdminFacade *facade,
                         bool tcpListening,
                         quint16 tcpPort,
                         bool sqliteRepository,
                         QWidget *parent = nullptr);

private:
    QWidget *buildLoginPage();
    QWidget *buildApplicationPage();
    QWidget *buildDashboardPage();
    QWidget *buildStationsPage();
    QWidget *buildPilesPage();
    QWidget *buildUsersPage();
    QWidget *buildOrdersPage();

    void attemptLogin();
    void selectPage(int index);
    void refreshAll();
    void refreshDashboard();
    void refreshStations();
    void refreshPiles();
    void refreshUsers();
    void refreshOrders();
    void showCreateStationDialog();
    void showCreatePileDialog();
    void showStationDetails(qint64 stationId);
    void showPileDetails(qint64 pileId);
    void showUserDetails(qint64 userId);
    void showOrderDetails(qint64 orderId);
    void showDetails(const QString &title, const QString &content);
    void deleteSelectedStation();
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
    QListWidget *navigation_ = nullptr;
    QLabel *pageTitle_ = nullptr;
    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLabel *loginError_ = nullptr;
    QLabel *todayRevenue_ = nullptr;
    QLabel *monthRevenue_ = nullptr;
    QLabel *totalRevenue_ = nullptr;
    QLabel *resourceCount_ = nullptr;
    QLabel *idlePiles_ = nullptr;
    QLabel *inUsePiles_ = nullptr;
    QLabel *faultPiles_ = nullptr;
    QComboBox *dashboardDays_ = nullptr;
    QDateEdit *dashboardStartDate_ = nullptr;
    QDateEdit *dashboardEndDate_ = nullptr;
    QPushButton *dashboardApplyButton_ = nullptr;
    QDockWidget *detailsDock_ = nullptr;
    RevenueChart *revenueChart_ = nullptr;
    QLineEdit *stationSearch_ = nullptr;
    QComboBox *stationRegion_ = nullptr;
    QComboBox *stationStatus_ = nullptr;
    QTreeWidget *stationsTable_ = nullptr;
    QLineEdit *pileSearch_ = nullptr;
    QComboBox *pileStation_ = nullptr;
    QComboBox *pileStatus_ = nullptr;
    QTableWidget *pilesTable_ = nullptr;
    QLineEdit *userSearch_ = nullptr;
    QComboBox *userStatus_ = nullptr;
    QTableWidget *usersTable_ = nullptr;
    QLineEdit *orderSearch_ = nullptr;
    QComboBox *orderStatus_ = nullptr;
    QComboBox *orderMode_ = nullptr;
    QTableWidget *ordersTable_ = nullptr;
    QString appliedStationSearch_;
    QString appliedPileSearch_;
    QString appliedUserSearch_;
    QString appliedOrderSearch_;
    qint64 expandStationAfterRefresh_ = 0;
};

}  // namespace charging::server
