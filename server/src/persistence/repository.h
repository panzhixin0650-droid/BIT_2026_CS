#pragma once

#include "i_repository.h"

#include <QSqlDatabase>
#include <QString>

namespace charging::server {

// The sole SQL boundary. Callers receive domain DTOs rather than QSqlQuery or
// database-specific errors, so another persistence backend can be introduced
// without changing ApplicationService or the external protocol.
class Repository final : public IRepository {
public:
    explicit Repository(QString connectionName = QStringLiteral("charging-server"));
    ~Repository();

    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;

    [[nodiscard]] bool open(const QString &databasePath, QString *error = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const noexcept;
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
        qint64 pileCount) override;
    [[nodiscard]] DeleteStationResult deleteStation(qint64 stationId) override;
    [[nodiscard]] QList<charging::protocol::PileDto>
    listPilesByStationId(qint64 stationId) const override;
    [[nodiscard]] QList<charging::protocol::PileDto>
    listPiles() const override;
    [[nodiscard]] bool updatePile(
        const charging::protocol::PileDto &pile) override;

    [[nodiscard]] QList<charging::protocol::OrderDto>
    listOrders() const override;

private:
    void beginOperation() const noexcept;
    void failOperation(const QString &operation, const QString &detail) const;
    [[nodiscard]] bool requireOpen(const QString &operation) const;

    QString connectionName_;
    QSqlDatabase database_;
    mutable bool lastOperationSucceeded_ = true;
};

}  // namespace charging::server
