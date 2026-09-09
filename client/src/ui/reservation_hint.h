#pragma once

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include <QDateTime>

namespace charging::client {

// Display only: cancellation and its deadline check belong to the API producer.
// 生成预约提示文案：仅展示用，取消与超时判定由服务端负责
inline QString reservationHint(const protocol::OrderDto &order)
{
    if (order.status != protocol::OrderStatus::Reserved) return {};
    // 预约时间转北京时间并加上30分钟保留时长
    const auto reserved = order.reservedAt
        ? QDateTime::fromString(*order.reservedAt, Qt::ISODate) : QDateTime{};
    if (!reserved.isValid()) return QStringLiteral("预约保留 30 分钟，超时自动取消");
    const auto deadline = reserved.addSecs(protocol::DemoReservationDurationSeconds)
                             .toOffsetFromUtc(8 * 3600);
    return QStringLiteral("请在 %1 前开始充电（北京时间）\n预约保留 30 分钟，超时自动取消，不扣费")
        .arg(deadline.toString(QStringLiteral("MM-dd HH:mm:ss")));
}

}  // namespace charging::client
