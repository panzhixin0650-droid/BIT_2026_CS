// 拥堵预测提供者声明：标记未来接真实预测的替换位置
#pragma once

#include "charging/protocol/dto.h"

#include <optional>

namespace charging::server {

// PredictionDto is intentionally introduced with the shared contract in a
// later change. This adapter marks the replacement seam without inventing a
// second, server-only JSON shape.
class MockPredictionProvider final {
public:
    [[nodiscard]] bool available() const noexcept;
    // 查询站点拥堵档位，站点非法返回空值
    [[nodiscard]] std::optional<charging::protocol::CongestionLevel>
    congestionForStation(qint64 stationId) const;
};

}  // namespace charging::server
