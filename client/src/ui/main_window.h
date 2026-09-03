#pragma once

#include "charging/protocol/dto.h"

#include <QMainWindow>

class QLabel;
class QStackedWidget;

namespace charging::client {

class IChargingApi;
class LoginController;
class LoginPage;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(IChargingApi &api, QWidget *parent = nullptr);

private:
    void showAuthenticatedHome(const protocol::UserDto &user, bool isNewUser);

    QStackedWidget *pages_ = nullptr;
    LoginPage *loginPage_ = nullptr;
    QWidget *homePage_ = nullptr;
    QLabel *welcomeLabel_ = nullptr;
    QLabel *loginNoticeLabel_ = nullptr;
    LoginController *loginController_ = nullptr;
};

}  // namespace charging::client
