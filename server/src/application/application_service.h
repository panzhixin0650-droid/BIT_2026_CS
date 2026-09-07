#pragma once

#include "service_result.h"

#include "charging/protocol/dto.h"

#include <QJsonObject>
#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QString>

#include <optional>

namespace charging::server {

class IRepository;
class IPileGateway;
class MockPredictionProvider;
class SessionStore;

// Shared user/admin business boundary; it owns validation, order states,
// billing and transactions without exposing SQL to UI or TCP classes.
class ApplicationService final : public QObject {
    Q_OBJECT

public:
    ApplicationService(IRepository *repository,
                       SessionStore *sessions,
                       IPileGateway *pileGateway,
                       MockPredictionProvider *predictions,
                       QObject *parent = nullptr);

    [[nodiscard]] ServiceResult ping(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult loginUser(const QJsonObject &input);
    [[nodiscard]] ServiceResult logout(const QString &token);
    [[nodiscard]] ServiceResult getProfile(const QString &token) const;
    [[nodiscard]] ServiceResult updateProfile(const QString &token,
                                              const QJsonObject &input);
    [[nodiscard]] ServiceResult recharge(const QString &token,
                                         const QJsonObject &input);
    [[nodiscard]] ServiceResult listStations(const QString &token,
                                             const QJsonObject &input) const;
    [[nodiscard]] ServiceResult getStation(const QString &token,
                                           const QJsonObject &input) const;

    [[nodiscard]] ServiceResult getCurrentOrder(const QString &token,
                                               const QJsonObject &input = {}) const;
    [[nodiscard]] ServiceResult listUserOrders(const QString &token,
                                              const QJsonObject &input = {}) const;
    [[nodiscard]] ServiceResult reserveOrder(const QString &token, const QJsonObject &input);
    [[nodiscard]] ServiceResult cancelOrder(const QString &token, const QJsonObject &input);
    [[nodiscard]] ServiceResult startOrder(const QString &token, const QJsonObject &input);
    [[nodiscard]] ServiceResult getOrderProgress(const QString &token,
                                                const QJsonObject &input) const;
    [[nodiscard]] ServiceResult stopOrder(const QString &token, const QJsonObject &input);
    [[nodiscard]] ServiceResult payOrder(const QString &token, const QJsonObject &input);

    [[nodiscard]] ServiceResult loginAdmin(const QString &username,
                                           const QString &password);
    [[nodiscard]] ServiceResult getAdminProfile(qint64 actorAdminId) const;
    [[nodiscard]] ServiceResult listAdminAccounts(qint64 actorAdminId,
                                                  const QString &keyword = {},
                                                  const QString &status = {}) const;
    [[nodiscard]] ServiceResult createAdminAccount(qint64 actorAdminId,
                                                   const QJsonObject &input);
    [[nodiscard]] ServiceResult updateAdminAccount(qint64 actorAdminId,
                                                   const QJsonObject &input);
    [[nodiscard]] ServiceResult changeAdminPassword(qint64 actorAdminId,
                                                    const QString &currentPassword,
                                                    const QString &newPassword);
    [[nodiscard]] ServiceResult getDashboard(qint64 actorAdminId, int days) const;
    [[nodiscard]] ServiceResult getDashboard(qint64 actorAdminId,
                                             const QDate &startDate,
                                             const QDate &endDate) const;
    [[nodiscard]] ServiceResult listAdminStations(qint64 actorAdminId,
                                                  const QString &region,
                                                  const QString &keyword) const;
    [[nodiscard]] ServiceResult createAdminStation(qint64 actorAdminId,
                                                   const QJsonObject &input);
    [[nodiscard]] ServiceResult updateAdminStation(qint64 actorAdminId,
                                                   const QJsonObject &input);
    [[nodiscard]] ServiceResult setAdminStationStatus(
        qint64 actorAdminId,
        qint64 stationId,
        charging::protocol::StationStatus status);
    [[nodiscard]] ServiceResult deleteAdminStation(qint64 actorAdminId,
                                                   qint64 stationId);
    [[nodiscard]] ServiceResult listAdminPiles(
        qint64 actorAdminId,
        std::optional<qint64> stationId = std::nullopt) const;
    [[nodiscard]] ServiceResult createAdminPile(qint64 actorAdminId,
                                                const QJsonObject &input);
    [[nodiscard]] ServiceResult updateAdminPile(qint64 actorAdminId,
                                                const QJsonObject &input);
    [[nodiscard]] ServiceResult deleteAdminPile(qint64 actorAdminId,
                                                qint64 pileId);
    [[nodiscard]] ServiceResult setAdminPileStatus(
        qint64 actorAdminId,
        qint64 pileId,
        charging::protocol::PileStatus status);
    [[nodiscard]] ServiceResult restartAdminPile(qint64 actorAdminId,
                                                 qint64 pileId);
    [[nodiscard]] ServiceResult listAdminUsers(qint64 actorAdminId,
                                               const QString &phoneKeyword) const;
    [[nodiscard]] ServiceResult setAdminUserStatus(
        qint64 actorAdminId,
        qint64 userId,
        charging::protocol::UserStatus status);
    [[nodiscard]] ServiceResult listAdminOrders(qint64 actorAdminId) const;

private:
    [[nodiscard]] std::optional<qint64> authenticatedUserId(
        const QString &token,
        ServiceResult *failure) const;
    [[nodiscard]] bool refreshOrderReading(charging::protocol::OrderDto *order,
                                           const QDateTime &now, bool stop = false) const;

    IRepository *repository_ = nullptr;
    SessionStore *sessions_ = nullptr;
    IPileGateway *pileGateway_ = nullptr;
    MockPredictionProvider *predictions_ = nullptr;
};

}  // namespace charging::server
