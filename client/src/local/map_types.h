// 地图相关的数据类型：位置、地址解析与路线规划结果
#pragma once

#include <QJsonArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace charging::client {

// 出行方式枚举：驾车、步行、公交、骑行
enum class RouteMode {
    Driving,
    Walking,
    Transit,
    Cycling,
};

// 一个地点：地址文字加经纬度
struct MapLocation {
    QString address;
    double longitude = 0.0;
    double latitude = 0.0;
};

// 地址解析结果，失败时 location 为空
struct GeocodeResult {
    QString requestId;
    bool success = false;
    QString message;
    std::optional<MapLocation> location;
};

// 路线结果：摘要、折线点集与文字指引
struct RouteResult {
    QString requestId;
    bool success = false;
    QString message;
    QString summary;
    // Client-local data only. No remote route page or route-dependent HTML.
    // 地图脚本地址，仅在成功且已配置 Key 时填充
    QUrl mapScriptUrl;
    QJsonArray paths;  // {points: [[latitude, longitude], ...], walking: bool}
    QStringList instructions;
};

}  // namespace charging::client

// 注册元类型，便于在信号槽中传递这些结构
Q_DECLARE_METATYPE(charging::client::MapLocation)
Q_DECLARE_METATYPE(charging::client::GeocodeResult)
Q_DECLARE_METATYPE(charging::client::RouteResult)
