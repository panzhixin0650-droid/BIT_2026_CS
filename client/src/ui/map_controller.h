#pragma once

#include "local/map_types.h"

#include <QObject>
#include <QString>

namespace charging::protocol {
struct StationDto;
}

namespace charging::client {

class IMapService;
class StationBrowserPage;

class MapController final : public QObject {
    Q_OBJECT

public:
    MapController(StationBrowserPage &page,
                  IMapService &mapService,
                  QObject *parent = nullptr);
    void reset();
    void openNavigation(const protocol::StationDto &station);

signals:
    void locationChanged();

private:
    enum class GeocodePurpose { None, LocationSelection, RouteStart };

    void resolveLocation(const QString &address);
    void requestRoute(const QString &startAddress, RouteMode mode);
    void handleGeocode(const GeocodeResult &result);
    void handleRoute(const RouteResult &result);

    StationBrowserPage &page_;
    IMapService &mapService_;
    QString pendingGeocodeRequestId_;
    QString pendingRouteRequestId_;
    GeocodePurpose geocodePurpose_ = GeocodePurpose::None;
    MapLocation routeDestination_;
    RouteMode pendingRouteMode_ = RouteMode::Driving;
};

}  // namespace charging::client
