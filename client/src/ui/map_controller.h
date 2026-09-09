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

// 连接站点浏览页与地图服务的控制器
class MapController final : public QObject {
    Q_OBJECT

public:
    // 构造时绑定页面与地图服务引用
    MapController(StationBrowserPage &page,
                  IMapService &mapService,
                  QObject *parent = nullptr);
    // 重置未完成请求，或直接打开某站点导航
    void reset();
    void openNavigation(const protocol::StationDto &station);

signals:
    void locationChanged();

private:
    // 标记本次地理编码是给定位还是路线起点用
    enum class GeocodePurpose { None, LocationSelection, RouteStart };

    // 页面请求处理与服务回调处理
    void resolveLocation(const QString &address);
    void requestRoute(const QString &startAddress, RouteMode mode);
    void handleGeocode(const GeocodeResult &result);
    void handleRoute(const RouteResult &result);
    void cancelRoute();

    StationBrowserPage &page_;
    IMapService &mapService_;
    // 记录在途请求编号，用于匹配与取消
    QString pendingGeocodeRequestId_;
    QString pendingRouteRequestId_;
    GeocodePurpose geocodePurpose_ = GeocodePurpose::None;
    MapLocation routeDestination_;
    RouteMode pendingRouteMode_ = RouteMode::Driving;
};

}  // namespace charging::client
