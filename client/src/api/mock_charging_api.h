#pragma once

#include "api/i_charging_api.h"

#include <QHash>

#include <optional>

namespace charging::client {

class MockChargingApi final : public IChargingApi {
    Q_OBJECT

public:
    explicit MockChargingApi(QObject *parent = nullptr);

    [[nodiscard]] QString loginUser(const QString &phone) override;
    [[nodiscard]] QString logout() override;
    [[nodiscard]] QString getProfile() override;
    [[nodiscard]] QString updateNickname(const QString &nickname) override;

private:
    [[nodiscard]] QString nextRequestId();
    [[nodiscard]] ApiResponse response(const QString &requestId,
                                       const char *type,
                                       int code,
                                       const QString &message) const;
    [[nodiscard]] std::optional<protocol::UserDto> authenticatedUser() const;

    QHash<QString, protocol::UserDto> usersByPhone_;
    QString authenticatedPhone_;
    QString token_;
    qint64 nextUserId_ = 2;
    quint64 requestSequence_ = 0;
};

}  // namespace charging::client
