#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace charging::protocol {

struct RequestEnvelope {
    int version = 1;
    QString type;
    QString requestId;
    std::optional<QString> token;
    QJsonObject data;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject &json,
                                       RequestEnvelope *result,
                                       QString *error = nullptr);
};

struct ResponseEnvelope {
    int version = 1;
    QString type;
    QString requestId;
    int code = 0;
    QString message;
    QJsonObject data;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject &json,
                                       ResponseEnvelope *result,
                                       QString *error = nullptr);
};

}  // namespace charging::protocol
