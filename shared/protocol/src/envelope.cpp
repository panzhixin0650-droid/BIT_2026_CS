#include "charging/protocol/envelope.h"

#include "charging/protocol/protocol_constants.h"

#include <QJsonValue>

#include <cmath>
#include <limits>

namespace charging::protocol {
namespace {

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

bool readInt(const QJsonObject &json, const QString &key, int *value, QString *error)
{
    const QJsonValue item = json.value(key);
    if (!item.isDouble()) {
        return fail(error, key + QStringLiteral(" must be an integer"));
    }
    const double number = item.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) {
        return fail(error, key + QStringLiteral(" must be an integer"));
    }
    *value = static_cast<int>(number);
    return true;
}

bool readString(const QJsonObject &json,
                const QString &key,
                QString *value,
                bool requireNonEmpty,
                QString *error)
{
    const QJsonValue item = json.value(key);
    if (!item.isString() || (requireNonEmpty && item.toString().isEmpty())) {
        return fail(error, key + QStringLiteral(" must be a non-empty string"));
    }
    *value = item.toString();
    return true;
}

bool readData(const QJsonObject &json, QJsonObject *data, QString *error)
{
    const QJsonValue item = json.value(QStringLiteral("data"));
    if (!item.isObject()) {
        return fail(error, QStringLiteral("data must be an object"));
    }
    *data = item.toObject();
    return true;
}

}  // namespace

QJsonObject RequestEnvelope::toJson() const
{
    QJsonObject json{
        {QStringLiteral("version"), version},
        {QStringLiteral("type"), type},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("data"), data},
    };
    if (token.has_value()) {
        json.insert(QStringLiteral("token"), *token);
    }
    return json;
}

bool RequestEnvelope::fromJson(const QJsonObject &json,
                               RequestEnvelope *result,
                               QString *error)
{
    if (result == nullptr) {
        return fail(error, QStringLiteral("result must not be null"));
    }

    RequestEnvelope parsed;
    if (!readInt(json, QStringLiteral("version"), &parsed.version, error)
        || !readString(json, QStringLiteral("type"), &parsed.type, true, error)
        || !readString(json, QStringLiteral("requestId"), &parsed.requestId, true, error)
        || !readData(json, &parsed.data, error)) {
        return false;
    }
    if (parsed.version != kProtocolVersion) {
        return fail(error, QStringLiteral("unsupported version"));
    }

    if (json.contains(QStringLiteral("token"))) {
        const QJsonValue token = json.value(QStringLiteral("token"));
        if (!token.isString()) {
            return fail(error, QStringLiteral("token must be a string when present"));
        }
        parsed.token = token.toString();
    }

    *result = parsed;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

QJsonObject ResponseEnvelope::toJson() const
{
    return {
        {QStringLiteral("version"), version},
        {QStringLiteral("type"), type},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("code"), code},
        {QStringLiteral("message"), message},
        {QStringLiteral("data"), data},
    };
}

bool ResponseEnvelope::fromJson(const QJsonObject &json,
                                ResponseEnvelope *result,
                                QString *error)
{
    if (result == nullptr) {
        return fail(error, QStringLiteral("result must not be null"));
    }

    ResponseEnvelope parsed;
    if (!readInt(json, QStringLiteral("version"), &parsed.version, error)
        || !readString(json, QStringLiteral("type"), &parsed.type, true, error)
        || !readString(json, QStringLiteral("requestId"), &parsed.requestId, true, error)
        || !readInt(json, QStringLiteral("code"), &parsed.code, error)
        || !readString(json, QStringLiteral("message"), &parsed.message, false, error)
        || !readData(json, &parsed.data, error)) {
        return false;
    }
    if (parsed.version != kProtocolVersion) {
        return fail(error, QStringLiteral("unsupported version"));
    }

    *result = parsed;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace charging::protocol
