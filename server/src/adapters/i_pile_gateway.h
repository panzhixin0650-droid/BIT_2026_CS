#pragma once

#include "charging/protocol/dto.h"

#include <QDateTime>
#include <QString>

namespace charging::server {

struct PileReading {
    qint64 durationSeconds = 0;
    qint64 energyWh = 0;
};

// The Demo uses MockPile. Device protocols can later implement this boundary
// without entering the TCP router, order state machine or Repository.
// Calls are serial. Readings use seconds/Wh and must not retreat within one
// charge; a negative reading reports failure. Implementations never write SQL.
class IPileGateway {
public:
    virtual ~IPileGateway() = default;
    [[nodiscard]] virtual bool start(qint64 pileId, const QDateTime &startedAt,
                                     QString *error = nullptr) const = 0;
    [[nodiscard]] virtual PileReading read(qint64 pileId, const QDateTime &startedAt,
                                           const QDateTime &now) const = 0;
    [[nodiscard]] virtual PileReading stop(qint64 pileId, const QDateTime &startedAt,
                                           const QDateTime &now) const = 0;
    [[nodiscard]] virtual bool restart(qint64 pileId,
                                       charging::protocol::PileStatus status,
                                       QString *error = nullptr) const = 0;
};

}  // namespace charging::server
