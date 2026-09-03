#pragma once

#include "service_result.h"

#include "charging/protocol/dto.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <optional>

namespace charging::server {

class IRepository;
class MockPile;
class MockPredictionProvider;
class SessionStore;

// The business boundary. User operations, administrator operations and
// transaction orchestration will be added here without exposing SQL to UI or
// TCP classes.
class ApplicationService final : public QObject {
    Q_OBJECT

public:
    ApplicationService(IRepository *repository,
                       SessionStore *sessions,
                       MockPile *pileGateway,
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

    [[nodiscard]] ServiceResult loginAdmin(const QString &username,
                                           const QString &password) const;
    [[nodiscard]] ServiceResult getDashboard(int days) const;
    [[nodiscard]] ServiceResult listAdminStations(const QString &region,
                                                  const QString &keyword) const;
    [[nodiscard]] ServiceResult createAdminStation(const QJsonObject &input);
    [[nodiscard]] ServiceResult listAdminPiles(
        std::optional<qint64> stationId = std::nullopt) const;
    [[nodiscard]] ServiceResult restartAdminPile(qint64 pileId);
    [[nodiscard]] ServiceResult listAdminUsers(const QString &phoneKeyword) const;
    [[nodiscard]] ServiceResult setAdminUserStatus(
        qint64 userId,
        charging::protocol::UserStatus status);
    [[nodiscard]] ServiceResult listAdminOrders() const;

private:
    [[nodiscard]] std::optional<qint64> authenticatedUserId(
        const QString &token,
        ServiceResult *failure) const;

    IRepository *repository_ = nullptr;
    SessionStore *sessions_ = nullptr;
    MockPile *pileGateway_ = nullptr;
    MockPredictionProvider *predictions_ = nullptr;
};

}  // namespace charging::server
