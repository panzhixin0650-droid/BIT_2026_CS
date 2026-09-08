#pragma once

#include "api/api_result.h"
#include "local/map_types.h"

#include <QList>
#include <QWidget>

#include <optional>

class QLabel;
class QComboBox;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace charging::client {

class RouteMapView;
class StationMapView;

class VehicleHomePage final : public QWidget {
    Q_OBJECT
public:
    explicit VehicleHomePage(const QUrl &mapScriptUrl, QWidget *parent = nullptr);

    [[nodiscard]] StationQuery stationQuery() const;
    [[nodiscard]] MapLocation currentLocation() const { return location_; }
    void setUser(const protocol::UserDto &user);
    void setLoading(bool loading);
    void showStations(const QList<protocol::StationDto> &stations);
    void showVisitHistory(const QList<protocol::OrderDto> &orders);
    void showStationDetail(const StationDetailPayload &detail);
    void showMessage(const QString &message, bool error = false);
    void setResolvedLocation(const MapLocation &location);
    void showRoute(const RouteResult &route);
    void showRouteMessage(const QString &message, bool error = false);
    void reset();

signals:
    void refreshRequested();
    void stationSelected(qint64 stationId);
    void reserveRequested(const QString &pileCode);
    void chargeRequested(const QString &pileCode, double ratedPowerKw);
    void locationRequested(const QString &address);
    void routeRequested(const MapLocation &start, const MapLocation &end, RouteMode mode);

private:
    void renderDiscovery();
    void showDiscovery();
    void showRoutePlanner();
    void clearLayout(QVBoxLayout *layout);

    StationMapView *stationMap_ = nullptr;
    RouteMapView *routeMap_ = nullptr;
    QStackedWidget *mapStack_ = nullptr;
    QStackedWidget *panelStack_ = nullptr;
    QWidget *discoveryPanel_ = nullptr;
    QWidget *detailPanel_ = nullptr;
    QWidget *routePanel_ = nullptr;
    QVBoxLayout *stationList_ = nullptr;
    QVBoxLayout *pileList_ = nullptr;
    QLabel *greeting_ = nullptr;
    QLabel *message_ = nullptr;
    QLabel *detailTitle_ = nullptr;
    QLabel *detailBody_ = nullptr;
    QLabel *routeDestination_ = nullptr;
    QLabel *routeMessage_ = nullptr;
    QLineEdit *search_ = nullptr;
    QLineEdit *locationInput_ = nullptr;
    QLineEdit *routeStart_ = nullptr;
    QComboBox *routeMode_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QList<protocol::StationDto> stations_;
    QList<protocol::OrderDto> orders_;
    protocol::StationDto selectedStation_;
    MapLocation location_{QStringLiteral("演示位置"), 123.42, 41.70};
    bool hasOnlineMapCanvas_ = false;
};

class VehicleChargingPage final : public QWidget {
    Q_OBJECT
public:
    explicit VehicleChargingPage(QWidget *parent = nullptr);
    void prepare(const QString &pileCode, double ratedPowerKw = 0.0);
    void showOrder(const protocol::OrderDto &order);
    void showNoOrder();
    void setBusy(bool busy);
    void showMessage(const QString &message, bool error = false);
    [[nodiscard]] QString pileCode() const { return pileCode_; }
    [[nodiscard]] std::optional<protocol::OrderDto> order() const { return order_; }
    void reset();

signals:
    void refreshRequested();
    void startRequested(const QString &pileCode);
    void stopRequested();
    void cancelRequested(qint64 orderId);
    void payRequested(qint64 orderId);
    void rechargeRequested();
    void ordersRequested();
    void repairRequested(const QString &pileCode);

private:
    void render();
    QString pileCode_;
    double ratedPowerKw_ = 0.0;
    std::optional<protocol::OrderDto> order_;
    bool busy_ = false;
    QLabel *state_ = nullptr;
    QLabel *station_ = nullptr;
    QLabel *message_ = nullptr;
    QLabel *power_ = nullptr;
    QLabel *energy_ = nullptr;
    QLabel *duration_ = nullptr;
    QLabel *amount_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *stop_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QPushButton *pay_ = nullptr;
    QPushButton *recharge_ = nullptr;
    QPushButton *orders_ = nullptr;
    QPushButton *repair_ = nullptr;
};

class VehicleProfilePage final : public QWidget {
    Q_OBJECT
public:
    explicit VehicleProfilePage(QWidget *parent = nullptr);
    void setUser(const protocol::UserDto &user, const QString &avatarPath = {});
    void setBusy(bool busy);
    void showOrders(const QList<protocol::OrderDto> &orders);
    void showMessage(const QString &message, bool error = false);
    void showOverview();
    void showOrderList();
    void reset();

signals:
    void refreshRequested();
    void nicknameRequested(const QString &nickname);
    void rechargeRequested(const QString &amountYuan);
    void ordersRequested();
    void supportRequested();
    void repairRequested();
    void ticketsRequested();
    void logoutRequested();

private:
    QStackedWidget *sections_ = nullptr;
    QWidget *overview_ = nullptr;
    QWidget *ordersPage_ = nullptr;
    QLabel *avatar_ = nullptr;
    QLabel *nickname_ = nullptr;
    QLabel *phone_ = nullptr;
    QLabel *balance_ = nullptr;
    QLabel *message_ = nullptr;
    QLineEdit *nicknameInput_ = nullptr;
    QLineEdit *rechargeInput_ = nullptr;
    QListWidget *ordersList_ = nullptr;
    QPushButton *saveNickname_ = nullptr;
    QPushButton *rechargeButton_ = nullptr;
    QList<QPushButton *> actions_;
};

}  // namespace charging::client
