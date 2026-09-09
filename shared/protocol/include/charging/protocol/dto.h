// 定义客户端与服务端共用的数据结构及其 JSON 互转声明
#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace charging::protocol {

// 各类业务状态枚举，与协议中的大写字符串一一对应
enum class UserStatus { Active, Frozen };
enum class StationStatus { Active, Disabled };
enum class PileType { Fast, Slow };
enum class PileStatus { Idle, Reserved, Charging, Fault, Offline };
enum class OrderMode { Reservation, Direct };
enum class OrderStatus { Reserved, Charging, PendingPayment, Completed, Cancelled };
enum class CongestionLevel { Low, Medium, High };

[[nodiscard]] QString toString(UserStatus value);
[[nodiscard]] QString toString(StationStatus value);
[[nodiscard]] QString toString(PileType value);
[[nodiscard]] QString toString(PileStatus value);
[[nodiscard]] QString toString(OrderMode value);
[[nodiscard]] QString toString(OrderStatus value);
[[nodiscard]] QString toString(CongestionLevel value);

// 用户信息，余额以整数分表示避免浮点误差
struct UserDto {
    qint64 userId = 0;
    QString phone;
    QString nickname;
    qint64 balanceCents = 0;
    UserStatus status = UserStatus::Active;
    QString createdAt;
};

// 场站信息，距离与拥堵预测为可选字段
struct StationDto {
    qint64 stationId = 0;
    QString name;
    QString region;
    QString address;
    double longitude = 0.0;
    double latitude = 0.0;
    qint64 priceCentsPerKwh = 0;
    StationStatus status = StationStatus::Active;
    qint64 totalPileCount = 0;
    qint64 availablePileCount = 0;
    double onlineRatePercent = 0.0;
    std::optional<double> distanceKm;
    std::optional<CongestionLevel> predictedCongestion;
    bool recommended = false;
    // Optional V1 quote metadata; absent/unknown rules do not imply peak pricing.
    QString pricingRule;
};

// 充电桩信息，含累计充电次数与总时长
struct PileDto {
    qint64 pileId = 0;
    qint64 stationId = 0;
    QString pileCode;
    PileType pileType = PileType::Fast;
    double ratedPowerKw = 0.0;
    PileStatus status = PileStatus::Idle;
    qint64 chargeCount = 0;
    qint64 totalChargeSeconds = 0;
};

// 订单快照：时间为 UTC 字符串，电量 Wh，金额分
struct OrderDto {
    qint64 orderId = 0;
    QString orderNo;
    QString createdAt;
    qint64 userId = 0;
    qint64 stationId = 0;
    QString stationName;
    qint64 pileId = 0;
    QString pileCode;
    OrderMode mode = OrderMode::Direct;
    OrderStatus status = OrderStatus::Reserved;
    std::optional<QString> reservedAt;
    std::optional<QString> startedAt;
    std::optional<QString> endedAt;
    std::optional<QString> paidAt;
    qint64 durationSeconds = 0;
    qint64 energyWh = 0;
    // 充电开始时锁定的单价，尚未开始时可能为空
    std::optional<qint64> unitPriceCentsPerKwh;
    qint64 amountCents = 0;
};

[[nodiscard]] QJsonObject toJson(const UserDto &dto);
[[nodiscard]] QJsonObject toJson(const StationDto &dto);
[[nodiscard]] QJsonObject toJson(const PileDto &dto);
[[nodiscard]] QJsonObject toJson(const OrderDto &dto);

// 从 JSON 解析，失败时通过 error 返回原因
[[nodiscard]] bool fromJson(const QJsonObject &json, UserDto *dto, QString *error = nullptr);
[[nodiscard]] bool fromJson(const QJsonObject &json, StationDto *dto, QString *error = nullptr);
[[nodiscard]] bool fromJson(const QJsonObject &json, PileDto *dto, QString *error = nullptr);
[[nodiscard]] bool fromJson(const QJsonObject &json, OrderDto *dto, QString *error = nullptr);

}  // namespace charging::protocol
