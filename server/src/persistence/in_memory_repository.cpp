#include "in_memory_repository.h"

#include <QDateTime>
#include <QCryptographicHash>

#include <algorithm>

namespace charging::server {

using namespace charging::protocol;

InMemoryRepository::InMemoryRepository()
{
    admins_ = {
        AdminRecord{
            1,
            QStringLiteral("admin"),
            QString::fromLatin1(QCryptographicHash::hash(
                QByteArrayLiteral("123456"), QCryptographicHash::Sha256).toHex()),
            QStringLiteral("系统管理员"),
        },
    };

    users_ = {
        UserDto{1, QStringLiteral("13800000001"), QStringLiteral("演示用户0001"),
                20000, UserStatus::Active, QStringLiteral("2026-06-04T11:53:41Z")},
        UserDto{2, QStringLiteral("13800000005"), QStringLiteral("冻结用户0005"),
                5000, UserStatus::Frozen, QStringLiteral("2026-06-08T08:00:00Z")},
    };

    stations_ = {
        StationDto{1, QStringLiteral("浑南演示充电站"), QStringLiteral("浑南区"),
                   QStringLiteral("浑南区创新路1号"), 123.43, 41.71, 135,
                   StationStatus::Active},
        StationDto{2, QStringLiteral("和平智慧充电站"), QStringLiteral("和平区"),
                   QStringLiteral("和平区青年大街88号"), 123.42, 41.79, 128,
                   StationStatus::Active},
        StationDto{3, QStringLiteral("沈北大学城充电站"), QStringLiteral("沈北新区"),
                   QStringLiteral("沈北新区蒲昌路10号"), 123.41, 41.92, 120,
                   StationStatus::Active},
    };

    piles_ = {
        PileDto{1, 1, QStringLiteral("PILE-A-01"), PileType::Fast, 10.0,
                PileStatus::Idle, 4, 14400},
        PileDto{2, 1, QStringLiteral("PILE-A-02"), PileType::Slow, 7.0,
                PileStatus::Charging, 2, 7200},
        PileDto{3, 2, QStringLiteral("PILE-B-01"), PileType::Fast, 60.0,
                PileStatus::Idle, 8, 28600},
        PileDto{4, 2, QStringLiteral("PILE-B-02"), PileType::Slow, 7.0,
                PileStatus::Fault, 3, 9600},
        PileDto{5, 3, QStringLiteral("PILE-C-01"), PileType::Fast, 60.0,
                PileStatus::Idle, 5, 18000},
        PileDto{6, 3, QStringLiteral("PILE-C-02"), PileType::Fast, 60.0,
                PileStatus::Offline, 1, 3600},
    };
    nextUserId_ = 3;
    nextStationId_ = 4;
    nextPileId_ = 7;

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto makeOrder = [&now](qint64 id,
                                  qint64 userId,
                                  qint64 stationId,
                                  const QString &stationName,
                                  qint64 pileId,
                                  const QString &pileCode,
                                  OrderStatus status,
                                  int daysAgo,
                                  qint64 energyWh,
                                  qint64 amountCents) {
        OrderDto order;
        order.orderId = id;
        order.orderNo = QStringLiteral("ORD-DEMO-%1").arg(id, 4, 10, QLatin1Char('0'));
        order.createdAt = now.addDays(-daysAgo).addSecs(-3600).toString(Qt::ISODate);
        order.userId = userId;
        order.stationId = stationId;
        order.stationName = stationName;
        order.pileId = pileId;
        order.pileCode = pileCode;
        order.mode = OrderMode::Direct;
        order.status = status;
        order.startedAt = now.addDays(-daysAgo).addSecs(-3600).toString(Qt::ISODate);
        order.durationSeconds = energyWh / 2;
        order.energyWh = energyWh;
        order.unitPriceCentsPerKwh = stationId == 1 ? 135 : 128;
        order.amountCents = amountCents;
        if (status == OrderStatus::Completed || status == OrderStatus::PendingPayment) {
            order.endedAt = now.addDays(-daysAgo).toString(Qt::ISODate);
        }
        if (status == OrderStatus::Completed) {
            order.paidAt = now.addDays(-daysAgo).toString(Qt::ISODate);
        }
        return order;
    };

    orders_ = {
        makeOrder(1001, 1, 1, QStringLiteral("浑南演示充电站"), 1,
                  QStringLiteral("PILE-A-01"), OrderStatus::Completed, 0, 5000, 675),
        makeOrder(1002, 1, 2, QStringLiteral("和平智慧充电站"), 3,
                  QStringLiteral("PILE-B-01"), OrderStatus::Completed, 1, 10000, 1280),
        makeOrder(1003, 1, 1, QStringLiteral("浑南演示充电站"), 1,
                  QStringLiteral("PILE-A-01"), OrderStatus::Completed, 3, 8000, 1080),
        makeOrder(1004, 1, 2, QStringLiteral("和平智慧充电站"), 3,
                  QStringLiteral("PILE-B-01"), OrderStatus::Completed, 6, 6500, 832),
        makeOrder(1005, 1, 1, QStringLiteral("浑南演示充电站"), 2,
                  QStringLiteral("PILE-A-02"), OrderStatus::Charging, 0, 2400, 324),
        makeOrder(1006, 2, 3, QStringLiteral("沈北大学城充电站"), 5,
                  QStringLiteral("PILE-C-01"), OrderStatus::PendingPayment, 2, 4000, 480),
    };
}

std::optional<AdminRecord> InMemoryRepository::findAdminByUsername(
    const QString &username) const
{
    const auto found = std::find_if(admins_.cbegin(), admins_.cend(),
                                    [&username](const AdminRecord &admin) {
                                        return admin.username == username;
                                    });
    return found == admins_.cend() ? std::nullopt
                                   : std::optional<AdminRecord>(*found);
}

std::optional<UserDto> InMemoryRepository::findUserByPhone(const QString &phone) const
{
    const auto found = std::find_if(users_.cbegin(), users_.cend(),
                                    [&phone](const UserDto &user) {
                                        return user.phone == phone;
                                    });
    return found == users_.cend() ? std::nullopt
                                  : std::optional<UserDto>(*found);
}

std::optional<UserDto> InMemoryRepository::findUserById(qint64 userId) const
{
    const auto found = std::find_if(users_.cbegin(), users_.cend(),
                                    [userId](const UserDto &user) {
                                        return user.userId == userId;
                                    });
    return found == users_.cend() ? std::nullopt
                                  : std::optional<UserDto>(*found);
}

UserDto InMemoryRepository::createUser(const QString &phone,
                                       const QString &nickname,
                                       const QString &createdAt)
{
    UserDto user;
    user.userId = nextUserId_++;
    user.phone = phone;
    user.nickname = nickname;
    user.status = UserStatus::Active;
    user.createdAt = createdAt;
    users_.append(user);
    return user;
}

bool InMemoryRepository::updateUser(const UserDto &user)
{
    const auto found = std::find_if(users_.begin(), users_.end(),
                                    [&user](const UserDto &stored) {
                                        return stored.userId == user.userId;
                                    });
    if (found == users_.end()) {
        return false;
    }
    *found = user;
    return true;
}

QList<UserDto> InMemoryRepository::listUsers() const
{
    QList<UserDto> result = users_;
    std::sort(result.begin(), result.end(), [](const UserDto &left, const UserDto &right) {
        return left.userId < right.userId;
    });
    return result;
}

QList<StationDto> InMemoryRepository::listActiveStations() const
{
    QList<StationDto> result;
    for (const StationDto &station : stations_) {
        if (station.status == StationStatus::Active) {
            result.append(withPileCounts(station));
        }
    }
    return result;
}

QList<StationDto> InMemoryRepository::listStations() const
{
    QList<StationDto> result;
    for (const StationDto &station : stations_) {
        result.append(withPileCounts(station));
    }
    std::sort(result.begin(), result.end(), [](const StationDto &left,
                                               const StationDto &right) {
        return left.stationId < right.stationId;
    });
    return result;
}

std::optional<StationDto> InMemoryRepository::findStationById(qint64 stationId) const
{
    const auto found = std::find_if(stations_.cbegin(), stations_.cend(),
                                    [stationId](const StationDto &station) {
                                        return station.stationId == stationId;
                                    });
    return found == stations_.cend()
        ? std::nullopt
        : std::optional<StationDto>(withPileCounts(*found));
}

StationDto InMemoryRepository::createStation(StationDto station, qint64 pileCount)
{
    station.stationId = nextStationId_++;
    station.status = StationStatus::Active;
    station.distanceKm.reset();
    station.predictedCongestion.reset();
    station.recommended = false;
    stations_.append(station);

    for (qint64 index = 0; index < pileCount; ++index) {
        PileDto pile;
        pile.pileId = nextPileId_++;
        pile.stationId = station.stationId;
        pile.pileCode = QStringLiteral("PILE-%1-%2")
                            .arg(station.stationId, 3, 10, QLatin1Char('0'))
                            .arg(index + 1, 2, 10, QLatin1Char('0'));
        pile.pileType = index % 2 == 0 ? PileType::Fast : PileType::Slow;
        pile.ratedPowerKw = pile.pileType == PileType::Fast ? 60.0 : 7.0;
        pile.status = PileStatus::Idle;
        piles_.append(pile);
    }
    return withPileCounts(station);
}

QList<PileDto> InMemoryRepository::listPilesByStationId(qint64 stationId) const
{
    QList<PileDto> result;
    for (const PileDto &pile : piles_) {
        if (pile.stationId == stationId) {
            result.append(pile);
        }
    }
    std::sort(result.begin(), result.end(), [](const PileDto &left, const PileDto &right) {
        return left.pileId < right.pileId;
    });
    return result;
}

QList<PileDto> InMemoryRepository::listPiles() const
{
    QList<PileDto> result = piles_;
    std::sort(result.begin(), result.end(), [](const PileDto &left,
                                               const PileDto &right) {
        return left.pileId < right.pileId;
    });
    return result;
}

bool InMemoryRepository::updatePile(const PileDto &pile)
{
    const auto found = std::find_if(piles_.begin(), piles_.end(),
                                    [&pile](const PileDto &stored) {
                                        return stored.pileId == pile.pileId;
                                    });
    if (found == piles_.end()) {
        return false;
    }
    *found = pile;
    return true;
}

QList<OrderDto> InMemoryRepository::listOrders() const
{
    QList<OrderDto> result = orders_;
    std::sort(result.begin(), result.end(), [](const OrderDto &left,
                                               const OrderDto &right) {
        return left.createdAt > right.createdAt;
    });
    return result;
}

StationDto InMemoryRepository::withPileCounts(StationDto station) const
{
    qint64 total = 0;
    qint64 available = 0;
    qint64 online = 0;
    for (const PileDto &pile : piles_) {
        if (pile.stationId != station.stationId) {
            continue;
        }
        ++total;
        if (pile.status == PileStatus::Idle && station.status == StationStatus::Active) {
            ++available;
        }
        if (pile.status != PileStatus::Offline) {
            ++online;
        }
    }
    station.totalPileCount = total;
    station.availablePileCount = available;
    station.onlineRatePercent = total == 0
        ? 0.0
        : static_cast<double>(online) * 100.0 / static_cast<double>(total);
    return station;
}

}  // namespace charging::server
