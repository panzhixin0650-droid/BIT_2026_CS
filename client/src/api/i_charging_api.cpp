// 充电API接口基类实现：工单方法默认返回不可用
#include "api/i_charging_api.h"
#include <QTimer>
#include <QUuid>

namespace charging::client {

// 构造时注册工单结果元类型，保证信号可传参
IChargingApi::IChargingApi(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<TicketResult>();
    qRegisterMetaType<TicketListResult>();
}

IChargingApi::~IChargingApi() = default;

// 默认创建工单：异步回一个服务不可用的响应
QString IChargingApi::createSupportTicket(const protocol::SupportTicketDraft &)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTimer::singleShot(0, this, [this, id] {
        emit supportTicketCreated({{id, protocol::MessageType::SupportTicketCreate,
            protocol::ErrorCode::ServiceUnavailable, QStringLiteral("工单功能暂不可用")}, {}});
    });
    return id;
}

// 默认工单列表同样返回功能暂不可用
QString IChargingApi::listSupportTickets(std::optional<qint64>)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTimer::singleShot(0, this, [this, id] {
        emit supportTicketsListed({{id, protocol::MessageType::SupportTicketList,
            protocol::ErrorCode::ServiceUnavailable, QStringLiteral("工单功能暂不可用")}, {}});
    });
    return id;
}

// 默认工单详情返回不可用，由具体实现覆盖
QString IChargingApi::getSupportTicket(qint64)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTimer::singleShot(0, this, [this, id] {
        emit supportTicketDetailed({{id, protocol::MessageType::SupportTicketDetail,
            protocol::ErrorCode::ServiceUnavailable, QStringLiteral("工单功能暂不可用")}, {}});
    });
    return id;
}

}  // namespace charging::client
