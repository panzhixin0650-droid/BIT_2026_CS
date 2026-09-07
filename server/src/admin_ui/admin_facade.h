#pragma once

#include "application/service_result.h"

#include "charging/protocol/dto.h"

#include <QJsonObject>
#include <QDate>
#include <QString>

#include <optional>

namespace charging::server {

class ApplicationService;

// In-process administrator boundary. The UI will call this facade rather than
// accessing Repository or SQL directly.
class AdminFacade final {
public:
    explicit AdminFacade(ApplicationService *service);

    [[nodiscard]] ServiceResult login(const QString &username,
                                      const QString &password) const;
    [[nodiscard]] ServiceResult getDashboard(int days) const;
    [[nodiscard]] ServiceResult getDashboard(const QDate &startDate,
                                             const QDate &endDate) const;
    [[nodiscard]] ServiceResult listStations(const QString &region = {},
                                             const QString &keyword = {}) const;
    [[nodiscard]] ServiceResult createStation(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult updateStation(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult setStationStatus(
        qint64 stationId,
        charging::protocol::StationStatus status) const;
    [[nodiscard]] ServiceResult deleteStation(qint64 stationId) const;
    [[nodiscard]] ServiceResult listPiles(
        std::optional<qint64> stationId = std::nullopt) const;
    [[nodiscard]] ServiceResult createPile(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult updatePile(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult deletePile(qint64 pileId) const;
    [[nodiscard]] ServiceResult setPileStatus(
        qint64 pileId,
        charging::protocol::PileStatus status) const;
    [[nodiscard]] ServiceResult restartPile(qint64 pileId) const;
    [[nodiscard]] ServiceResult listUsers(const QString &phoneKeyword = {}) const;
    [[nodiscard]] ServiceResult setUserStatus(
        qint64 userId,
        charging::protocol::UserStatus status) const;
    [[nodiscard]] ServiceResult listOrders() const;

private:
    ApplicationService *service_ = nullptr;
};

}  // namespace charging::server
