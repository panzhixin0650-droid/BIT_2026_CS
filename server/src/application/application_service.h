// 本文件声明应用服务层，集中用户与管理员的业务入口
#pragma once

#include "service_result.h"

#include "charging/protocol/dto.h"

#include <QJsonObject>
#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QString>

#include <optional>
#include <functional>

namespace charging::server {

class IRepository;
class IPileGateway;
class MockPredictionProvider;
class SessionStore;

// Shared user/admin business boundary; it owns validation, order states,
// billing and transactions without exposing SQL to UI or TCP classes.
// 服务类持有仓储、会话、桩网关与预测组件，UI和TCP都经它调用
class ApplicationService final : public QObject {
    Q_OBJECT

public:
    using Clock = std::function<QDateTime()>;
    ApplicationService(IRepository *repository,
                       SessionStore *sessions,
                       IPileGateway *pileGateway,
                       MockPredictionProvider *predictions,
                       QObject *parent = nullptr,
                       Clock clock = QDateTime::currentDateTimeUtc);

    // 用户侧接口：登录、资料、充值与站点查询
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

    // 订单接口：查询、预约、开始、进度与停止
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
    // Demo定时任务开关：自动结束充电、预约到期作废
    void enableDemoAutomaticStop();
    int completeDueDemoCharges(const QDateTime &now);
    void enableReservationExpiry();
    // Housekeeping can persist cancellations before logically read-only queries.
    // Returns the number expired, or -1 on a storage/consistency failure.
    int expireDueReservations(const QDateTime &now) const;
    [[nodiscard]] ServiceResult payOrder(const QString &token, const QJsonObject &input);

    // 用户工单接口：创建、列表与详情
    [[nodiscard]] ServiceResult createSupportTicket(const QString &token, const QJsonObject &input);
    [[nodiscard]] ServiceResult listSupportTickets(const QString &token, const QJsonObject &input) const;
    [[nodiscard]] ServiceResult getSupportTicket(const QString &token, const QJsonObject &input) const;
    // Local administrator calls only; re-check identity and role on every operation.
    [[nodiscard]] ServiceResult listAdminSupportTickets(qint64 actorAdminId, std::optional<qint64> beforeId = {}) const;
    [[nodiscard]] ServiceResult updateAdminSupportTicket(qint64 actorAdminId, const QJsonObject &input);

    // 管理员接口，由本机管理端直接调用而非网络令牌
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

// 私有辅助：取当前时间、结算充电单、校验令牌与刷新读数
private:
    [[nodiscard]] QDateTime nowUtc() const { return clock_().toUTC(); }
    ServiceResult settleChargingOrder(qint64 orderId, qint64 userId, const QDateTime &now);
    bool demoAutomaticStop_ = false;
    bool reservationExpiryEnabled_ = false;
    int cancelReservation(charging::protocol::OrderDto *order) const;
    [[nodiscard]] std::optional<qint64> authenticatedUserId(
        const QString &token,
        ServiceResult *failure) const;
    [[nodiscard]] bool refreshOrderReading(charging::protocol::OrderDto *order,
                                           const QDateTime &now, bool stop = false) const;

    // 依赖均为外部注入的指针，本类不负责其生命周期
    IRepository *repository_ = nullptr;
    SessionStore *sessions_ = nullptr;
    IPileGateway *pileGateway_ = nullptr;
    MockPredictionProvider *predictions_ = nullptr;
    Clock clock_;
};

}  // namespace charging::server
