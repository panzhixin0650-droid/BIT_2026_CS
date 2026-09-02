#include "charging/protocol/dto.h"

#include <QJsonValue>

#include <cmath>

namespace charging::protocol {
namespace {

constexpr double kMaxSafeJsonInteger = 9007199254740991.0;

bool fail(QString *error, const QString &field, const QString &expectation)
{
    if (error != nullptr) {
        *error = field + QStringLiteral(" ") + expectation;
    }
    return false;
}

bool readString(const QJsonObject &json, const char *field, QString *value, QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isString()) {
        return fail(error, key, QStringLiteral("must be a string"));
    }
    *value = item.toString();
    return true;
}

bool readBool(const QJsonObject &json, const char *field, bool *value, QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isBool()) {
        return fail(error, key, QStringLiteral("must be a boolean"));
    }
    *value = item.toBool();
    return true;
}

bool readDouble(const QJsonObject &json, const char *field, double *value, QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isDouble() || !std::isfinite(item.toDouble())) {
        return fail(error, key, QStringLiteral("must be a finite number"));
    }
    *value = item.toDouble();
    return true;
}

bool readInteger(const QJsonObject &json, const char *field, qint64 *value, QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isDouble()) {
        return fail(error, key, QStringLiteral("must be an integer"));
    }
    const double number = item.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || std::abs(number) > kMaxSafeJsonInteger) {
        return fail(error, key, QStringLiteral("must be a safe JSON integer"));
    }
    *value = static_cast<qint64>(number);
    return true;
}

bool readNullableString(const QJsonObject &json,
                        const char *field,
                        std::optional<QString> *value,
                        QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (item.isNull()) {
        value->reset();
        return true;
    }
    if (!item.isString()) {
        return fail(error, key, QStringLiteral("must be a string or null"));
    }
    *value = item.toString();
    return true;
}

bool readNullableDouble(const QJsonObject &json,
                        const char *field,
                        std::optional<double> *value,
                        QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (item.isNull()) {
        value->reset();
        return true;
    }
    if (!item.isDouble() || !std::isfinite(item.toDouble())) {
        return fail(error, key, QStringLiteral("must be a finite number or null"));
    }
    *value = item.toDouble();
    return true;
}

bool readNullableInteger(const QJsonObject &json,
                         const char *field,
                         std::optional<qint64> *value,
                         QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (item.isNull()) {
        value->reset();
        return true;
    }
    qint64 integer = 0;
    if (!readInteger(json, field, &integer, error)) {
        return false;
    }
    *value = integer;
    return true;
}

bool parseEnum(const QString &text, UserStatus *value)
{
    if (text == QStringLiteral("ACTIVE")) {
        *value = UserStatus::Active;
        return true;
    }
    if (text == QStringLiteral("FROZEN")) {
        *value = UserStatus::Frozen;
        return true;
    }
    return false;
}

bool parseEnum(const QString &text, StationStatus *value)
{
    if (text == QStringLiteral("ACTIVE")) {
        *value = StationStatus::Active;
        return true;
    }
    if (text == QStringLiteral("DISABLED")) {
        *value = StationStatus::Disabled;
        return true;
    }
    return false;
}

bool parseEnum(const QString &text, PileType *value)
{
    if (text == QStringLiteral("FAST")) {
        *value = PileType::Fast;
        return true;
    }
    if (text == QStringLiteral("SLOW")) {
        *value = PileType::Slow;
        return true;
    }
    return false;
}

bool parseEnum(const QString &text, PileStatus *value)
{
    if (text == QStringLiteral("IDLE")) {
        *value = PileStatus::Idle;
        return true;
    }
    if (text == QStringLiteral("RESERVED")) {
        *value = PileStatus::Reserved;
        return true;
    }
    if (text == QStringLiteral("CHARGING")) {
        *value = PileStatus::Charging;
        return true;
    }
    if (text == QStringLiteral("FAULT")) {
        *value = PileStatus::Fault;
        return true;
    }
    if (text == QStringLiteral("OFFLINE")) {
        *value = PileStatus::Offline;
        return true;
    }
    return false;
}

bool parseEnum(const QString &text, OrderMode *value)
{
    if (text == QStringLiteral("RESERVATION")) {
        *value = OrderMode::Reservation;
        return true;
    }
    if (text == QStringLiteral("DIRECT")) {
        *value = OrderMode::Direct;
        return true;
    }
    return false;
}

bool parseEnum(const QString &text, OrderStatus *value)
{
    if (text == QStringLiteral("RESERVED")) {
        *value = OrderStatus::Reserved;
        return true;
    }
    if (text == QStringLiteral("CHARGING")) {
        *value = OrderStatus::Charging;
        return true;
    }
    if (text == QStringLiteral("PENDING_PAYMENT")) {
        *value = OrderStatus::PendingPayment;
        return true;
    }
    if (text == QStringLiteral("COMPLETED")) {
        *value = OrderStatus::Completed;
        return true;
    }
    if (text == QStringLiteral("CANCELLED")) {
        *value = OrderStatus::Cancelled;
        return true;
    }
    return false;
}

bool parseEnum(const QString &text, CongestionLevel *value)
{
    if (text == QStringLiteral("LOW")) {
        *value = CongestionLevel::Low;
        return true;
    }
    if (text == QStringLiteral("MEDIUM")) {
        *value = CongestionLevel::Medium;
        return true;
    }
    if (text == QStringLiteral("HIGH")) {
        *value = CongestionLevel::High;
        return true;
    }
    return false;
}

template<typename Enum>
bool readEnum(const QJsonObject &json, const char *field, Enum *value, QString *error)
{
    QString text;
    if (!readString(json, field, &text, error)) {
        return false;
    }
    if (!parseEnum(text, value)) {
        return fail(error,
                    QString::fromLatin1(field),
                    QStringLiteral("contains an unknown enum value"));
    }
    return true;
}

bool readNullableCongestion(const QJsonObject &json,
                            const char *field,
                            std::optional<CongestionLevel> *value,
                            QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (item.isNull()) {
        value->reset();
        return true;
    }
    if (!item.isString()) {
        return fail(error, key, QStringLiteral("must be a congestion level or null"));
    }
    CongestionLevel parsed;
    if (!parseEnum(item.toString(), &parsed)) {
        return fail(error, key, QStringLiteral("contains an unknown enum value"));
    }
    *value = parsed;
    return true;
}

QJsonValue jsonInteger(qint64 value)
{
    return QJsonValue(static_cast<double>(value));
}

QJsonValue jsonNullableString(const std::optional<QString> &value)
{
    return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QJsonValue jsonNullableInteger(const std::optional<qint64> &value)
{
    return value.has_value() ? jsonInteger(*value) : QJsonValue(QJsonValue::Null);
}

}  // namespace

QString toString(UserStatus value)
{
    switch (value) {
    case UserStatus::Active: return QStringLiteral("ACTIVE");
    case UserStatus::Frozen: return QStringLiteral("FROZEN");
    }
    return {};
}

QString toString(StationStatus value)
{
    switch (value) {
    case StationStatus::Active: return QStringLiteral("ACTIVE");
    case StationStatus::Disabled: return QStringLiteral("DISABLED");
    }
    return {};
}

QString toString(PileType value)
{
    switch (value) {
    case PileType::Fast: return QStringLiteral("FAST");
    case PileType::Slow: return QStringLiteral("SLOW");
    }
    return {};
}

QString toString(PileStatus value)
{
    switch (value) {
    case PileStatus::Idle: return QStringLiteral("IDLE");
    case PileStatus::Reserved: return QStringLiteral("RESERVED");
    case PileStatus::Charging: return QStringLiteral("CHARGING");
    case PileStatus::Fault: return QStringLiteral("FAULT");
    case PileStatus::Offline: return QStringLiteral("OFFLINE");
    }
    return {};
}

QString toString(OrderMode value)
{
    switch (value) {
    case OrderMode::Reservation: return QStringLiteral("RESERVATION");
    case OrderMode::Direct: return QStringLiteral("DIRECT");
    }
    return {};
}

QString toString(OrderStatus value)
{
    switch (value) {
    case OrderStatus::Reserved: return QStringLiteral("RESERVED");
    case OrderStatus::Charging: return QStringLiteral("CHARGING");
    case OrderStatus::PendingPayment: return QStringLiteral("PENDING_PAYMENT");
    case OrderStatus::Completed: return QStringLiteral("COMPLETED");
    case OrderStatus::Cancelled: return QStringLiteral("CANCELLED");
    }
    return {};
}

QString toString(CongestionLevel value)
{
    switch (value) {
    case CongestionLevel::Low: return QStringLiteral("LOW");
    case CongestionLevel::Medium: return QStringLiteral("MEDIUM");
    case CongestionLevel::High: return QStringLiteral("HIGH");
    }
    return {};
}

QJsonObject toJson(const UserDto &dto)
{
    return {
        {QStringLiteral("userId"), jsonInteger(dto.userId)},
        {QStringLiteral("phone"), dto.phone},
        {QStringLiteral("nickname"), dto.nickname},
        {QStringLiteral("balanceCents"), jsonInteger(dto.balanceCents)},
        {QStringLiteral("status"), toString(dto.status)},
        {QStringLiteral("createdAt"), dto.createdAt},
    };
}

QJsonObject toJson(const StationDto &dto)
{
    return {
        {QStringLiteral("stationId"), jsonInteger(dto.stationId)},
        {QStringLiteral("name"), dto.name},
        {QStringLiteral("region"), dto.region},
        {QStringLiteral("address"), dto.address},
        {QStringLiteral("longitude"), dto.longitude},
        {QStringLiteral("latitude"), dto.latitude},
        {QStringLiteral("priceCentsPerKwh"), jsonInteger(dto.priceCentsPerKwh)},
        {QStringLiteral("status"), toString(dto.status)},
        {QStringLiteral("totalPileCount"), jsonInteger(dto.totalPileCount)},
        {QStringLiteral("availablePileCount"), jsonInteger(dto.availablePileCount)},
        {QStringLiteral("onlineRatePercent"), dto.onlineRatePercent},
        {QStringLiteral("distanceKm"), dto.distanceKm.has_value()
             ? QJsonValue(*dto.distanceKm) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("predictedCongestion"), dto.predictedCongestion.has_value()
             ? QJsonValue(toString(*dto.predictedCongestion)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("recommended"), dto.recommended},
    };
}

QJsonObject toJson(const PileDto &dto)
{
    return {
        {QStringLiteral("pileId"), jsonInteger(dto.pileId)},
        {QStringLiteral("stationId"), jsonInteger(dto.stationId)},
        {QStringLiteral("pileCode"), dto.pileCode},
        {QStringLiteral("pileType"), toString(dto.pileType)},
        {QStringLiteral("ratedPowerKw"), dto.ratedPowerKw},
        {QStringLiteral("status"), toString(dto.status)},
        {QStringLiteral("chargeCount"), jsonInteger(dto.chargeCount)},
        {QStringLiteral("totalChargeSeconds"), jsonInteger(dto.totalChargeSeconds)},
    };
}

QJsonObject toJson(const OrderDto &dto)
{
    return {
        {QStringLiteral("orderId"), jsonInteger(dto.orderId)},
        {QStringLiteral("orderNo"), dto.orderNo},
        {QStringLiteral("createdAt"), dto.createdAt},
        {QStringLiteral("userId"), jsonInteger(dto.userId)},
        {QStringLiteral("stationId"), jsonInteger(dto.stationId)},
        {QStringLiteral("stationName"), dto.stationName},
        {QStringLiteral("pileId"), jsonInteger(dto.pileId)},
        {QStringLiteral("pileCode"), dto.pileCode},
        {QStringLiteral("mode"), toString(dto.mode)},
        {QStringLiteral("status"), toString(dto.status)},
        {QStringLiteral("reservedAt"), jsonNullableString(dto.reservedAt)},
        {QStringLiteral("startedAt"), jsonNullableString(dto.startedAt)},
        {QStringLiteral("endedAt"), jsonNullableString(dto.endedAt)},
        {QStringLiteral("paidAt"), jsonNullableString(dto.paidAt)},
        {QStringLiteral("durationSeconds"), jsonInteger(dto.durationSeconds)},
        {QStringLiteral("energyWh"), jsonInteger(dto.energyWh)},
        {QStringLiteral("unitPriceCentsPerKwh"), jsonNullableInteger(dto.unitPriceCentsPerKwh)},
        {QStringLiteral("amountCents"), jsonInteger(dto.amountCents)},
    };
}

bool fromJson(const QJsonObject &json, UserDto *dto, QString *error)
{
    if (dto == nullptr) {
        return fail(error, QStringLiteral("dto"), QStringLiteral("must not be null"));
    }
    UserDto parsed;
    if (!readInteger(json, "userId", &parsed.userId, error)
        || !readString(json, "phone", &parsed.phone, error)
        || !readString(json, "nickname", &parsed.nickname, error)
        || !readInteger(json, "balanceCents", &parsed.balanceCents, error)
        || !readEnum(json, "status", &parsed.status, error)
        || !readString(json, "createdAt", &parsed.createdAt, error)) {
        return false;
    }
    *dto = parsed;
    if (error != nullptr) error->clear();
    return true;
}

bool fromJson(const QJsonObject &json, StationDto *dto, QString *error)
{
    if (dto == nullptr) {
        return fail(error, QStringLiteral("dto"), QStringLiteral("must not be null"));
    }
    StationDto parsed;
    if (!readInteger(json, "stationId", &parsed.stationId, error)
        || !readString(json, "name", &parsed.name, error)
        || !readString(json, "region", &parsed.region, error)
        || !readString(json, "address", &parsed.address, error)
        || !readDouble(json, "longitude", &parsed.longitude, error)
        || !readDouble(json, "latitude", &parsed.latitude, error)
        || !readInteger(json, "priceCentsPerKwh", &parsed.priceCentsPerKwh, error)
        || !readEnum(json, "status", &parsed.status, error)
        || !readInteger(json, "totalPileCount", &parsed.totalPileCount, error)
        || !readInteger(json, "availablePileCount", &parsed.availablePileCount, error)
        || !readDouble(json, "onlineRatePercent", &parsed.onlineRatePercent, error)
        || !readNullableDouble(json, "distanceKm", &parsed.distanceKm, error)
        || !readNullableCongestion(json, "predictedCongestion", &parsed.predictedCongestion, error)
        || !readBool(json, "recommended", &parsed.recommended, error)) {
        return false;
    }
    *dto = parsed;
    if (error != nullptr) error->clear();
    return true;
}

bool fromJson(const QJsonObject &json, PileDto *dto, QString *error)
{
    if (dto == nullptr) {
        return fail(error, QStringLiteral("dto"), QStringLiteral("must not be null"));
    }
    PileDto parsed;
    if (!readInteger(json, "pileId", &parsed.pileId, error)
        || !readInteger(json, "stationId", &parsed.stationId, error)
        || !readString(json, "pileCode", &parsed.pileCode, error)
        || !readEnum(json, "pileType", &parsed.pileType, error)
        || !readDouble(json, "ratedPowerKw", &parsed.ratedPowerKw, error)
        || !readEnum(json, "status", &parsed.status, error)
        || !readInteger(json, "chargeCount", &parsed.chargeCount, error)
        || !readInteger(json, "totalChargeSeconds", &parsed.totalChargeSeconds, error)) {
        return false;
    }
    *dto = parsed;
    if (error != nullptr) error->clear();
    return true;
}

bool fromJson(const QJsonObject &json, OrderDto *dto, QString *error)
{
    if (dto == nullptr) {
        return fail(error, QStringLiteral("dto"), QStringLiteral("must not be null"));
    }
    OrderDto parsed;
    if (!readInteger(json, "orderId", &parsed.orderId, error)
        || !readString(json, "orderNo", &parsed.orderNo, error)
        || !readString(json, "createdAt", &parsed.createdAt, error)
        || !readInteger(json, "userId", &parsed.userId, error)
        || !readInteger(json, "stationId", &parsed.stationId, error)
        || !readString(json, "stationName", &parsed.stationName, error)
        || !readInteger(json, "pileId", &parsed.pileId, error)
        || !readString(json, "pileCode", &parsed.pileCode, error)
        || !readEnum(json, "mode", &parsed.mode, error)
        || !readEnum(json, "status", &parsed.status, error)
        || !readNullableString(json, "reservedAt", &parsed.reservedAt, error)
        || !readNullableString(json, "startedAt", &parsed.startedAt, error)
        || !readNullableString(json, "endedAt", &parsed.endedAt, error)
        || !readNullableString(json, "paidAt", &parsed.paidAt, error)
        || !readInteger(json, "durationSeconds", &parsed.durationSeconds, error)
        || !readInteger(json, "energyWh", &parsed.energyWh, error)
        || !readNullableInteger(json, "unitPriceCentsPerKwh", &parsed.unitPriceCentsPerKwh, error)
        || !readInteger(json, "amountCents", &parsed.amountCents, error)) {
        return false;
    }
    *dto = parsed;
    if (error != nullptr) error->clear();
    return true;
}

}  // namespace charging::protocol
