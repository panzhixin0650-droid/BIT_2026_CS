// 本文件放计费小工具：单价选择与订单金额换算
#pragma once

#include <QDateTime>

#include <limits>
#include <optional>

namespace charging::server {

// ADR-0016: fixed Demo policy, evaluated only for quotes and charging start.
// Beijing clock, inclusive start / exclusive end. Never apply to stored orders.
// 按开始时间返回单价，仅用于报价和充电开始时锁价
inline std::optional<qint64> chargingUnitPriceCents(qint64 basePrice,
                                                  const QDateTime &startedAt)
{
    if (basePrice <= 0 || !startedAt.isValid()) return std::nullopt;
    // 换算到北京时间取小时，判断是否落在高峰区间
    const int hour = startedAt.toOffsetFromUtc(8 * 3600).time().hour();
    const bool peak = (hour >= 8 && hour < 11) || (hour >= 18 && hour < 21);
    if (!peak) return basePrice;
    // 高峰上浮20%，先防溢出再四舍五入到整数分
    constexpr qint64 permille = 1200;
    if (basePrice > (std::numeric_limits<qint64>::max() - 500) / permille) {
        return std::nullopt;
    }
    return (basePrice * permille + 500) / 1000;
}

// One immutable price snapshot, integer Wh and cents throughout settlement.
// 电量Wh乘单价得金额，整数分四舍五入并防溢出
inline std::optional<qint64> orderAmountCents(qint64 energyWh, qint64 unitPrice)
{
    if (energyWh < 0 || unitPrice <= 0
        || energyWh > (std::numeric_limits<qint64>::max() - 500) / unitPrice) {
        return std::nullopt;
    }
    return (energyWh * unitPrice + 500) / 1000;
}

}  // namespace charging::server
