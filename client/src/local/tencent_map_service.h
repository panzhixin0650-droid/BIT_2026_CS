// 腾讯地图服务声明，实现地图接口的在线版本
#pragma once

#include "local/i_map_service.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QSet>

namespace charging::client {

// 在线地图适配器，需要有效的腾讯地图 Key
class TencentMapService final : public IMapService {
    Q_OBJECT

public:
    // 构造参数：Key、请求超时，可选注入网络管理器
    explicit TencentMapService(QString apiKey,
                               int requestTimeoutMs = 5000,
                               QObject *parent = nullptr,
                               QNetworkAccessManager *networkAccess = nullptr);
    ~TencentMapService() override;

    // 实现地图脚本地址、地址解析、路线规划与取消
    [[nodiscard]] QUrl mapScriptUrl() const override;
    [[nodiscard]] QString geocode(const QString &address) override;
    [[nodiscard]] QString openRoute(const MapLocation &start,
                                    const MapLocation &end,
                                    RouteMode mode) override;
    void cancel(const QString &requestId) override;

private:
    [[nodiscard]] QString nextRequestId();
    void emitGeocodeFailure(const QString &requestId, const QString &message);

    // 已去除首尾空白的 Key 与超时毫秒数
    QString apiKey_;
    int requestTimeoutMs_ = 5000;
    quint64 nextRequestNumber_ = 1;
    QNetworkAccessManager network_;
    // Optional caller-owned transport must outlive this service (used by offline tests).
    QNetworkAccessManager *networkAccess_;
    // 记录在途请求编号与对应回复，用于取消和去重
    QSet<QString> activeRequests_;
    QHash<QString, QNetworkReply *> replies_;
};

}  // namespace charging::client
