// 本文件声明 TCP 网关类，负责连接与帧的收发
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

    // 启动与停止监听，并可查询端口与监听状态
    [[nodiscard]] bool start(quint16 port,
                             const QHostAddress &address = QHostAddress::Any,
                             QString *error = nullptr);
    void stop();

    [[nodiscard]] bool isListening() const noexcept;
    [[nodiscard]] quint16 serverPort() const noexcept;

// 三个私有槽分别处理新连接、可读数据与断开
private slots:
    void acceptPendingConnections();
    void readClientData();
    void removeClient();

private:
    void sendResponse(QTcpSocket *socket,
                      const charging::protocol::ResponseEnvelope &response);

    // 每个 socket 各自保存解帧进度，互不影响
    QTcpServer server_;
    RequestRouter *router_ = nullptr;
    QHash<QTcpSocket *, charging::protocol::FrameDecoder> decoders_;
};

}  // namespace charging::server
