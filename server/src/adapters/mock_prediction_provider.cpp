#include "mock_prediction_provider.h"

namespace charging::server {

bool MockPredictionProvider::available() const noexcept
{
    return true;
}

std::optional<charging::protocol::CongestionLevel>
MockPredictionProvider::congestionForStation(qint64 stationId) const
{
    using charging::protocol::CongestionLevel;
    if (stationId <= 0) {
        return std::nullopt;
    }
    switch ((stationId - 1) % 3) {
    case 0: return CongestionLevel::Low;
    case 1: return CongestionLevel::Medium;
    default: return CongestionLevel::High;
    }
}

}  // namespace charging::server
