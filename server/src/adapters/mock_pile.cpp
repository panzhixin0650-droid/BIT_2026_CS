#include "mock_pile.h"

namespace charging::server {
namespace {

void clearError(QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
}

}  // namespace

bool MockPile::start(qint64 pileId,
                     const QDateTime &startedAt,
                     QString *error) const
{
    if (pileId <= 0 || !startedAt.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid pile or start time");
        }
        return false;
    }
    readings_.insert(pileId, {startedAt, {}});
    clearError(error);
    return true;
}

PileReading MockPile::read(qint64 pileId,
                           const QDateTime &startedAt,
                           const QDateTime &now) const
{
    if (pileId <= 0 || !startedAt.isValid() || !now.isValid()) {
        return {};
    }

    SessionReading &session = readings_[pileId];
    if (session.startedAt != startedAt) session = {startedAt, {}};
    const qint64 duration = qMax<qint64>(session.reading.durationSeconds,
                                        qMax<qint64>(0, startedAt.secsTo(now)));
    // A deterministic 7.2 kW demo curve: two Wh per elapsed second.
    session.reading = {duration, duration * 2};
    return session.reading;
}

PileReading MockPile::stop(qint64 pileId,
                           const QDateTime &startedAt,
                           const QDateTime &now) const
{
    return read(pileId, startedAt, now);
}

bool MockPile::restart(qint64 pileId,
                       charging::protocol::PileStatus status,
                       QString *error) const
{
    if (pileId <= 0) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid pile");
        }
        return false;
    }
    if (status == charging::protocol::PileStatus::Reserved
        || status == charging::protocol::PileStatus::Charging
        || status == charging::protocol::PileStatus::Fault) {
        if (error != nullptr) {
            *error = QStringLiteral("pile is in use");
        }
        return false;
    }
    clearError(error);
    return true;
}

}  // namespace charging::server
