#pragma once

#include <QJsonArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace charging::client {

enum class RouteMode {
    Driving,
    Walking,
    Transit,
    Cycling,
};

struct MapLocation {
    QString address;
    double longitude = 0.0;
    double latitude = 0.0;
};

struct GeocodeResult {
    QString requestId;
    bool success = false;
    QString message;
    std::optional<MapLocation> location;
};

struct RouteResult {
    QString requestId;
    bool success = false;
    QString message;
    QString summary;
    // Client-local data only. No remote route page or route-dependent HTML.
    QUrl mapScriptUrl;
    QJsonArray paths;  // {points: [[latitude, longitude], ...], walking: bool}
    QStringList instructions;
};

}  // namespace charging::client

Q_DECLARE_METATYPE(charging::client::MapLocation)
Q_DECLARE_METATYPE(charging::client::GeocodeResult)
Q_DECLARE_METATYPE(charging::client::RouteResult)
