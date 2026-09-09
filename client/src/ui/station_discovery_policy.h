#pragma once

#include "charging/protocol/dto.h"
#include <QList>
#include <algorithm>
#include <cmath>

// 本文件提供客户端本地的附近电站筛选与推荐规则
namespace charging::client::discovery {
constexpr double preferredRadiusKm = 10.0;
constexpr double maximumRadiusKm = 30.0;
constexpr int nearbyLimit = 4;

// 判断电站是否营业、有空闲桩且距离在给定半径内
inline bool availableWithin(const protocol::StationDto &s, double radius)
{
    return s.status == protocol::StationStatus::Active && s.availablePileCount > 0
        && s.totalPileCount > 0 && s.distanceKm && std::isfinite(*s.distanceKm)
        && *s.distanceKm >= 0 && *s.distanceKm <= radius;
}

// 排序规则：先近后空闲多，再便宜，最后按ID稳定排序
inline bool nearer(const protocol::StationDto &a, const protocol::StationDto &b)
{
    if (*a.distanceKm != *b.distanceKm) return *a.distanceKm < *b.distanceKm;
    if (a.availablePileCount != b.availablePileCount) return a.availablePileCount > b.availablePileCount;
    if (a.priceCentsPerKwh != b.priceCentsPerKwh) return a.priceCentsPerKwh < b.priceCentsPerKwh;
    return a.stationId < b.stationId;
}

// Bounded, explainable product heuristic, not a learned preference model.
// 距离、空闲、价格、拥堵加权打分，权重固定可解释
inline double score(const protocol::StationDto &s, double radius)
{
    const double distance = 1.0 - *s.distanceKm / radius;
    const double availability = .6 * std::min<qint64>(s.availablePileCount, 4) / 4.0
        + .4 * std::clamp(double(s.availablePileCount) / s.totalPileCount, 0.0, 1.0);
    const double price = 1.0 / (1.0 + std::max(0LL, static_cast<long long>(s.priceCentsPerKwh)) / 100.0);
    double congestion = .5;
    if (s.predictedCongestion == protocol::CongestionLevel::Low) congestion = 1;
    if (s.predictedCongestion == protocol::CongestionLevel::High) congestion = 0;
    return 55 * distance + 25 * availability + 15 * price + 5 * congestion;
}

// 取最大半径内可用电站，按距离排序后截取前几个
inline QList<protocol::StationDto> nearby(const QList<protocol::StationDto> &stations)
{
    QList<protocol::StationDto> result;
    for (const auto &s : stations) if (availableWithin(s, maximumRadiusKm)) result.append(s);
    std::sort(result.begin(), result.end(), nearer);
    return result.mid(0, nearbyLimit);
}

// 优先10公里内推荐，若都不可用再放宽到30公里
inline qint64 recommend(const QList<protocol::StationDto> &stations)
{
    double radius = preferredRadiusKm;
    if (std::none_of(stations.cbegin(), stations.cend(), [](const auto &s) { return availableWithin(s, preferredRadiusKm); }))
        radius = maximumRadiusKm;
    const protocol::StationDto *best = nullptr;
    for (const auto &s : stations) {
        if (!availableWithin(s, radius)) continue;
        if (!best || score(s, radius) > score(*best, radius)
            || (score(s, radius) == score(*best, radius) && nearer(s, *best))) best = &s;
    }
    return best ? best->stationId : 0;
}
} // namespace charging::client::discovery
