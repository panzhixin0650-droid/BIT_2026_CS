// 实现各 DTO 与 JSON 的相互转换及字段类型校验
#include "charging/protocol/dto.h"

#include <QJsonValue>

#include <cmath>

namespace charging::protocol {
namespace {

// JSON 可安全表示的最大整数，超出即判为非法
constexpr double kMaxSafeJsonInteger = 9007199254740991.0;

// 统一拼出字段错误描述并返回失败
bool fail(QString *error, const QString &field, const QString &expectation)
{
    if (error != nullptr) {
        *error = field + QStringLiteral(" ") + expectation;
    }
    return false;
}

// 读取字符串字段，类型不符直接报错
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

// 读取浮点数并拒绝 NaN 与无穷大
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

// 整数字段必须无小数且落在安全范围内
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

// 可空字符串：JSON 为 null 时视为未设置
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

// 可空整数先判 null，再复用整数校验逻辑
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

// 以下重载把协议大写字符串解析成对应枚举
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

// 模板函数：先读字符串再按枚举类型解析
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

// 拥堵等级可为 null 表示暂无预测，非空时须为合法枚举
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

// 整数按 double 写入 JSON，缺省值写成 null
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

// 以下函数把枚举转成协议规定的大写字符串
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

// 场站转 JSON，pricingRule 为空时不写该字段
QJsonObject toJson(const StationDto &dto)
{
    QJsonObject json{
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
    if (!dto.pricingRule.isEmpty()) {
        json.insert(QStringLiteral("pricingRule"), dto.pricingRule);
    }
    return json;
}

// 充电桩转 JSON
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

// 订单转 JSON，未发生的时间点写 null
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

// 解析用户，任一字段不合法就整体失败
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

// 解析场站，pricingRule 存在时才校验
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
        || !readBool(json, "recommended", &parsed.recommended, error)
        || (json.contains(QStringLiteral("pricingRule"))
            && !readString(json, "pricingRule", &parsed.pricingRule, error))) {
        return false;
    }
    *dto = parsed;
    if (error != nullptr) error->clear();
    return true;
}

// 解析充电桩各字段
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

// 解析订单，逐项校验必填与可空字段
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
