#include "tcp_gateway.h"

#include <QJsonObject>

namespace charging::server {

TcpGateway::TcpGateway(RequestRouter *router, QObject *parent)
    : QObject(parent)
    , router_(router)
{
    connect(&server_, &QTcpServer::newConnection,
            this, &TcpGateway::acceptPendingConnections);
}

bool TcpGateway::start(quint16 port, const QHostAddress &address, QString *error)
{
    if (server_.isListening()) {
        if (error != nullptr) {
            *error = QStringLiteral("server is already listening");
        }
        return false;
    }

    if (server_.listen(address, port)) {
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

    if (error != nullptr) {
        *error = server_.errorString();
    }
    return false;
}

void TcpGateway::stop()
{
    const auto clients = decoders_.keys();
    for (QTcpSocket *socket : clients) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    decoders_.clear();
    server_.close();
}

bool TcpGateway::isListening() const noexcept
{
    return server_.isListening();
}

quint16 TcpGateway::serverPort() const noexcept
{
    return server_.serverPort();
}

void TcpGateway::acceptPendingConnections()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket *socket = server_.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }

        decoders_.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead,
                this, &TcpGateway::readClientData);
        connect(socket, &QTcpSocket::disconnected,
                this, &TcpGateway::removeClient);
    }
}

void TcpGateway::readClientData()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket == nullptr || !decoders_.contains(socket)) {
        return;
    }

    const charging::protocol::DecodeResult decoded =
        decoders_[socket].append(socket->readAll());
    if (!decoded.ok()) {
        // Invalid frame length, JSON or root type is fatal for this
        // connection according to the V1 transport contract.
        socket->disconnectFromHost();
        return;
    }

    for (const QJsonObject &json : decoded.messages) {
        charging::protocol::RequestEnvelope request;
        QString parseError;
        if (!charging::protocol::RequestEnvelope::fromJson(json, &request,
                                                            &parseError)) {
            socket->disconnectFromHost();
            return;
        }

        if (router_ == nullptr) {
            socket->disconnectFromHost();
            return;
        }
        sendResponse(socket, router_->route(request));
    }
}

void TcpGateway::removeClient()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket == nullptr) {
        return;
    }
    decoders_.remove(socket);
    socket->deleteLater();
}

void TcpGateway::sendResponse(
    QTcpSocket *socket,
    const charging::protocol::ResponseEnvelope &response)
{
    if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    socket->write(charging::protocol::encodeFrame(response.toJson()));
}

}  // namespace charging::server
