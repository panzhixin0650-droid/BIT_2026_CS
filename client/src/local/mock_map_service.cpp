#include "local/mock_map_service.h"

#include <QTimer>

#include <cmath>

namespace charging::client {

namespace {

bool validCoordinate(const MapLocation &location)
{
    return std::isfinite(location.longitude) && std::isfinite(location.latitude)
        && location.longitude >= -180.0 && location.longitude <= 180.0
        && location.latitude >= -90.0 && location.latitude <= 90.0;
}

std::optional<MapLocation> locationForAddress(const QString &address)
{
    if (address.contains(QStringLiteral("和平"))) {
        return MapLocation{address, 123.40, 41.79};
    }
    if (address.contains(QStringLiteral("浑南"))) {
        return MapLocation{address, 123.43, 41.71};
    }
    if (address.contains(QStringLiteral("演示"))) {
        return MapLocation{address, 123.42, 41.70};
    }
    return std::nullopt;
}

}  // namespace

QString MockMapService::geocode(const QString &address)
{
    const QString requestId = nextRequestId();
    const QString normalizedAddress = address.trimmed();
    QTimer::singleShot(0, this, [this, requestId, normalizedAddress]() {
        GeocodeResult result;
        result.requestId = requestId;
        if (normalizedAddress.isEmpty()) {
            result.message = QStringLiteral("请输入要定位的地址");
        } else if (normalizedAddress.contains(QStringLiteral("无法解析"))
                   || normalizedAddress.contains(QStringLiteral("不存在"))) {
            result.message = QStringLiteral("未能解析该地址，请修改后重试");
        } else if (const auto location = locationForAddress(normalizedAddress);
                   location.has_value()) {
            result.success = true;
            result.message = QStringLiteral("位置解析成功");
            result.location = location;
        } else {
            result.message = QStringLiteral(
                "当前 Mock 仅支持演示位置、和平区和浑南区；其他地址需接入腾讯地图");
        }
        emit geocodeCompleted(result);
    });
    return requestId;
}

QString MockMapService::openRoute(const MapLocation &start,
                                  const MapLocation &end,
                                  RouteMode mode)
{
    const QString requestId = nextRequestId();
    QTimer::singleShot(0, this, [this, requestId, start, end, mode]() {
        RouteResult result;
        result.requestId = requestId;
        if (start.address.trimmed().isEmpty() || end.address.trimmed().isEmpty()
            || !validCoordinate(start) || !validCoordinate(end)) {
            result.message = QStringLiteral("路线起点或终点无效");
        } else {
            result.success = true;
            result.message = QStringLiteral("Mock 路线已生成");
            result.summary = QStringLiteral("%1\n从：%2\n到：%3\n"
                                            "当前为离线 Mock 路线；安装 WebEngine 并配置腾讯地图后，"
                                            "此区域将加载真实路线页面。")
                                 .arg(mode == RouteMode::Driving
                                          ? QStringLiteral("驾车路线")
                                          : QStringLiteral("步行路线"),
                                      start.address,
                                      end.address);
        }
        emit routeCompleted(result);
    });
    return requestId;
}

QString MockMapService::nextRequestId()
{
    return QStringLiteral("map-mock-%1").arg(nextRequestNumber_++);
}

}  // namespace charging::client
