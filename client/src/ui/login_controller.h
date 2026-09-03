#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class LoginPage;

class LoginController final : public QObject {
    Q_OBJECT

public:
    LoginController(LoginPage &page, IChargingApi &api, QObject *parent = nullptr);

signals:
    void loginSucceeded(const charging::protocol::UserDto &user, bool isNewUser);

private:
    void submitLogin(const QString &phone);
    void handleLoginCompleted(const LoginResult &result);
    [[nodiscard]] QString errorMessage(const ApiResponse &response) const;

    LoginPage &page_;
    IChargingApi &api_;
    QString pendingRequestId_;
};

}  // namespace charging::client
