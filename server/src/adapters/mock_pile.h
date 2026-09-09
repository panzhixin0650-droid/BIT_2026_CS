// MockPile 声明：开发期用的电桩网关替身
#pragma once

#include "i_pile_gateway.h"

#include <QDateTime>
#include <QHash>
#include <QString>

namespace charging::server {

// Development adapter for the V1 MockPile boundary. It has no hardware
// protocol or device thread; ApplicationService remains the authority for
// order and pile state.
// 没有硬件协议与设备线程，权威状态仍在应用服务
class MockPile final : public IPileGateway {
public:
    [[nodiscard]] bool start(qint64 pileId,
                             const QDateTime &startedAt,
                             QString *error = nullptr) const override;
    [[nodiscard]] PileReading read(qint64 pileId,
                                   const QDateTime &startedAt,
                                   const QDateTime &now) const override;
    [[nodiscard]] PileReading stop(qint64 pileId,
                                   const QDateTime &startedAt,
                                   const QDateTime &now) const override;
    [[nodiscard]] bool restart(qint64 pileId,
                               charging::protocol::PileStatus status,
                               QString *error = nullptr) const override;

private:
    // 每桩记录起始时间与最近读数，用于识别新会话
    struct SessionReading {
        QDateTime startedAt;
        PileReading reading;
    };
    mutable QHash<qint64, SessionReading> readings_;
};

}  // namespace charging::server
