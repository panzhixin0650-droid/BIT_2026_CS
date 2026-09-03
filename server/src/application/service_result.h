#pragma once

#include <QJsonObject>
#include <QString>

#include <utility>

namespace charging::server {

// A small value type shared by ApplicationService and its future facades.
// Transport code converts it to the protocol ResponseEnvelope.
struct ServiceResult {
    int code = 0;
    QString message = QStringLiteral("OK");
    QJsonObject data;

    [[nodiscard]] bool ok() const noexcept { return code == 0; }

    [[nodiscard]] static ServiceResult success(QJsonObject payload = {})
    {
        return {0, QStringLiteral("OK"), std::move(payload)};
    }

    [[nodiscard]] static ServiceResult failure(int errorCode,
                                                QString errorMessage,
                                                QJsonObject payload = {})
    {
        return {errorCode, std::move(errorMessage), std::move(payload)};
    }
};

}  // namespace charging::server
