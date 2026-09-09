#pragma once

#include "assistant/assistant_config.h"
#include "charging/protocol/dto.h"
#include "local/map_types.h"

#include <QHash>
#include <QMainWindow>

#include <memory>
#include <optional>

class QPushButton;
class QFrame;
class QStackedWidget;
class QTabWidget;
class QTimer;

namespace charging::client {

class AssistantService;
class AvatarStorage;
class IChargingApi;
class IMapService;
class LoginController;
class LoginPage;
class SupportDeskPage;
class SupportPage;
class VehicleChargingPage;
class VehicleHomePage;
class VehicleProfilePage;

class VehicleMainWindow final : public QMainWindow {
    Q_OBJECT
public:
    VehicleMainWindow(IChargingApi &api, IMapService &mapService,
                      const AssistantConfig &assistantConfig = {}, QWidget *parent = nullptr);
    ~VehicleMainWindow() override;

private:
    enum class CurrentPurpose { None, Refresh, StartCheck, RechargeCheck };
    enum class DetailPurpose { None, SelectedStation, ResolveOrderPower };
    enum class GeocodePurpose { None, Location, RouteStart };

    void authenticated(const protocol::UserDto &user, bool isNewUser);
    void showLogin(const QString &message = {});
    void refreshHome();
    void refreshCharging();
    void refreshProfile();
    void startCharging(const QString &pileCode);
    void stopCharging();
    void openSupport();
    void openDesk(bool repair, bool tickets, const QString &pileCode = {});
    bool handleInvalidSession(int code);
    void updateHeader();
    void clearPending();

    IChargingApi &api_;
    IMapService &mapService_;
    AssistantConfig assistantConfig_;
    QStackedWidget *applicationPages_ = nullptr;
    LoginPage *loginPage_ = nullptr;
    LoginController *loginController_ = nullptr;
    QTabWidget *navigation_ = nullptr;
    VehicleHomePage *home_ = nullptr;
    VehicleChargingPage *charging_ = nullptr;
    VehicleProfilePage *profile_ = nullptr;
    SupportPage *support_ = nullptr;
    QWidget *supportContainer_ = nullptr;
    SupportDeskPage *desk_ = nullptr;
    AssistantService *assistant_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QFrame *header_ = nullptr;
    QPushButton *account_ = nullptr;
    QTimer *chargingTimer_ = nullptr;
    std::unique_ptr<AvatarStorage> avatarStorage_;
    protocol::UserDto user_;
    bool authenticated_ = false;
    quint64 sessionGeneration_ = 0;
    std::optional<protocol::OrderDto> currentOrder_;
    QString candidatePile_;
    double candidatePowerKw_ = 0.0;
    QHash<QString, double> ratedPowerByPile_;
    CurrentPurpose currentPurpose_ = CurrentPurpose::None;
    DetailPurpose detailPurpose_ = DetailPurpose::None;
    QString loginRequest_;
    QString stationListRequest_;
    QString stationDetailRequest_;
    QString historyRequest_;
    QString currentRequest_;
    QString actionRequest_;
    QString profileRequest_;
    QString ordersRequest_;
    QString finalHistoryRequest_;
    QString geocodeRequest_;
    QString routeRequest_;
    MapLocation pendingRouteEnd_;
    RouteMode pendingRouteMode_ = RouteMode::Driving;
    GeocodePurpose geocodePurpose_ = GeocodePurpose::None;
};

}  // namespace charging::client
