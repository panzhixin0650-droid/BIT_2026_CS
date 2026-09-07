#include "api/i_charging_api.h"
#include <QTimer>
#include <QUuid>

namespace charging::client {

IChargingApi::IChargingApi(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<TicketResult>();
    qRegisterMetaType<TicketListResult>();
}

IChargingApi::~IChargingApi() = default;

QString IChargingApi::createSupportTicket(const protocol::SupportTicketDraft &)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTimer::singleShot(0, this, [this, id] {
        emit supportTicketCreated({{id, protocol::MessageType::SupportTicketCreate,
            protocol::ErrorCode::ServiceUnavailable, QStringLiteral("工单功能暂不可用")}, {}});
    });
    return id;
}

QString IChargingApi::listSupportTickets(std::optional<qint64>)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTimer::singleShot(0, this, [this, id] {
        emit supportTicketsListed({{id, protocol::MessageType::SupportTicketList,
            protocol::ErrorCode::ServiceUnavailable, QStringLiteral("工单功能暂不可用")}, {}});
    });
    return id;
}

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
