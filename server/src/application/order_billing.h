#pragma once

#include <QtGlobal>

#include <limits>
#include <optional>

namespace charging::server {

// V1: one immutable station-price snapshot, integer Wh and cents. Keep the
// calculation in one place so a separately accepted pricing change is bounded.
inline std::optional<qint64> orderAmountCents(qint64 energyWh, qint64 unitPrice)
{
    if (energyWh < 0 || unitPrice <= 0
        || energyWh > (std::numeric_limits<qint64>::max() - 500) / unitPrice) {
        return std::nullopt;
    }
    return (energyWh * unitPrice + 500) / 1000;
}

}  // namespace charging::server
