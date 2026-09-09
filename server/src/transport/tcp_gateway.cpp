// 本文件实现 TCP 网关：管理连接、分帧并收发协议消息
#include "tcp_gateway.h"

#include <QJsonObject>

namespace charging::server {

// 构造时把新连接信号接到接受连接的槽
TcpGateway::TcpGateway(RequestRouter *router, QObject *parent)
    : QObject(parent)
    , router_(router)
{
    connect(&server_, &QTcpServer::newConnection,
            this, &TcpGateway::acceptPendingConnections);
}

// 开始监听指定端口，已在监听则返回失败
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

// 停止服务：断开并释放所有客户端后关闭监听
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

// 为每个新连接建立独立解码器并接好读写信号
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

// 读取字节交给解码器，帧非法则断开该连接
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

    // 逐条解析请求信封，解析失败或无路由即断开
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

// 客户端断开后移除解码器并延迟释放对象
void TcpGateway::removeClient()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket == nullptr) {
        return;
    }
    decoders_.remove(socket);
    socket->deleteLater();
}

// 仅在连接可用时把响应编码成帧写出
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
