// 电桩网关接口：隔离真实设备协议与业务逻辑
#pragma once

#include "charging/protocol/dto.h"

#include <QDateTime>
#include <QString>

namespace charging::server {

// 一次读数结果：累计时长（秒）与电量（Wh）
struct PileReading {
    qint64 durationSeconds = 0;
    qint64 energyWh = 0;
};

// The Demo uses MockPile. Device protocols can later implement this boundary
// without entering the TCP router, order state machine or Repository.
// Calls are serial. Readings use seconds/Wh and must not retreat within one
// charge; a negative reading reports failure. Implementations never write SQL.
// 抽象电桩边界，实现方不碰订单状态与 SQL
class IPileGateway {
public:
    virtual ~IPileGateway() = default;
    // 通知电桩开始本次充电会话
    [[nodiscard]] virtual bool start(qint64 pileId, const QDateTime &startedAt,
                                     QString *error = nullptr) const = 0;
    // 读取当前累计时长与电量，用于中途查询
    [[nodiscard]] virtual PileReading read(qint64 pileId, const QDateTime &startedAt,
                                           const QDateTime &now) const = 0;
    // 结束会话并返回最终读数
    [[nodiscard]] virtual PileReading stop(qint64 pileId, const QDateTime &startedAt,
                                           const QDateTime &now) const = 0;
    // 按当前桩状态判断能否重启，占用中应拒绝
    [[nodiscard]] virtual bool restart(qint64 pileId,
                                       charging::protocol::PileStatus status,
                                       QString *error = nullptr) const = 0;
};

}  // namespace charging::server
