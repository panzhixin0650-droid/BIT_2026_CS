#pragma once

#include "charging/protocol/dto.h"

#include <QMainWindow>

#include <memory>

class QStackedWidget;
class QTabWidget;

namespace charging::client {

class IChargingApi;
class AvatarStorage;
class LoginController;
class LoginPage;
class OrderController;
class OrderPage;
class ProfileController;
class ProfilePage;
class ScanController;
class ScanPage;
class StationBrowserController;
class StationBrowserPage;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(IChargingApi &api, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser);
    void showLoginPage(const QString &message = {});

    QStackedWidget *pages_ = nullptr;
    LoginPage *loginPage_ = nullptr;
    QTabWidget *mainTabs_ = nullptr;
    StationBrowserPage *homePage_ = nullptr;
    OrderPage *orderPage_ = nullptr;
    ScanPage *scanPage_ = nullptr;
    ProfilePage *profilePage_ = nullptr;
    LoginController *loginController_ = nullptr;
    ProfileController *profileController_ = nullptr;
    StationBrowserController *stationBrowserController_ = nullptr;
    OrderController *orderController_ = nullptr;
    ScanController *scanController_ = nullptr;
    std::unique_ptr<AvatarStorage> avatarStorage_;
};

}  // namespace charging::client
