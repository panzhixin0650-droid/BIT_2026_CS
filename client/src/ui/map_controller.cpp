// 地图控制器：把页面的定位与路线操作转给地图服务
#include "ui/map_controller.h"

#include "charging/protocol/dto.h"
#include "local/i_map_service.h"
#include "ui/station_browser_page.h"

#include <cmath>

namespace charging::client {
namespace {

// 校验经纬度是否有效可用
bool validCoordinate(const MapLocation &location)
{
    return std::isfinite(location.longitude) && std::isfinite(location.latitude)
        && location.longitude >= -180.0 && location.longitude <= 180.0
        && location.latitude >= -90.0 && location.latitude <= 90.0;
}

}  // namespace

// 连接页面操作与地图服务的异步回调
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
    connect(&page_, &StationBrowserPage::navigationClosed,
            this, &MapController::cancelRoute);
    connect(&mapService_, &IMapService::geocodeCompleted,
            this, &MapController::handleGeocode);
    connect(&mapService_, &IMapService::routeCompleted,
            this, &MapController::handleRoute);
}

// 重置时取消未完成的地理编码与路线请求
void MapController::reset()
{
    mapService_.cancel(pendingGeocodeRequestId_);
    mapService_.cancel(pendingRouteRequestId_);
    pendingGeocodeRequestId_.clear();
    pendingRouteRequestId_.clear();
    geocodePurpose_ = GeocodePurpose::None;
    routeDestination_ = {};
}

// 解析地址；演示位置直接使用固定坐标
void MapController::resolveLocation(const QString &address)
{
    if (!pendingGeocodeRequestId_.isEmpty()) {
        return;
    }
    if (address.trimmed() == QStringLiteral("演示位置")) {
        page_.setResolvedLocation({QStringLiteral("演示位置"), 123.42, 41.70});
        page_.showLocationMessage(QStringLiteral("已恢复默认位置"));
        emit locationChanged();
        return;
    }
    page_.setLocationBusy(true);
    page_.showLocationMessage(QStringLiteral("正在解析地址…"));
    geocodePurpose_ = GeocodePurpose::LocationSelection;
    pendingGeocodeRequestId_ = mapService_.geocode(address);
}

// 打开导航面板并记录目标站点为终点
void MapController::openNavigation(const protocol::StationDto &station)
{
    cancelRoute();
    routeDestination_ = {station.address, station.longitude, station.latitude};
    page_.showNavigation(station, page_.currentLocation());
}

// 关闭导航时取消路线及起点解析
void MapController::cancelRoute()
{
    mapService_.cancel(pendingRouteRequestId_);
    pendingRouteRequestId_.clear();
    if (geocodePurpose_ == GeocodePurpose::RouteStart) {
        mapService_.cancel(pendingGeocodeRequestId_);
        pendingGeocodeRequestId_.clear();
        geocodePurpose_ = GeocodePurpose::None;
    }
    page_.setRouteBusy(false);
}

// 起点与当前定位一致则跳过解析直接算路
void MapController::requestRoute(const QString &startAddress, RouteMode mode)
{
    if (!pendingGeocodeRequestId_.isEmpty() || !pendingRouteRequestId_.isEmpty()) {
        return;
    }
    page_.setRouteBusy(true);
    pendingRouteMode_ = mode;
    const MapLocation current = page_.currentLocation();
    if (startAddress.trimmed() == current.address.trimmed()
        && validCoordinate(current)) {
        page_.showRouteMessage(QStringLiteral("正在生成路线…"));
        pendingRouteRequestId_ =
            mapService_.openRoute(current, routeDestination_, pendingRouteMode_);
        return;
    }

    page_.showRouteMessage(QStringLiteral("正在解析起点…"));
    geocodePurpose_ = GeocodePurpose::RouteStart;
    pendingGeocodeRequestId_ = mapService_.geocode(startAddress);
}

// 按请求用途区分：起点解析或定位更新
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

// 路线结果回调：失败提示，成功展示路线
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
