#pragma once

#include "local/i_map_service.h"

#include <QNetworkAccessManager>

namespace charging::client {

class TencentMapService final : public IMapService {
    Q_OBJECT

public:
    explicit TencentMapService(QString apiKey,
                               int requestTimeoutMs = 5000,
                               QObject *parent = nullptr,
                               QNetworkAccessManager *networkAccess = nullptr);

    [[nodiscard]] QString geocode(const QString &address) override;
    [[nodiscard]] QString openRoute(const MapLocation &start,
                                    const MapLocation &end,
                                    RouteMode mode) override;

private:
    [[nodiscard]] QString nextRequestId();
    void emitGeocodeFailure(const QString &requestId, const QString &message);

    QString apiKey_;
    int requestTimeoutMs_ = 5000;
    quint64 nextRequestNumber_ = 1;
    QNetworkAccessManager network_;
    // Optional caller-owned transport must outlive this service (used by offline tests).
    QNetworkAccessManager *networkAccess_;
};

}  // namespace charging::client
