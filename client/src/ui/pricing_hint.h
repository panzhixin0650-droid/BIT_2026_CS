#pragma once

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

namespace charging::client {

// Presentation only: never calculate a quote or infer a past order's tariff.
inline QString pricingHint(const protocol::StationDto &station)
{
    if (station.pricingRule == QLatin1String(protocol::DemoPeakPricingRule)) {
        return QStringLiteral("Demo 高峰 +20%\n08:00–11:00、18:00–21:00（北京时间）\n\n"
                              "其他时间原价；以开始充电时单价锁定全单，跨时段不变价。\n\n"
                              "仅用于课程演示，不代表实际电价。");
    }
    return QStringLiteral("当前为参考价，以开始充电时的订单单价为准");
}

inline QString chargingPriceText(qint64 cents)
{
    return QStringLiteral("¥%1.%2/度").arg(cents / 100).arg(cents % 100, 2, 10, QChar('0'));
}

}  // namespace charging::client
