// 文件用途：声明客户端Mock充电接口，用内存数据模拟服务端
#pragma once

#include "api/i_charging_api.h"

#include <QHash>
#include <QDateTime>

#include <optional>
#include <functional>

namespace charging::client {

// MockChargingApi：以内存表实现IChargingApi，供无服务端Demo使用
class MockChargingApi final : public IChargingApi {
    Q_OBJECT

public:
    // 时钟可注入，便于测试控制当前时间
    using Clock = std::function<QDateTime()>;
    explicit MockChargingApi(QObject *parent = nullptr,
                             Clock clock = QDateTime::currentDateTimeUtc);

    // 以下重写各接口，均立即返回请求ID再异步发完成信号
    [[nodiscard]] QString loginUser(const QString &phone) override;
    [[nodiscard]] QString logout() override;
    [[nodiscard]] QString getProfile() override;
    [[nodiscard]] QString updateNickname(const QString &nickname) override;
    [[nodiscard]] QString recharge(qint64 amountCents) override;
    [[nodiscard]] QString listStations(const StationQuery &query) override;
    [[nodiscard]] QString getStation(qint64 stationId) override;
    [[nodiscard]] QString getCurrentOrder() override;
    [[nodiscard]] QString listOrders() override;
    [[nodiscard]] QString reserve(const QString &pileCode) override;
    [[nodiscard]] QString cancel(qint64 orderId) override;
    [[nodiscard]] QString startCharging(
        const QString &pileCode,
        std::optional<qint64> reservationOrderId = std::nullopt) override;
    [[nodiscard]] QString getChargingProgress(qint64 orderId) override;
    [[nodiscard]] QString stopCharging(qint64 orderId) override;
    [[nodiscard]] QString payOrder(qint64 orderId) override;
    [[nodiscard]] QString createSupportTicket(const protocol::SupportTicketDraft &draft) override;
    [[nodiscard]] QString listSupportTickets(std::optional<qint64> beforeId = {}) override;
    [[nodiscard]] QString getSupportTicket(qint64 ticketId) override;

private:
    // 统一取UTC时间，保证时间口径一致
    [[nodiscard]] QDateTime nowUtc() const { return clock_().toUTC(); }
    // 结算充电与处理到期预约的内部辅助
    ChargingStopPayload finishCharge(qint64 orderId, const QDateTime &endedAt);
    void expireDueReservations(const QDateTime &now);
    [[nodiscard]] QString nextRequestId();
    [[nodiscard]] ApiResponse response(const QString &requestId,
                                       const char *type,
                                       int code,
                                       const QString &message) const;
    [[nodiscard]] std::optional<protocol::UserDto> authenticatedUser() const;
    [[nodiscard]] protocol::StationDto station(qint64 stationId) const;
    [[nodiscard]] QList<protocol::PileDto> piles(qint64 stationId) const;
    [[nodiscard]] std::optional<protocol::OrderDto> currentOrder(qint64 userId) const;
    [[nodiscard]] protocol::OrderDto orderWithProgress(
        const protocol::OrderDto &order) const;

    // 内存数据表：用户、充电桩、订单与模拟计时
    QHash<QString, protocol::UserDto> usersByPhone_;
    QHash<QString, protocol::PileDto> pilesByCode_;
    QHash<qint64, protocol::OrderDto> ordersById_;
    QHash<qint64, qint64> simulatedDurationByOrder_;
    QString authenticatedPhone_;
    QString token_;
    // 自增编号与请求序号，仅在本进程内有效
    qint64 nextUserId_ = 2;
    qint64 nextOrderId_ = 1001;
    quint64 requestSequence_ = 0;
    QList<protocol::SupportTicketDto> tickets_;
    qint64 nextTicketId_ = 1;
    Clock clock_;
};

}  // namespace charging::client
