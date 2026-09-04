#include "ui/map_controller.h"

#include "charging/protocol/dto.h"
#include "local/i_map_service.h"
#include "ui/station_browser_page.h"

namespace charging::client {

MapController::MapController(StationBrowserPage &page,
                             IMapService &mapService,
                             QObject *parent)
    : QObject(parent), page_(page), mapService_(mapService)
{
    connect(&page_, &StationBrowserPage::locationResolutionRequested,
            this, &MapController::resolveLocation);
    connect(&page_, &StationBrowserPage::navigationRequested,
            this, &MapController::openNavigation);
    connect(&page_, &StationBrowserPage::routeRequested,
            this, &MapController::requestRoute);
    connect(&mapService_, &IMapService::geocodeCompleted,
            this, &MapController::handleGeocode);
    connect(&mapService_, &IMapService::routeCompleted,
            this, &MapController::handleRoute);
}

void MapController::reset()
{
    pendingGeocodeRequestId_.clear();
    pendingRouteRequestId_.clear();
    geocodePurpose_ = GeocodePurpose::None;
    routeDestination_ = {};
}

void MapController::resolveLocation(const QString &address)
{
    if (!pendingGeocodeRequestId_.isEmpty()) {
        return;
    }
    page_.setLocationBusy(true);
    page_.showLocationMessage(QStringLiteral("正在解析地址…"));
    geocodePurpose_ = GeocodePurpose::LocationSelection;
    pendingGeocodeRequestId_ = mapService_.geocode(address);
}

void MapController::openNavigation(const protocol::StationDto &station)
{
    routeDestination_ = {station.address, station.longitude, station.latitude};
    page_.showNavigation(station, page_.currentLocation());
}

void MapController::requestRoute(const QString &startAddress, RouteMode mode)
{
    if (!pendingGeocodeRequestId_.isEmpty() || !pendingRouteRequestId_.isEmpty()) {
        return;
    }
    page_.setRouteBusy(true);
    page_.showRouteMessage(QStringLiteral("正在解析起点…"));
    pendingRouteMode_ = mode;
    geocodePurpose_ = GeocodePurpose::RouteStart;
    pendingGeocodeRequestId_ = mapService_.geocode(startAddress);
}

void MapController::handleGeocode(const GeocodeResult &result)
{
    if (result.requestId != pendingGeocodeRequestId_) {
        return;
    }
    pendingGeocodeRequestId_.clear();
    const GeocodePurpose purpose = geocodePurpose_;
    geocodePurpose_ = GeocodePurpose::None;
    if (!result.success || !result.location.has_value()) {
        if (purpose == GeocodePurpose::RouteStart) {
            page_.setRouteBusy(false);
            page_.showRouteMessage(result.message.isEmpty()
                                       ? QStringLiteral("起点解析失败，请修改后重试")
                                       : result.message,
                                   true);
        } else {
            page_.setLocationBusy(false);
            page_.showLocationMessage(result.message.isEmpty()
                                          ? QStringLiteral("地址解析失败，请修改后重试")
                                          : result.message,
                                      true);
        }
        return;
    }

    if (purpose == GeocodePurpose::RouteStart) {
        page_.showRouteMessage(QStringLiteral("正在生成路线…"));
        pendingRouteRequestId_ =
            mapService_.openRoute(*result.location, routeDestination_, pendingRouteMode_);
        return;
    }

    page_.setResolvedLocation(*result.location);
    page_.setLocationBusy(false);
    page_.showLocationMessage(QStringLiteral("位置已更新，充电站距离已重新计算"));
    emit locationChanged();
}

void MapController::handleRoute(const RouteResult &result)
{
    if (result.requestId != pendingRouteRequestId_) {
        return;
    }
    pendingRouteRequestId_.clear();
    page_.setRouteBusy(false);
    if (!result.success) {
        page_.showRouteMessage(result.message.isEmpty()
                                   ? QStringLiteral("路线规划失败，请稍后重试")
                                   : result.message,
                               true);
        return;
    }
    page_.showRouteResult(result);
}

}  // namespace charging::client
