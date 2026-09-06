#include "local/tencent_map_service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QTimer>
#include <QUrlQuery>
#include <QVector>

#include <cmath>

namespace charging::client {
namespace {

bool validCoordinate(const MapLocation &location)
{
    return std::isfinite(location.longitude) && std::isfinite(location.latitude)
        && location.longitude >= -180.0 && location.longitude <= 180.0
        && location.latitude >= -90.0 && location.latitude <= 90.0;
}

QString coordinateText(const MapLocation &location)
{
    return QStringLiteral("%1,%2")
        .arg(location.latitude, 0, 'f', 6)
        .arg(location.longitude, 0, 'f', 6);
}

QString apiKeyConfigurationError(const QString &apiKey)
{
    if (apiKey.isEmpty()) {
        return QStringLiteral("腾讯地图 Key 未配置，请切回 Mock 或配置后重试");
    }
    if (apiKey.startsWith(QLatin1Char('\''))
        || apiKey.endsWith(QLatin1Char('\''))
        || apiKey.startsWith(QLatin1Char('"'))
        || apiKey.endsWith(QLatin1Char('"'))) {
        return QStringLiteral(
            "腾讯地图 Key 配置格式错误：Value 只填写 Key 本身，不要包含引号");
    }
    if (apiKey.contains(QStringLiteral("TENCENT_MAP_KEY"),
                        Qt::CaseInsensitive)
        || apiKey.contains(QLatin1Char('='))) {
        return QStringLiteral(
            "腾讯地图 Key 配置格式错误：Value 中不要填写 TENCENT_MAP_KEY=");
    }
    for (const QChar character : apiKey) {
        if (character.isSpace()) {
            return QStringLiteral("腾讯地图 Key 配置格式错误：Key 中不能包含空白字符");
        }
    }
    return {};
}

std::optional<QJsonArray> decodeRoutePolyline(const QJsonArray &encoded)
{
    if (encoded.size() < 4 || encoded.size() % 2 != 0 || encoded.size() > 100000) {
        return std::nullopt;
    }

    QVector<double> coordinates;
    coordinates.reserve(encoded.size());
    for (const QJsonValue &value : encoded) {
        if (!value.isDouble()) {
            return std::nullopt;
        }
        coordinates.append(value.toDouble());
    }
    for (qsizetype index = 2; index < coordinates.size(); ++index) {
        coordinates[index] = coordinates[index - 2]
            + coordinates[index] / 1000000.0;
    }

    QJsonArray points;
    for (qsizetype index = 0; index < coordinates.size(); index += 2) {
        const MapLocation point{
            QString(), coordinates[index + 1], coordinates[index]};
        if (!validCoordinate(point)) {
            return std::nullopt;
        }
        QJsonArray pair;
        pair.append(point.latitude);
        pair.append(point.longitude);
        points.append(pair);
    }
    return points;
}

QString routeModeLabel(RouteMode mode)
{
    switch (mode) {
    case RouteMode::Driving: return QStringLiteral("驾车");
    case RouteMode::Walking: return QStringLiteral("步行");
    case RouteMode::Transit: return QStringLiteral("公共交通");
    case RouteMode::Cycling: return QStringLiteral("骑行");
    }
    return {};
}

QString routeEndpoint(RouteMode mode)
{
    switch (mode) {
    case RouteMode::Driving: return QStringLiteral("driving");
    case RouteMode::Walking: return QStringLiteral("walking");
    case RouteMode::Transit: return QStringLiteral("transit");
    case RouteMode::Cycling: return QStringLiteral("bicycling");
    }
    return {};
}

QString distanceText(double meters)
{
    return meters >= 1000 ? QStringLiteral("%1 公里").arg(meters / 1000, 0, 'f', 1)
                          : QStringLiteral("%1 米").arg(qRound(meters));
}

bool appendPath(const QJsonArray &encoded, bool walking, RouteResult &result)
{
    const auto points = decodeRoutePolyline(encoded);
    if (!points) return false;
    result.paths.append(QJsonObject{{QStringLiteral("points"), *points},
                                   {QStringLiteral("walking"), walking}});
    return true;
}

bool parseRoute(const QJsonObject &route, RouteMode mode, RouteResult &result)
{
    const auto distance = route.value(QStringLiteral("distance"));
    const auto duration = route.value(QStringLiteral("duration"));
    if (!distance.isDouble() || !duration.isDouble()
        || !std::isfinite(distance.toDouble()) || !std::isfinite(duration.toDouble())
        || distance.toDouble() < 0 || duration.toDouble() < 0
        || distance.toDouble() > 50000000 || duration.toDouble() > 1000000)
        return false;
    const auto steps = route.value(QStringLiteral("steps")).toArray();
    if (mode != RouteMode::Transit) {
        if (!appendPath(route.value(QStringLiteral("polyline")).toArray(),
                        mode == RouteMode::Walking, result)) return false;
        for (const auto &step : steps) {
            const QString instruction = step.toObject().value(QStringLiteral("instruction")).toString().trimmed();
            if (!instruction.isEmpty()) result.instructions.append(instruction);
        }
    } else {
        if (steps.isEmpty() || steps.size() > 100) return false;
        for (const auto &value : steps) {
            const auto step = value.toObject();
            const QString legMode = step.value(QStringLiteral("mode")).toString();
            if (legMode == QStringLiteral("WALKING")) {
                const auto polyline = step.value(QStringLiteral("polyline")).toArray();
                // A zero-length transfer may have no geometry.
                if (polyline.isEmpty() && step.value(QStringLiteral("distance")).isDouble()
                    && step.value(QStringLiteral("distance")).toDouble() == 0) continue;
                if (!appendPath(polyline, true, result)) return false;
                const auto legDistance = step.value(QStringLiteral("distance"));
                result.instructions.append(legDistance.isDouble() && legDistance.toDouble() >= 0
                    ? QStringLiteral("步行 %1").arg(distanceText(legDistance.toDouble()))
                    : QStringLiteral("步行换乘"));
            } else if (legMode == QStringLiteral("TRANSIT")) {
                const auto lines = step.value(QStringLiteral("lines")).toArray();
                if (lines.isEmpty()) return false;
                // Alternative lines share stops; the first has complete geometry.
                const auto line = lines.first().toObject();
                if (!appendPath(line.value(QStringLiteral("polyline")).toArray(), false, result)) return false;
                const QString title = line.value(QStringLiteral("title")).toString();
                const QString geton = line.value(QStringLiteral("geton")).toObject().value(QStringLiteral("title")).toString();
                const QString getoff = line.value(QStringLiteral("getoff")).toObject().value(QStringLiteral("title")).toString();
                if (title.isEmpty() || geton.isEmpty() || getoff.isEmpty()) return false;
                result.instructions.append(QStringLiteral("%1：%2 上车 → %3 下车").arg(title, geton, getoff));
                switch (line.value(QStringLiteral("running_status")).toInt()) {
                case 301: result.instructions.append(QStringLiteral("注意：可能错过末班车")); break;
                case 302: result.instructions.append(QStringLiteral("注意：首班车还未发出")); break;
                case 303: result.instructions.append(QStringLiteral("注意：该线路停运，请重新选择出行方式")); break;
                default: break;
                }
            } else {
                return false;
            }
        }
    }
    if (result.paths.isEmpty()) return false;
    result.summary = QStringLiteral("%1约 %2 · %3 分钟")
        .arg(routeModeLabel(mode), distanceText(distance.toDouble()))
        .arg(static_cast<int>(std::ceil(duration.toDouble())));
    return true;
}

}  // namespace

TencentMapService::TencentMapService(QString apiKey,
                                     int requestTimeoutMs,
                                     QObject *parent,
                                     QNetworkAccessManager *networkAccess)
    : IMapService(parent)
    , apiKey_(apiKey.trimmed())
    , requestTimeoutMs_(qMax(1, requestTimeoutMs))
    , networkAccess_(networkAccess ? networkAccess : &network_)
{
    if (!networkAccess) {
        // This adapter talks directly to Tencent. Explicitly bypass desktop
        // WPAD/PAC discovery, which can stall each cold request for tens of
        // seconds when GNOME is set to an empty automatic-proxy profile.
        network_.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
}

TencentMapService::~TencentMapService()
{
    const auto requests = activeRequests_.values();
    for (const auto &id : requests) cancel(id);
}

QUrl TencentMapService::mapScriptUrl() const
{
    if (!apiKeyConfigurationError(apiKey_).isEmpty()) return {};

    QUrl url(QStringLiteral("https://map.qq.com/api/gljs"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), QStringLiteral("1.exp"));
    query.addQueryItem(QStringLiteral("key"), apiKey_);
    url.setQuery(query);
    return url;
}

QString TencentMapService::geocode(const QString &address)
{
    const QString requestId = nextRequestId();
    activeRequests_.insert(requestId);
    const QString normalizedAddress = address.trimmed();
    if (normalizedAddress.isEmpty()) {
        emitGeocodeFailure(requestId, QStringLiteral("请输入要定位的地址"));
        return requestId;
    }
    const QString configurationError = apiKeyConfigurationError(apiKey_);
    if (!configurationError.isEmpty()) {
        emitGeocodeFailure(requestId, configurationError);
        return requestId;
    }

    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("address"), normalizedAddress);
    query.addQueryItem(QStringLiteral("key"), apiKey_);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BIT-ChargingClient/1.0"));
    request.setTransferTimeout(requestTimeoutMs_);
    QNetworkReply *reply = networkAccess_->get(request);
    replies_.insert(requestId, reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, normalizedAddress]() {
                replies_.remove(requestId);
                if (!activeRequests_.remove(requestId)) { reply->deleteLater(); return; }
                GeocodeResult result;
                result.requestId = requestId;
                if (reply->error() != QNetworkReply::NoError) {
                    result.message = QStringLiteral("腾讯地图地址解析失败，请检查网络后重试");
                    reply->deleteLater();
                    emit geocodeCompleted(result);
                    return;
                }

                QJsonParseError parseError;
                const QJsonDocument document =
                    QJsonDocument::fromJson(reply->readAll(), &parseError);
                reply->deleteLater();
                if (parseError.error != QJsonParseError::NoError
                    || !document.isObject()) {
                    result.message = QStringLiteral("腾讯地图返回了无法识别的数据");
                    emit geocodeCompleted(result);
                    return;
                }

                const QJsonObject root = document.object();
                const int status = root.value(QStringLiteral("status")).toInt(-1);
                const QJsonObject location =
                    root.value(QStringLiteral("result"))
                        .toObject()
                        .value(QStringLiteral("location"))
                        .toObject();
                const QJsonValue longitude = location.value(QStringLiteral("lng"));
                const QJsonValue latitude = location.value(QStringLiteral("lat"));
                if (status != 0 || !longitude.isDouble() || !latitude.isDouble()) {
                    const QString serviceMessage =
                        root.value(QStringLiteral("message")).toString().trimmed();
                    result.message = serviceMessage.isEmpty()
                        ? QStringLiteral("腾讯地图地址解析失败（状态码 %1）")
                              .arg(status)
                        : QStringLiteral("腾讯地图地址解析失败（状态码 %1）：%2")
                              .arg(status)
                              .arg(serviceMessage);
                    emit geocodeCompleted(result);
                    return;
                }

                const MapLocation resolved{normalizedAddress,
                                           longitude.toDouble(),
                                           latitude.toDouble()};
                if (!validCoordinate(resolved)) {
                    result.message = QStringLiteral("腾讯地图返回的经纬度无效");
                    emit geocodeCompleted(result);
                    return;
                }
                result.success = true;
                result.message = QStringLiteral("位置解析成功");
                result.location = resolved;
                emit geocodeCompleted(result);
            });
    return requestId;
}

QString TencentMapService::openRoute(const MapLocation &start, const MapLocation &end, RouteMode mode)
{
    const QString requestId = nextRequestId();
    activeRequests_.insert(requestId);
    QTimer::singleShot(0, this, [this, requestId, start, end, mode]() {
        if (!activeRequests_.contains(requestId)) return;
        RouteResult result;
        result.requestId = requestId;
        result.message = apiKeyConfigurationError(apiKey_);
        if (result.message.isEmpty() && (start.address.trimmed().isEmpty()
            || end.address.trimmed().isEmpty() || !validCoordinate(start) || !validCoordinate(end)))
            result.message = QStringLiteral("路线起点或终点无效");
        if (result.message.isEmpty() && routeEndpoint(mode).isEmpty())
            result.message = QStringLiteral("不支持的出行方式");
        if (!result.message.isEmpty()) {
            activeRequests_.remove(requestId);
            emit routeCompleted(result);
            return;
        }
        QUrl url(QStringLiteral("https://apis.map.qq.com/ws/direction/v1/%1/").arg(routeEndpoint(mode)));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("from"), coordinateText(start));
        query.addQueryItem(QStringLiteral("to"), coordinateText(end));
        query.addQueryItem(QStringLiteral("key"), apiKey_);
        url.setQuery(query);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("BIT-ChargingClient/1.0"));
        request.setTransferTimeout(requestTimeoutMs_);
        auto *reply = networkAccess_->get(request);
        replies_.insert(requestId, reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, mode]() {
            replies_.remove(requestId);
            reply->deleteLater();
            if (!activeRequests_.remove(requestId)) return;
            RouteResult result;
            result.requestId = requestId;
            const QString label = routeModeLabel(mode);
            const QByteArray body = reply->readAll();
            const auto document = body.size() <= 8 * 1024 * 1024
                ? QJsonDocument::fromJson(body) : QJsonDocument();
            const auto root = document.object();
            const int status = root.value(QStringLiteral("status")).toInt(-1);
            const auto routes = root.value(QStringLiteral("result")).toObject().value(QStringLiteral("routes")).toArray();
            if (reply->error() != QNetworkReply::NoError) {
                result.message = QStringLiteral("腾讯地图%1路线请求失败或超时，请检查网络后重试").arg(label);
            } else if (!document.isObject()) {
                result.message = QStringLiteral("腾讯地图返回了无法识别的%1路线数据").arg(label);
            } else if (mode == RouteMode::Transit && status == 348) {
                result.message = QStringLiteral(
                    "未找到可用的公共交通路线（腾讯状态码 348），请更换起点或出行方式");
            } else if (status != 0) {
                result.message = QStringLiteral("腾讯地图%1路线请求失败（状态码 %2），请检查 Key 权限、配额及起终点")
                    .arg(label).arg(status);
            } else if (routes.isEmpty()) {
                result.message = QStringLiteral("未找到可用的%1路线，请修改起点或出行方式").arg(label);
            } else if (!parseRoute(routes.first().toObject(), mode, result)) {
                result.paths = {};
                result.instructions.clear();
                result.message = QStringLiteral("腾讯地图返回的%1路线坐标或详情无效").arg(label);
            } else {
                result.success = true;
                result.message = QStringLiteral("腾讯地图%1路线规划成功").arg(label);
                result.mapScriptUrl = mapScriptUrl();
            }
            emit routeCompleted(result);
        });
    });
    return requestId;
}

void TencentMapService::cancel(const QString &requestId)
{
    activeRequests_.remove(requestId);
    if (auto *reply = replies_.take(requestId)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

QString TencentMapService::nextRequestId()
{
    return QStringLiteral("map-tencent-%1").arg(nextRequestNumber_++);
}

void TencentMapService::emitGeocodeFailure(const QString &requestId,
                                           const QString &message)
{
    QTimer::singleShot(0, this, [this, requestId, message]() {
        if (activeRequests_.remove(requestId))
            emit geocodeCompleted(GeocodeResult{requestId, false, message, std::nullopt});
    });
}

}  // namespace charging::client
