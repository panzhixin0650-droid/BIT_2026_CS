#pragma once

#include <QMainWindow>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTableWidget;

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
    void deleteSelectedStation();
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
    RevenueChart *revenueChart_ = nullptr;
    QLineEdit *stationSearch_ = nullptr;
    QComboBox *stationRegion_ = nullptr;
    QTableWidget *stationsTable_ = nullptr;
    QTableWidget *pilesTable_ = nullptr;
    QLineEdit *userSearch_ = nullptr;
    QTableWidget *usersTable_ = nullptr;
    QTableWidget *ordersTable_ = nullptr;
};

}  // namespace charging::server
