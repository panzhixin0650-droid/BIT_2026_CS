#pragma once

#include "charging/protocol/dto.h"

#include <QDateTime>
#include <QString>

namespace charging::server {

struct PileReading {
    qint64 durationSeconds = 0;
    qint64 energyWh = 0;
};

// Development adapter for the V1 MockPile boundary. It has no hardware
// protocol or device thread; ApplicationService remains the authority for
// order and pile state.
class MockPile final {
public:
    [[nodiscard]] bool start(qint64 pileId,
                             const QDateTime &startedAt,
                             QString *error = nullptr) const;
    [[nodiscard]] PileReading read(qint64 pileId,
                                   const QDateTime &startedAt,
                                   const QDateTime &now) const;
    [[nodiscard]] PileReading stop(qint64 pileId,
                                   const QDateTime &startedAt,
                                   const QDateTime &now) const;
    [[nodiscard]] bool restart(qint64 pileId,
                               charging::protocol::PileStatus status,
                               QString *error = nullptr) const;
};

}  // namespace charging::server
