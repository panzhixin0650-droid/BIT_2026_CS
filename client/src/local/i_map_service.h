// 本文件定义地图服务抽象接口，供真实或模拟实现继承
#pragma once

#include "local/map_types.h"

#include <QObject>
#include <QString>

namespace charging::client {

// IMapService 约定地理编码与路线规划的异步调用方式
class IMapService : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IMapService() override = default;

    // 提供脚本地址、地理编码、打开路线和取消请求
    [[nodiscard]] virtual QUrl mapScriptUrl() const { return {}; }
    [[nodiscard]] virtual QString geocode(const QString &address) = 0;
    [[nodiscard]] virtual QString openRoute(const MapLocation &start,
                                            const MapLocation &end,
                                            RouteMode mode) = 0;
    virtual void cancel(const QString &requestId) { Q_UNUSED(requestId); }

// 结果通过信号异步返回，内含对应请求标识
signals:
    void geocodeCompleted(const charging::client::GeocodeResult &result);
    void routeCompleted(const charging::client::RouteResult &result);
};

}  // namespace charging::client
