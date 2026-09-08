#pragma once

#include <QDateTime>

#include <limits>
#include <optional>

namespace charging::server {

// ADR-0013: fixed Demo policy, evaluated only for quotes and charging start.
// Beijing clock, inclusive start / exclusive end. Never apply to stored orders.
inline std::optional<qint64> chargingUnitPriceCents(qint64 basePrice,
                                                  const QDateTime &startedAt)
{
    if (basePrice <= 0 || !startedAt.isValid()) return std::nullopt;
    const int hour = startedAt.toOffsetFromUtc(8 * 3600).time().hour();
    const bool peak = (hour >= 8 && hour < 11) || (hour >= 18 && hour < 21);
    if (!peak) return basePrice;
    constexpr qint64 permille = 1200;
    if (basePrice > (std::numeric_limits<qint64>::max() - 500) / permille) {
        return std::nullopt;
    }
    return (basePrice * permille + 500) / 1000;
}

// One immutable price snapshot, integer Wh and cents throughout settlement.
inline std::optional<qint64> orderAmountCents(qint64 energyWh, qint64 unitPrice)
{
    if (energyWh < 0 || unitPrice <= 0
        || energyWh > (std::numeric_limits<qint64>::max() - 500) / unitPrice) {
        return std::nullopt;
    }
    return (energyWh * unitPrice + 500) / 1000;
}

}  // namespace charging::server
