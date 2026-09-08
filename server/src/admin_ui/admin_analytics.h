#pragma once

#include "charging/protocol/dto.h"
#include <QDateTime>
#include <QJsonArray>
#include <QMap>
#include <QTimeZone>

namespace charging::server {

// Half-open bands prevent boundary values from being counted twice.
inline int stationOccupancyBand(qint64 inUse, qint64 total)
{
    if (total <= 0) return -1;
    if (inUse >= total) return 0;
    if (inUse*100 >= total*70) return 1;
    if (inUse*100 >= total*40) return 2;
    if (inUse*100 >= total*20) return 3;
    return 4;
}
inline QString stationOccupancyLabel(int band)
{
    const QStringList labels{QStringLiteral("100%"),QStringLiteral("70%–不足100%"),
        QStringLiteral("40%–不足70%"),QStringLiteral("20%–不足40%"),QStringLiteral("低于20%")};
    return labels.value(band);
}

// Read-only aggregation of the already permission-filtered administrator API.
struct RevenueAnalysis {
    qint64 receivedCents = 0;
    qint64 paidOrders = 0;
    qint64 energyWh = 0;
    qint64 previousCents = 0;
    qint64 pendingCents = 0; // Current receivables, independent of the date range.
    QMap<QDate, qint64> dailyOrders;
    QMap<QDate, qint64> dailyEnergy;
    QMap<qint64, qint64> stationRevenue;
    QMap<qint64, QString> stationNames;
    QMap<QString, qint64> modeRevenue;
    QMap<QString, qint64> currentOrderStates;
    QList<qint64> startPeriods{0, 0, 0, 0, 0, 0};
    bool valid = true;
};

inline RevenueAnalysis analyzeRevenue(const QJsonArray &items, const QDate &start, const QDate &end)
{
    RevenueAnalysis result;
    if (!start.isValid() || !end.isValid() || start > end || start.daysTo(end) > 365) {
        result.valid = false; return result;
    }
    for (QDate day = start; day <= end; day = day.addDays(1)) {
        result.dailyOrders[day] = 0; result.dailyEnergy[day] = 0;
    }
    const QTimeZone zone("Asia/Shanghai");
    const QDate previousStart = start.addDays(-start.daysTo(end) - 1);
    for (const auto &value : items) {
        protocol::OrderDto order;
        if (!value.isObject() || !protocol::fromJson(value.toObject(), &order)) {
            result.valid = false; return result;
        }
        ++result.currentOrderStates[protocol::toString(order.status)];
        if (order.status == protocol::OrderStatus::PendingPayment) result.pendingCents += order.amountCents;
        if (order.status != protocol::OrderStatus::Completed || !order.paidAt) continue;
        const auto paid = QDateTime::fromString(*order.paidAt, Qt::ISODate).toTimeZone(zone);
        if (!paid.isValid()) { result.valid = false; return result; }
        if (paid.date() >= previousStart && paid.date() < start) result.previousCents += order.amountCents;
        if (paid.date() < start || paid.date() > end) continue;
        result.receivedCents += order.amountCents;
        ++result.paidOrders;
        result.energyWh += order.energyWh;
        ++result.dailyOrders[paid.date()];
        result.dailyEnergy[paid.date()] += order.energyWh;
        result.stationRevenue[order.stationId] += order.amountCents;
        result.stationNames[order.stationId] = order.stationName.isEmpty()
            ? QStringLiteral("站点 #%1").arg(order.stationId) : order.stationName;
        result.modeRevenue[protocol::toString(order.mode)] += order.amountCents;
        if (order.startedAt) {
            const auto started = QDateTime::fromString(*order.startedAt, Qt::ISODate).toTimeZone(zone);
            if (started.isValid()) ++result.startPeriods[started.time().hour() / 4];
        }
    }
    return result;
}
} // namespace charging::server
