#pragma once

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include <QString>

#include <optional>

namespace charging::client::session {

enum class StartDecision { Direct, UseReservation, Blocked };

inline StartDecision startDecision(const std::optional<protocol::OrderDto> &current,
                                   const QString &candidatePile)
{
    if (!current) return StartDecision::Direct;
    if (current->status == protocol::OrderStatus::Reserved
        && current->pileCode == candidatePile) return StartDecision::UseReservation;
    return StartDecision::Blocked;
}

inline bool shouldPoll(const std::optional<protocol::OrderDto> &order)
{
    return order && order->status == protocol::OrderStatus::Charging;
}

inline bool needsFinalHistory(const std::optional<protocol::OrderDto> &previous)
{
    return previous && (previous->status == protocol::OrderStatus::Charging
                        || previous->status == protocol::OrderStatus::PendingPayment
                        || previous->status == protocol::OrderStatus::Reserved);
}

inline int demoProgressPercent(qint64 durationSeconds)
{
    return qBound(0, int(durationSeconds * 100 / protocol::DemoChargingDurationSeconds), 100);
}

}  // namespace charging::client::session
