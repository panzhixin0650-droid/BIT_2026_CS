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
    [[nodiscard]] std::optional<charging::protocol::CongestionLevel>
    congestionForStation(qint64 stationId) const;
};

}  // namespace charging::server
