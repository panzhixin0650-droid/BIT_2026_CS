#pragma once

#include "charging/protocol/dto.h"

#include <QMainWindow>

#include <memory>

class QLabel;
class QStackedWidget;
class QTabWidget;

namespace charging::client {

class IChargingApi;
class AvatarStorage;
class LoginController;
class LoginPage;
class ProfileController;
class ProfilePage;

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
    QWidget *homePage_ = nullptr;
    QLabel *welcomeLabel_ = nullptr;
    QLabel *loginNoticeLabel_ = nullptr;
    ProfilePage *profilePage_ = nullptr;
    LoginController *loginController_ = nullptr;
    ProfileController *profileController_ = nullptr;
    std::unique_ptr<AvatarStorage> avatarStorage_;
};

}  // namespace charging::client
