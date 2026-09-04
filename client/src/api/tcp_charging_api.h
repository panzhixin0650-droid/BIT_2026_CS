#pragma once

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

class TcpChargingApi final : public IChargingApi {
    Q_OBJECT

public:
    explicit TcpChargingApi(QString host = QStringLiteral("127.0.0.1"),
                            quint16 port = 45678,
                            int requestTimeoutMs = 5000,
                            QObject *parent = nullptr);
    ~TcpChargingApi() override;

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

private:
    struct PendingRequest {
        QString type;
        QByteArray frame;
        QTimer *timer = nullptr;
    };

    [[nodiscard]] QString nextRequestId();
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
    protocol::FrameDecoder decoder_;
    QHash<QString, PendingRequest> pending_;
    QStringList sendQueue_;
    QString token_;
    quint64 requestSequence_ = 0;
    bool handlingTransportFailure_ = false;
};

}  // namespace charging::client
