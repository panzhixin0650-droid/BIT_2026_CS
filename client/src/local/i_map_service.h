#pragma once

#include "local/map_types.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IMapService : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IMapService() override = default;

    [[nodiscard]] virtual QString geocode(const QString &address) = 0;
    [[nodiscard]] virtual QString openRoute(const MapLocation &start,
                                            const MapLocation &end,
                                            RouteMode mode) = 0;
    virtual void cancel(const QString &requestId) { Q_UNUSED(requestId); }

signals:
    void geocodeCompleted(const charging::client::GeocodeResult &result);
    void routeCompleted(const charging::client::RouteResult &result);
};

}  // namespace charging::client
