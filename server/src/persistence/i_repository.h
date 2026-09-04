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

enum class DeletePileResult { Deleted, NotFound, HasOrders, Busy, StorageError };

enum class DeleteStationResult {
    Deleted,
    NotFound,
    HasOrders,
    StorageError,
};

// Business-oriented persistence boundary. The production SQLite repository
// and the development in-memory repository expose the same operations, so
// ApplicationService never depends on SQL or QSqlQuery.
class IRepository {
public:
    virtual ~IRepository() = default;

    // Distinguishes an expected empty/not-found result from a storage failure.
    // Repositories update this flag for every operation; ApplicationService
    // never exposes the underlying SQL error to callers.
    [[nodiscard]] virtual bool lastOperationSucceeded() const noexcept = 0;

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
    [[nodiscard]] virtual std::optional<charging::protocol::UserDto>
    addUserBalance(qint64 userId, qint64 amountCents) = 0;
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
        const QList<charging::protocol::PileDto> &piles) = 0;
    [[nodiscard]] virtual DeleteStationResult deleteStation(qint64 stationId) = 0;
    [[nodiscard]] virtual QList<charging::protocol::PileDto>
    listPilesByStationId(qint64 stationId) const = 0;
    [[nodiscard]] virtual QList<charging::protocol::PileDto>
    listPiles() const = 0;
    [[nodiscard]] virtual charging::protocol::PileDto createPile(
        charging::protocol::PileDto pile) = 0;
    [[nodiscard]] virtual DeletePileResult deletePile(qint64 pileId) = 0;
    [[nodiscard]] virtual bool updatePile(
        const charging::protocol::PileDto &pile) = 0;

    [[nodiscard]] virtual QList<charging::protocol::OrderDto>
    listOrders() const = 0;
};

}  // namespace charging::server
