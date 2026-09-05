#pragma once

#include "charging/protocol/dto.h"
#include "assistant/assistant_config.h"

#include <QMainWindow>

#include <memory>

class QStackedWidget;
class QTabWidget;

namespace charging::client {

class IChargingApi;
class IMapService;
class AvatarStorage;
class LoginController;
class LoginPage;
class MapController;
class OrderController;
class OrderPage;
class ProfileController;
class ProfilePage;
class ScanController;
class ScanPage;
class StationBrowserController;
class StationBrowserPage;
class AssistantService;
class SupportPage;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(IChargingApi &api, QWidget *parent = nullptr);
    MainWindow(IChargingApi &api,
               IMapService &mapService,
               QWidget *parent = nullptr);
    MainWindow(IChargingApi &api, IMapService &mapService,
               const AssistantConfig &assistantConfig, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void initialize(IChargingApi &api, IMapService &mapService,
                    const AssistantConfig &assistantConfig = {});
    void showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser);
    void showLoginPage(const QString &message = {});

    QStackedWidget *pages_ = nullptr;
    LoginPage *loginPage_ = nullptr;
    QTabWidget *mainTabs_ = nullptr;
    StationBrowserPage *homePage_ = nullptr;
    OrderPage *orderPage_ = nullptr;
    ScanPage *scanPage_ = nullptr;
    AssistantService *assistantService_ = nullptr;
    SupportPage *supportPage_ = nullptr;
    ProfilePage *profilePage_ = nullptr;
    LoginController *loginController_ = nullptr;
    MapController *mapController_ = nullptr;
    ProfileController *profileController_ = nullptr;
    StationBrowserController *stationBrowserController_ = nullptr;
    OrderController *orderController_ = nullptr;
    ScanController *scanController_ = nullptr;
    std::unique_ptr<AvatarStorage> avatarStorage_;
    std::unique_ptr<IMapService> ownedMapService_;
};

}  // namespace charging::client
