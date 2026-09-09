// 离线 Mock 地图服务：返回演示用的定位与文字路线，不访问网络
#include "local/mock_map_service.h"

#include <QTimer>

#include <cmath>

namespace charging::client {

namespace {

// 判断经纬度是否为有限值且在合法范围内
bool validCoordinate(const MapLocation &location)
{
    return std::isfinite(location.longitude) && std::isfinite(location.latitude)
        && location.longitude >= -180.0 && location.longitude <= 180.0
        && location.latitude >= -90.0 && location.latitude <= 90.0;
}

// Mock 只识别几个演示地址关键字，其余返回空
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

// 地址解析：先生成请求编号，再用 singleShot 异步回报结果
QString MockMapService::geocode(const QString &address)
{
    const QString requestId = nextRequestId();
    const QString normalizedAddress = address.trimmed();
    QTimer::singleShot(0, this, [this, requestId, normalizedAddress]() {
        GeocodeResult result;
        result.requestId = requestId;
        // 依次处理空地址、指定失败关键字与未知地址
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

// 路线规划：校验起终点和出行方式后拼出提示文本
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
        } else if (mode != RouteMode::Driving && mode != RouteMode::Walking
                   && mode != RouteMode::Transit && mode != RouteMode::Cycling) {
            result.message = QStringLiteral("不支持的出行方式");
        } else {
            result.success = true;
            result.message = QStringLiteral("Mock 路线已生成");
            result.summary = QStringLiteral("%1\n从：%2\n到：%3\n"
                                            "当前为离线 Mock 路线；安装 WebEngine 并配置腾讯地图后，"
                                            "此区域将绘制真实路线，支持拖动和缩放。")
                                 .arg(mode == RouteMode::Driving
                                          ? QStringLiteral("驾车路线")
                                          : mode == RouteMode::Walking ? QStringLiteral("步行路线")
                                                                       : mode == RouteMode::Transit ? QStringLiteral("公共交通路线")
                                                                       : QStringLiteral("骑行路线"),
                                      start.address,
                                      end.address);
        }
        emit routeCompleted(result);
    });
    return requestId;
}

// 请求编号自增，便于界面匹配对应回调
QString MockMapService::nextRequestId()
{
    return QStringLiteral("map-mock-%1").arg(nextRequestNumber_++);
}

}  // namespace charging::client
