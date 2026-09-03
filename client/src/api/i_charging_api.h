#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi : public QObject {
    Q_OBJECT

public:
    explicit IChargingApi(QObject *parent = nullptr);
    ~IChargingApi() override;

    [[nodiscard]] virtual QString loginUser(const QString &phone) = 0;
    [[nodiscard]] virtual QString logout() = 0;
    [[nodiscard]] virtual QString getProfile() = 0;
    [[nodiscard]] virtual QString updateNickname(const QString &nickname) = 0;

signals:
    void loginCompleted(const charging::client::LoginResult &result);
    void logoutCompleted(const charging::client::LogoutResult &result);
    void profileCompleted(const charging::client::UserResult &result);
    void profileUpdateCompleted(const charging::client::UserResult &result);
};

}  // namespace charging::client
