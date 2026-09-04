#include "local/tencent_map_service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
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

std::optional<QJsonArray> decodeWalkingPolyline(const QJsonArray &encoded)
{
    if (encoded.size() < 4 || encoded.size() % 2 != 0) {
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

QString walkingRouteHtml(const QString &apiKey,
                         const QJsonArray &points,
                         int distanceMeters,
                         int durationMinutes)
{
    QUrl scriptUrl(QStringLiteral("https://map.qq.com/api/gljs"));
    QUrlQuery scriptQuery;
    scriptQuery.addQueryItem(QStringLiteral("v"), QStringLiteral("1.exp"));
    scriptQuery.addQueryItem(QStringLiteral("key"), apiKey);
    scriptUrl.setQuery(scriptQuery);

    const QString pointsJson = QString::fromUtf8(
        QJsonDocument(points).toJson(QJsonDocument::Compact));
    const QString distanceText = distanceMeters >= 1000
        ? QStringLiteral("%1 公里").arg(distanceMeters / 1000.0, 0, 'f', 1)
        : QStringLiteral("%1 米").arg(distanceMeters);
    const QString summary = QStringLiteral("步行约 %1 · %2 分钟")
                                .arg(distanceText)
                                .arg(durationMinutes);

    return QStringLiteral(R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body, #map { width: 100%; height: 100%; margin: 0; }
    #summary {
      position: absolute; z-index: 2; left: 12px; top: 12px;
      padding: 9px 13px; border-radius: 8px; background: rgba(255,255,255,.94);
      color: #202124; font: 14px sans-serif; box-shadow: 0 2px 8px rgba(0,0,0,.2);
    }
  </style>
  <script src="%1"></script>
</head>
<body>
  <div id="summary">%2</div>
  <div id="map"></div>
  <script>
    const coordinates = %3;
    const path = coordinates.map(point => new TMap.LatLng(point[0], point[1]));
    const map = new TMap.Map(document.getElementById('map'), {
      center: path[0], zoom: 15
    });
    new TMap.MultiPolyline({
      map: map,
      styles: {
        route: new TMap.PolylineStyle({
          color: '#2b7de9', width: 7, borderWidth: 2,
          borderColor: '#ffffff', lineCap: 'round'
        })
      },
      geometries: [{ id: 'walking-route', styleId: 'route', paths: path }]
    });
    const bounds = new TMap.LatLngBounds();
    path.forEach(point => bounds.extend(point));
    map.fitBounds(bounds, { padding: 52 });
  </script>
</body>
</html>)HTML")
        .arg(scriptUrl.toString(QUrl::FullyEncoded).toHtmlEscaped(),
             summary.toHtmlEscaped(),
             pointsJson);
}

}  // namespace

TencentMapService::TencentMapService(QString apiKey,
                                     int requestTimeoutMs,
                                     QObject *parent)
    : IMapService(parent)
    , apiKey_(apiKey.trimmed())
    , requestTimeoutMs_(qMax(1, requestTimeoutMs))
{
}

QString TencentMapService::geocode(const QString &address)
{
    const QString requestId = nextRequestId();
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
    QNetworkReply *reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, normalizedAddress]() {
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

QString TencentMapService::openRoute(const MapLocation &start,
                                     const MapLocation &end,
                                     RouteMode mode)
{
    const QString requestId = nextRequestId();
    QTimer::singleShot(0, this, [this, requestId, start, end, mode]() {
        RouteResult result;
        result.requestId = requestId;
        const QString configurationError = apiKeyConfigurationError(apiKey_);
        if (!configurationError.isEmpty()) {
            result.message = configurationError;
        } else if (start.address.trimmed().isEmpty()
                   || end.address.trimmed().isEmpty()
                   || !validCoordinate(start) || !validCoordinate(end)) {
            result.message = QStringLiteral("路线起点或终点无效");
        } else if (mode == RouteMode::Driving) {
            QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("type"), QStringLiteral("drive"));
            query.addQueryItem(QStringLiteral("from"), start.address);
            query.addQueryItem(QStringLiteral("fromcoord"), coordinateText(start));
            query.addQueryItem(QStringLiteral("to"), end.address);
            query.addQueryItem(QStringLiteral("tocoord"), coordinateText(end));
            query.addQueryItem(QStringLiteral("referer"), apiKey_);
            url.setQuery(query);

            result.success = true;
            result.message = QStringLiteral("正在加载腾讯地图路线…");
            result.summary = QStringLiteral("驾车：%1 → %2")
                                 .arg(start.address, end.address);
            result.routeUrl = url;
        } else {
            QUrl url(QStringLiteral(
                "https://apis.map.qq.com/ws/direction/v1/walking/"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("from"), coordinateText(start));
            query.addQueryItem(QStringLiteral("to"), coordinateText(end));
            query.addQueryItem(QStringLiteral("key"), apiKey_);
            url.setQuery(query);

            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("BIT-ChargingClient/1.0"));
            request.setTransferTimeout(requestTimeoutMs_);
            QNetworkReply *reply = network_.get(request);
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, requestId, start, end]() {
                        RouteResult walkingResult;
                        walkingResult.requestId = requestId;
                        if (reply->error() != QNetworkReply::NoError) {
                            walkingResult.message = QStringLiteral(
                                "腾讯地图步行路线规划失败，请检查网络后重试");
                            reply->deleteLater();
                            emit routeCompleted(walkingResult);
                            return;
                        }

                        QJsonParseError parseError;
                        const QJsonDocument document = QJsonDocument::fromJson(
                            reply->readAll(), &parseError);
                        reply->deleteLater();
                        if (parseError.error != QJsonParseError::NoError
                            || !document.isObject()) {
                            walkingResult.message = QStringLiteral(
                                "腾讯地图返回了无法识别的步行路线数据");
                            emit routeCompleted(walkingResult);
                            return;
                        }

                        const QJsonObject root = document.object();
                        const int status =
                            root.value(QStringLiteral("status")).toInt(-1);
                        const QJsonArray routes = root.value(QStringLiteral("result"))
                                                      .toObject()
                                                      .value(QStringLiteral("routes"))
                                                      .toArray();
                        if (status != 0 || routes.isEmpty()
                            || !routes.first().isObject()) {
                            const QString serviceMessage =
                                root.value(QStringLiteral("message"))
                                    .toString()
                                    .trimmed();
                            walkingResult.message = serviceMessage.isEmpty()
                                ? QStringLiteral("腾讯地图步行路线规划失败（状态码 %1）")
                                      .arg(status)
                                : QStringLiteral(
                                      "腾讯地图步行路线规划失败（状态码 %1）：%2")
                                      .arg(status)
                                      .arg(serviceMessage);
                            emit routeCompleted(walkingResult);
                            return;
                        }

                        const QJsonObject route = routes.first().toObject();
                        const auto points = decodeWalkingPolyline(
                            route.value(QStringLiteral("polyline")).toArray());
                        if (!points.has_value()) {
                            walkingResult.message =
                                QStringLiteral("腾讯地图返回的步行路线坐标无效");
                            emit routeCompleted(walkingResult);
                            return;
                        }

                        const int distance =
                            route.value(QStringLiteral("distance")).toInt();
                        const int duration =
                            route.value(QStringLiteral("duration")).toInt();
                        walkingResult.success = true;
                        walkingResult.message =
                            QStringLiteral("腾讯地图步行路线规划成功");
                        walkingResult.summary = QStringLiteral("步行：%1 → %2")
                                                    .arg(start.address, end.address);
                        walkingResult.routeHtml = walkingRouteHtml(
                            apiKey_, *points, distance, duration);
                        emit routeCompleted(walkingResult);
                    });
            return;
        }
        emit routeCompleted(result);
    });
    return requestId;
}

QString TencentMapService::nextRequestId()
{
    return QStringLiteral("map-tencent-%1").arg(nextRequestNumber_++);
}

void TencentMapService::emitGeocodeFailure(const QString &requestId,
                                           const QString &message)
{
    QTimer::singleShot(0, this, [this, requestId, message]() {
        emit geocodeCompleted(GeocodeResult{requestId, false, message, std::nullopt});
    });
}

}  // namespace charging::client
