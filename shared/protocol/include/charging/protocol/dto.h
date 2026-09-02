#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace charging::protocol {

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

struct UserDto {
    qint64 userId = 0;
    QString phone;
    QString nickname;
    qint64 balanceCents = 0;
    UserStatus status = UserStatus::Active;
    QString createdAt;
};

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
};

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
    std::optional<qint64> unitPriceCentsPerKwh;
    qint64 amountCents = 0;
};

[[nodiscard]] QJsonObject toJson(const UserDto &dto);
[[nodiscard]] QJsonObject toJson(const StationDto &dto);
[[nodiscard]] QJsonObject toJson(const PileDto &dto);
[[nodiscard]] QJsonObject toJson(const OrderDto &dto);

[[nodiscard]] bool fromJson(const QJsonObject &json, UserDto *dto, QString *error = nullptr);
[[nodiscard]] bool fromJson(const QJsonObject &json, StationDto *dto, QString *error = nullptr);
[[nodiscard]] bool fromJson(const QJsonObject &json, PileDto *dto, QString *error = nullptr);
[[nodiscard]] bool fromJson(const QJsonObject &json, OrderDto *dto, QString *error = nullptr);

}  // namespace charging::protocol
