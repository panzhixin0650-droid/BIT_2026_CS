#pragma once

// 本文件声明基于 TCP 的充电接口客户端实现
#include "api/i_charging_api.h"

#include "charging/protocol/frame_codec.h"

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QTcpSocket>

class QJsonObject;
class QTimer;

namespace charging::protocol {
struct ResponseEnvelope;
}

namespace charging::client {

// TcpChargingApi 用长连接和帧协议实现 IChargingApi
class TcpChargingApi final : public IChargingApi {
    Q_OBJECT

public:
    explicit TcpChargingApi(QString host = QStringLiteral("127.0.0.1"),
                            quint16 port = 45678,
                            int requestTimeoutMs = 5000,
                            QObject *parent = nullptr);
    ~TcpChargingApi() override;

    // 各接口只返回请求编号，真正结果由信号异步送回
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
    // PendingRequest 记录待响应请求的类型、原始帧与超时定时器
    struct PendingRequest {
        QString type;
        QByteArray frame;
        QTimer *timer = nullptr;
        bool responseReceived = false;
    };

    [[nodiscard]] QString nextRequestId();
    // submit 组装请求帧，requiresToken 决定是否附带登录令牌
    [[nodiscard]] QString submit(const char *type,
                                 const QJsonObject &data,
                                 bool requiresToken);
    [[nodiscard]] QString rejectInvalid(const char *type, const QString &message);
    void ensureConnected();
    void sendQueuedRequests();
    void handleReadyRead();
    void handleResponse(const protocol::ResponseEnvelope &response);
    void handleSocketError(QAbstractSocket::SocketError error);
    void handleDisconnected();
    void failTransport(const QString &message);
    void emitFailure(const QString &requestId,
                     const QString &type,
                     int code,
                     const QString &message);
    void emitMalformedPayload(const protocol::ResponseEnvelope &response,
                              const QString &detail);

    QString host_;
    quint16 port_ = 45678;
    int requestTimeoutMs_ = 5000;
    QTcpSocket socket_;
    // decoder_ 从字节流中切分出完整帧
    protocol::FrameDecoder decoder_;
    QHash<QString, PendingRequest> pending_;
    // sendQueue_ 暂存尚未连接时待发送的请求
    QStringList sendQueue_;
    QString token_;
    quint64 requestSequence_ = 0;
    bool handlingTransportFailure_ = false;
};

}  // namespace charging::client
