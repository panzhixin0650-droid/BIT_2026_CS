#pragma once

#include "request_router.h"

#include "charging/protocol/frame_codec.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace charging::server {

// Owns TCP connections, framing and request/response I/O. Business decisions
// remain in RequestRouter/ApplicationService.
class TcpGateway final : public QObject {
    Q_OBJECT

public:
    explicit TcpGateway(RequestRouter *router, QObject *parent = nullptr);

    [[nodiscard]] bool start(quint16 port,
                             const QHostAddress &address = QHostAddress::Any,
                             QString *error = nullptr);
    void stop();

    [[nodiscard]] bool isListening() const noexcept;
    [[nodiscard]] quint16 serverPort() const noexcept;

private slots:
    void acceptPendingConnections();
    void readClientData();
    void removeClient();

private:
    void sendResponse(QTcpSocket *socket,
                      const charging::protocol::ResponseEnvelope &response);

    QTcpServer server_;
    RequestRouter *router_ = nullptr;
    QHash<QTcpSocket *, charging::protocol::FrameDecoder> decoders_;
};

}  // namespace charging::server
