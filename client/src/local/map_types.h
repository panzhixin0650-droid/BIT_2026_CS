#pragma once

#include <QMetaType>
#include <QString>
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
    QUrl routeUrl;
    QString routeHtml;
};

}  // namespace charging::client

Q_DECLARE_METATYPE(charging::client::MapLocation)
Q_DECLARE_METATYPE(charging::client::GeocodeResult)
Q_DECLARE_METATYPE(charging::client::RouteResult)
