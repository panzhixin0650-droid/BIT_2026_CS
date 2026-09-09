// MockPile 实现：用确定曲线模拟电桩读数，便于 Demo 演示
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

// 开始充电：校验参数后记录本次会话起始时间
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

// 按经过秒数推算读数，参数非法返回空读数
PileReading MockPile::read(qint64 pileId,
                           const QDateTime &startedAt,
                           const QDateTime &now) const
{
    if (pileId <= 0 || !startedAt.isValid() || !now.isValid()) {
        return {};
    }

    // 起始时间变化说明是新会话，重置累计读数
    SessionReading &session = readings_[pileId];
    if (session.startedAt != startedAt) session = {startedAt, {}};
    // 时长只增不减，避免时钟回拨导致读数倒退
    const qint64 duration = qMax<qint64>(session.reading.durationSeconds,
                                        qMax<qint64>(0, startedAt.secsTo(now)));
    // A deterministic 7.2 kW demo curve: two Wh per elapsed second.
    session.reading = {duration, duration * 2};
    return session.reading;
}

// 停止即取一次当前读数，不额外累加
PileReading MockPile::stop(qint64 pileId,
                           const QDateTime &startedAt,
                           const QDateTime &now) const
{
    return read(pileId, startedAt, now);
}

// 重启检查：拒绝预约、充电和故障状态的电桩
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
