#pragma once

#include "i_repository.h"

#include <QList>

namespace charging::server {

// Explicit development/test Repository replacement. Data lives only for the
// lifetime of server-app and is never selected by the default startup path.
class InMemoryRepository final : public IRepository {
public:
    InMemoryRepository();

    [[nodiscard]] bool lastOperationSucceeded() const noexcept override;

    [[nodiscard]] std::optional<AdminRecord>
    findAdminByUsername(const QString &username) const override;

    [[nodiscard]] std::optional<charging::protocol::UserDto>
    findUserByPhone(const QString &phone) const override;
    [[nodiscard]] std::optional<charging::protocol::UserDto>
    findUserById(qint64 userId) const override;
    [[nodiscard]] charging::protocol::UserDto createUser(
        const QString &phone,
        const QString &nickname,
        const QString &createdAt) override;
    [[nodiscard]] bool updateUser(
        const charging::protocol::UserDto &user) override;
    [[nodiscard]] std::optional<charging::protocol::UserDto>
    addUserBalance(qint64 userId, qint64 amountCents) override;
    [[nodiscard]] QList<charging::protocol::UserDto>
    listUsers() const override;

    [[nodiscard]] QList<charging::protocol::StationDto>
    listActiveStations() const override;
    [[nodiscard]] QList<charging::protocol::StationDto>
    listStations() const override;
    [[nodiscard]] std::optional<charging::protocol::StationDto>
    findStationById(qint64 stationId) const override;
    [[nodiscard]] charging::protocol::StationDto createStation(
        charging::protocol::StationDto station,
        const QList<charging::protocol::PileDto> &piles) override;
    [[nodiscard]] bool updateStation(
        const charging::protocol::StationDto &station) override;
    [[nodiscard]] DeleteStationResult deleteStation(qint64 stationId) override;
    [[nodiscard]] QList<charging::protocol::PileDto>
    listPilesByStationId(qint64 stationId) const override;
    [[nodiscard]] QList<charging::protocol::PileDto>
    listPiles() const override;
    [[nodiscard]] charging::protocol::PileDto createPile(
        charging::protocol::PileDto pile) override;
    [[nodiscard]] DeletePileResult deletePile(qint64 pileId) override;
    [[nodiscard]] bool updatePile(
        const charging::protocol::PileDto &pile) override;

    [[nodiscard]] QList<charging::protocol::OrderDto>
    listOrders() const override;

private:
    [[nodiscard]] charging::protocol::StationDto withPileCounts(
        charging::protocol::StationDto station) const;

    QList<charging::protocol::UserDto> users_;
    QList<AdminRecord> admins_;
    QList<charging::protocol::StationDto> stations_;
    QList<charging::protocol::PileDto> piles_;
    QList<charging::protocol::OrderDto> orders_;
    qint64 nextUserId_ = 1;
    qint64 nextStationId_ = 1;
    qint64 nextPileId_ = 1;
};

}  // namespace charging::server
