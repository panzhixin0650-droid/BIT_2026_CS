// 拥堵预测替身实现：按站点编号给出固定档位，非真实预测
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
    // 用站点编号取模在低、中、高之间循环，结果可复现
    switch ((stationId - 1) % 3) {
    case 0: return CongestionLevel::Low;
    case 1: return CongestionLevel::Medium;
    default: return CongestionLevel::High;
    }
}

}  // namespace charging::server
