#pragma once

#include "local/i_map_service.h"

namespace charging::client {

class MockMapService final : public IMapService {
    Q_OBJECT

public:
    using IMapService::IMapService;

    [[nodiscard]] QString geocode(const QString &address) override;
    [[nodiscard]] QString openRoute(const MapLocation &start,
                                    const MapLocation &end,
                                    RouteMode mode) override;

private:
    [[nodiscard]] QString nextRequestId();

    quint64 nextRequestNumber_ = 1;
};

}  // namespace charging::client
