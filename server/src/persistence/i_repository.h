#pragma once

#include "charging/protocol/dto.h"

#include <QList>
#include <QString>

#include <optional>

namespace charging::server {

struct AdminRecord {
    qint64 adminId = 0;
    QString username;
    QString passwordHash;
    QString displayName;
};

// Business-oriented persistence boundary. The production SQLite repository
// and the development in-memory repository expose the same operations, so
// ApplicationService never depends on SQL or QSqlQuery.
class IRepository {
public:
    virtual ~IRepository() = default;

    [[nodiscard]] virtual std::optional<AdminRecord>
    findAdminByUsername(const QString &username) const = 0;

    [[nodiscard]] virtual std::optional<charging::protocol::UserDto>
    findUserByPhone(const QString &phone) const = 0;
    [[nodiscard]] virtual std::optional<charging::protocol::UserDto>
    findUserById(qint64 userId) const = 0;
    [[nodiscard]] virtual charging::protocol::UserDto createUser(
        const QString &phone,
        const QString &nickname,
        const QString &createdAt) = 0;
    [[nodiscard]] virtual bool updateUser(
        const charging::protocol::UserDto &user) = 0;
    [[nodiscard]] virtual QList<charging::protocol::UserDto>
    listUsers() const = 0;

    [[nodiscard]] virtual QList<charging::protocol::StationDto>
    listActiveStations() const = 0;
    [[nodiscard]] virtual QList<charging::protocol::StationDto>
    listStations() const = 0;
    [[nodiscard]] virtual std::optional<charging::protocol::StationDto>
    findStationById(qint64 stationId) const = 0;
    [[nodiscard]] virtual charging::protocol::StationDto createStation(
        charging::protocol::StationDto station,
        qint64 pileCount) = 0;
    [[nodiscard]] virtual QList<charging::protocol::PileDto>
    listPilesByStationId(qint64 stationId) const = 0;
    [[nodiscard]] virtual QList<charging::protocol::PileDto>
    listPiles() const = 0;
    [[nodiscard]] virtual bool updatePile(
        const charging::protocol::PileDto &pile) = 0;

    [[nodiscard]] virtual QList<charging::protocol::OrderDto>
    listOrders() const = 0;
};

}  // namespace charging::server
