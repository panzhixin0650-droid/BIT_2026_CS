// Mock 地图服务声明，实现地图接口用于离线演示
#pragma once

#include "local/i_map_service.h"

namespace charging::client {

class MockMapService final : public IMapService {
    Q_OBJECT

public:
    using IMapService::IMapService;

    // 实现接口的地址解析与路线规划，返回请求编号
    [[nodiscard]] QString geocode(const QString &address) override;
    [[nodiscard]] QString openRoute(const MapLocation &start,
                                    const MapLocation &end,
                                    RouteMode mode) override;

private:
    [[nodiscard]] QString nextRequestId();

    // 请求编号计数器，从 1 开始递增
    quint64 nextRequestNumber_ = 1;
};

}  // namespace charging::client
