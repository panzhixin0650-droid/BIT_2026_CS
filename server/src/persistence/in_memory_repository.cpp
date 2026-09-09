// 本文件实现内存仓储替身，仅供开发与测试使用
#include "in_memory_repository.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QSet>

#include <algorithm>
#include <limits>

namespace charging::server {

using namespace charging::protocol;

// 构造时灌入演示数据：管理员、用户、站点与桩
InMemoryRepository::InMemoryRepository()
{
    AdminRecord admin;
    admin.adminId = 1;
    admin.username = QStringLiteral("admin");
    admin.passwordHash = QString::fromLatin1(QCryptographicHash::hash(
        QByteArrayLiteral("123456"), QCryptographicHash::Sha256).toHex());
    admin.passwordAlgorithm = QStringLiteral("SHA256_LEGACY");
    admin.displayName = QStringLiteral("系统管理员");
    admin.createdAt = QStringLiteral("2026-09-01T00:00:00Z");
    admin.updatedAt = admin.createdAt;
    admins_ = {admin};

    // 三个演示用户分别覆盖正常、冻结与余额不足场景
    users_ = {
        UserDto{1, QStringLiteral("13800000001"), QStringLiteral("演示用户0001"),
                20000, UserStatus::Active, QStringLiteral("2026-06-04T11:53:41Z")},
        UserDto{4, QStringLiteral("13800000004"), QStringLiteral("冻结用户"),
                12000, UserStatus::Frozen, QStringLiteral("2026-07-05T08:00:00Z")},
        UserDto{5, QStringLiteral("13800000005"), QStringLiteral("待支付用户"),
                100, UserStatus::Active, QStringLiteral("2026-07-15T08:00:00Z")},
    };

    stations_ = {
        StationDto{1, QStringLiteral("浑南演示充电站"), QStringLiteral("浑南区"),
                   QStringLiteral("浑南区创新路1号"), 123.43, 41.71, 135,
                   StationStatus::Active},
        StationDto{2, QStringLiteral("和平智慧充电站"), QStringLiteral("和平区"),
                   QStringLiteral("和平区青年大街88号"), 123.42, 41.79, 128,
                   StationStatus::Active},
        StationDto{3, QStringLiteral("沈北大学城充电站"), QStringLiteral("沈北新区"),
                   QStringLiteral("沈北新区蒲昌路10号"), 123.41, 41.92, 120,
                   StationStatus::Active},
    };

    // 六个演示桩覆盖空闲、充电、故障与离线状态
    piles_ = {
        PileDto{1, 1, QStringLiteral("PILE-A-01"), PileType::Fast, 10.0,
                PileStatus::Idle, 4, 14400},
        PileDto{2, 1, QStringLiteral("PILE-A-02"), PileType::Slow, 7.0,
                PileStatus::Charging, 2, 7200},
        PileDto{3, 2, QStringLiteral("PILE-B-01"), PileType::Fast, 60.0,
                PileStatus::Idle, 8, 28600},
        PileDto{4, 2, QStringLiteral("PILE-B-02"), PileType::Slow, 7.0,
                PileStatus::Fault, 3, 9600},
        PileDto{5, 3, QStringLiteral("PILE-C-01"), PileType::Fast, 60.0,
                PileStatus::Idle, 5, 18000},
        PileDto{6, 3, QStringLiteral("PILE-C-02"), PileType::Fast, 60.0,
                PileStatus::Offline, 1, 3600},
    };
    nextUserId_ = 6;
    nextStationId_ = 4;
    nextPileId_ = 7;

    // 辅助函数按天数偏移生成一条演示订单
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto makeOrder = [&now](qint64 id,
                                  qint64 userId,
                                  qint64 stationId,
                                  const QString &stationName,
                                  qint64 pileId,
                                  const QString &pileCode,
                                  OrderStatus status,
                                  int daysAgo,
                                  qint64 energyWh,
                                  qint64 amountCents) {
        OrderDto order;
        order.orderId = id;
        order.orderNo = QStringLiteral("ORD-DEMO-%1").arg(id, 4, 10, QLatin1Char('0'));
        order.createdAt = now.addDays(-daysAgo).addSecs(-3600).toString(Qt::ISODate);
        order.userId = userId;
        order.stationId = stationId;
        order.stationName = stationName;
        order.pileId = pileId;
        order.pileCode = pileCode;
        order.mode = OrderMode::Direct;
        order.status = status;
        order.startedAt = now.addDays(-daysAgo).addSecs(-3600).toString(Qt::ISODate);
        order.durationSeconds = energyWh / 2;
        order.energyWh = energyWh;
        order.unitPriceCentsPerKwh = stationId == 1 ? 135 : 128;
        order.amountCents = amountCents;
        if (status == OrderStatus::Completed || status == OrderStatus::PendingPayment) {
            order.endedAt = now.addDays(-daysAgo).toString(Qt::ISODate);
        }
        if (status == OrderStatus::Completed) {
            order.paidAt = now.addDays(-daysAgo).toString(Qt::ISODate);
        }
        return order;
    };

    // 预置历史与进行中订单，供看板和列表演示
    orders_ = {
        makeOrder(1001, 1, 1, QStringLiteral("浑南演示充电站"), 1,
                  QStringLiteral("PILE-A-01"), OrderStatus::Completed, 0, 5000, 675),
        makeOrder(1002, 1, 2, QStringLiteral("和平智慧充电站"), 3,
                  QStringLiteral("PILE-B-01"), OrderStatus::Completed, 1, 10000, 1280),
        makeOrder(1003, 1, 1, QStringLiteral("浑南演示充电站"), 1,
                  QStringLiteral("PILE-A-01"), OrderStatus::Completed, 3, 8000, 1080),
        makeOrder(1004, 1, 2, QStringLiteral("和平智慧充电站"), 3,
                  QStringLiteral("PILE-B-01"), OrderStatus::Completed, 6, 6500, 832),
        makeOrder(1005, 1, 1, QStringLiteral("浑南演示充电站"), 2,
                  QStringLiteral("PILE-A-02"), OrderStatus::Charging, 0, 2400, 324),
        makeOrder(1006, 5, 3, QStringLiteral("沈北大学城充电站"), 5,
                  QStringLiteral("PILE-C-01"), OrderStatus::PendingPayment, 2, 4000, 480),
    };
}

// 此存储状态检查固定返回成功，业务操作仍会校验参数和状态
bool InMemoryRepository::lastOperationSucceeded() const noexcept
{
    return true;
}

// 事务用整表快照模拟，未提交时可还原数据，不会写入磁盘
bool InMemoryRepository::beginTransaction()
{
    if (transaction_.has_value()) return false;
    transaction_ = Snapshot{users_, admins_, stations_, piles_, orders_, nextUserId_,
                            nextAdminId_, nextStationId_, nextPileId_, nextOrderId_,
                            tickets_, nextTicketId_};
    return true;
}

bool InMemoryRepository::commitTransaction()
{
    if (!transaction_.has_value()) return false;
    transaction_.reset();
    return true;
}

// 回滚时用快照覆盖各表与自增ID
void InMemoryRepository::rollbackTransaction()
{
    if (!transaction_.has_value()) return;
    users_ = transaction_->users;
    admins_ = transaction_->admins;
    stations_ = transaction_->stations;
    piles_ = transaction_->piles;
    orders_ = transaction_->orders;
    nextUserId_ = transaction_->nextUserId;
    nextAdminId_ = transaction_->nextAdminId;
    nextStationId_ = transaction_->nextStationId;
    nextPileId_ = transaction_->nextPileId;
    nextOrderId_ = transaction_->nextOrderId;
    tickets_ = transaction_->tickets;
    nextTicketId_ = transaction_->nextTicketId;
    transaction_.reset();
}

// 按用户名不区分大小写查找管理员
std::optional<AdminRecord> InMemoryRepository::findAdminByUsername(
    const QString &username) const
{
    const auto found = std::find_if(admins_.cbegin(), admins_.cend(),
                                    [&username](const AdminRecord &admin) {
                                        return admin.username.compare(
                                            username, Qt::CaseInsensitive) == 0;
                                    });
    return found == admins_.cend() ? std::nullopt
                                   : std::optional<AdminRecord>(*found);
}

std::optional<AdminRecord> InMemoryRepository::findAdminById(qint64 adminId) const
{
    const auto found = std::find_if(admins_.cbegin(), admins_.cend(),
                                    [adminId](const AdminRecord &admin) {
                                        return admin.adminId == adminId;
                                    });
    return found == admins_.cend() ? std::nullopt
                                   : std::optional<AdminRecord>(*found);
}

QList<AdminRecord> InMemoryRepository::listAdmins() const
{
    QList<AdminRecord> result = admins_;
    std::sort(result.begin(), result.end(), [](const AdminRecord &left,
                                               const AdminRecord &right) {
        return left.adminId < right.adminId;
    });
    return result;
}

// 新增管理员前检查用户名不重复
AdminRecord InMemoryRepository::createAdmin(AdminRecord admin)
{
    if (admin.username.isEmpty()
        || std::any_of(admins_.cbegin(), admins_.cend(), [&admin](const AdminRecord &stored) {
               return stored.username.compare(admin.username, Qt::CaseInsensitive) == 0;
           })) {
        return {};
    }
    admin.adminId = nextAdminId_++;
    admins_.append(admin);
    return admin;
}

bool InMemoryRepository::updateAdmin(const AdminRecord &admin)
{
    const auto found = std::find_if(admins_.begin(), admins_.end(),
                                    [&admin](const AdminRecord &stored) {
                                        return stored.adminId == admin.adminId;
                                    });
    if (found == admins_.end()) return false;
    *found = admin;
    found->version = admin.version + 1;
    return true;
}

// 直接覆盖管理员的站点授权列表，忽略授权人与时间
bool InMemoryRepository::replaceAdminStationScopes(
    qint64 adminId,
    const QList<qint64> &stationIds,
    qint64 grantedByAdminId,
    const QString &grantedAt)
{
    Q_UNUSED(grantedByAdminId)
    Q_UNUSED(grantedAt)
    const auto found = std::find_if(admins_.begin(), admins_.end(),
                                    [adminId](const AdminRecord &admin) {
                                        return admin.adminId == adminId;
                                    });
    if (found == admins_.end()) return false;
    found->stationIds = stationIds;
    return true;
}

bool InMemoryRepository::appendAdminAudit(qint64 actorAdminId,
                                          const QString &action,
                                          qint64 targetAdminId,
                                          const QString &detailsJson,
                                          const QString &createdAt)
{
    return actorAdminId > 0 && targetAdminId > 0 && !action.isEmpty()
        && !detailsJson.isEmpty() && !createdAt.isEmpty();
}

// 按手机号查用户
std::optional<UserDto> InMemoryRepository::findUserByPhone(const QString &phone) const
{
    const auto found = std::find_if(users_.cbegin(), users_.cend(),
                                    [&phone](const UserDto &user) {
                                        return user.phone == phone;
                                    });
    return found == users_.cend() ? std::nullopt
                                  : std::optional<UserDto>(*found);
}

std::optional<UserDto> InMemoryRepository::findUserById(qint64 userId) const
{
    const auto found = std::find_if(users_.cbegin(), users_.cend(),
                                    [userId](const UserDto &user) {
                                        return user.userId == userId;
                                    });
    return found == users_.cend() ? std::nullopt
                                  : std::optional<UserDto>(*found);
}

UserDto InMemoryRepository::createUser(const QString &phone,
                                       const QString &nickname,
                                       const QString &createdAt)
{
    UserDto user;
    user.userId = nextUserId_++;
    user.phone = phone;
    user.nickname = nickname;
    user.status = UserStatus::Active;
    user.createdAt = createdAt;
    users_.append(user);
    return user;
}

bool InMemoryRepository::updateUser(const UserDto &user)
{
    const auto found = std::find_if(users_.begin(), users_.end(),
                                    [&user](const UserDto &stored) {
                                        return stored.userId == user.userId;
                                    });
    if (found == users_.end()) {
        return false;
    }
    *found = user;
    return true;
}

// 充值时校验金额为正并防止余额溢出
std::optional<UserDto> InMemoryRepository::addUserBalance(qint64 userId,
                                                          qint64 amountCents)
{
    const auto found = std::find_if(users_.begin(), users_.end(),
                                    [userId](const UserDto &user) {
                                        return user.userId == userId;
                                    });
    if (found == users_.end() || amountCents <= 0
        || found->balanceCents
            > std::numeric_limits<qint64>::max() - amountCents) {
        return std::nullopt;
    }
    found->balanceCents += amountCents;
    return *found;
}

QList<UserDto> InMemoryRepository::listUsers() const
{
    QList<UserDto> result = users_;
    std::sort(result.begin(), result.end(), [](const UserDto &left, const UserDto &right) {
        return left.userId < right.userId;
    });
    return result;
}

// 只列出启用站点，并补齐桩数量统计
QList<StationDto> InMemoryRepository::listActiveStations() const
{
    QList<StationDto> result;
    for (const StationDto &station : stations_) {
        if (station.status == StationStatus::Active) {
            result.append(withPileCounts(station));
        }
    }
    return result;
}

QList<StationDto> InMemoryRepository::listStations() const
{
    QList<StationDto> result;
    for (const StationDto &station : stations_) {
        result.append(withPileCounts(station));
    }
    std::sort(result.begin(), result.end(), [](const StationDto &left,
                                               const StationDto &right) {
        return left.stationId < right.stationId;
    });
    return result;
}

std::optional<StationDto> InMemoryRepository::findStationById(qint64 stationId) const
{
    const auto found = std::find_if(stations_.cbegin(), stations_.cend(),
                                    [stationId](const StationDto &station) {
                                        return station.stationId == stationId;
                                    });
    return found == stations_.cend()
        ? std::nullopt
        : std::optional<StationDto>(withPileCounts(*found));
}

// 建站同时批量建桩，校验编号唯一与额定功率范围
StationDto InMemoryRepository::createStation(StationDto station,
                                             const QList<PileDto> &piles)
{
    if (piles.size() > 100) {
        return {};
    }
    QSet<QString> pileCodes;
    for (const PileDto &pile : piles) {
        if (pile.pileType != PileType::Fast && pile.pileType != PileType::Slow) {
            return {};
        }
        const QString normalizedCode = pile.pileCode.trimmed().toCaseFolded();
        if (normalizedCode.isEmpty() || pile.pileCode.size() > 64
            || pile.ratedPowerKw <= 0.0 || pile.ratedPowerKw > 1000.0
            || pileCodes.contains(normalizedCode)
            || std::any_of(piles_.cbegin(), piles_.cend(), [&normalizedCode](const PileDto &stored) {
                   return stored.pileCode.toCaseFolded() == normalizedCode;
               })) {
            return {};
        }
        pileCodes.insert(normalizedCode);
    }

    station.stationId = nextStationId_++;
    station.status = StationStatus::Active;
    station.distanceKm.reset();
    station.predictedCongestion.reset();
    station.recommended = false;
    stations_.append(station);

    for (PileDto pile : piles) {
        pile.pileId = nextPileId_++;
        pile.stationId = station.stationId;
        pile.pileCode = pile.pileCode.trimmed();
        pile.status = PileStatus::Idle;
        pile.chargeCount = 0;
        pile.totalChargeSeconds = 0;
        piles_.append(pile);
    }
    return withPileCounts(station);
}

// 更新站点基础信息，校验名称地址长度与单价
bool InMemoryRepository::updateStation(const StationDto &station)
{
    const auto found = std::find_if(stations_.begin(), stations_.end(),
                                    [&station](const StationDto &stored) {
                                        return stored.stationId == station.stationId;
                                    });
    if (found == stations_.end() || station.name.trimmed().isEmpty()
        || station.name.size() > 64 || station.region.trimmed().isEmpty()
        || station.region.size() > 64 || station.address.trimmed().isEmpty()
        || station.address.size() > 200 || station.priceCentsPerKwh <= 0) {
        return false;
    }
    const qint64 id = found->stationId;
    *found = station;
    found->stationId = id;
    found->name = found->name.trimmed();
    found->region = found->region.trimmed();
    found->address = found->address.trimmed();
    found->distanceKm.reset();
    found->predictedCongestion.reset();
    found->recommended = false;
    return true;
}

// 有订单或被工单引用的站点不允许删除
DeleteStationResult InMemoryRepository::deleteStation(qint64 stationId)
{
    const auto station = std::find_if(
        stations_.begin(), stations_.end(), [stationId](const StationDto &item) {
            return item.stationId == stationId;
        });
    if (station == stations_.end()) {
        return DeleteStationResult::NotFound;
    }

    const bool hasOrders = std::any_of(
        orders_.cbegin(), orders_.cend(), [stationId](const OrderDto &order) {
            return order.stationId == stationId;
        });
    if (hasOrders) {
        return DeleteStationResult::HasOrders;
    }
    for (const auto &pile : piles_) {
        if (pile.stationId != stationId) continue;
        for (const auto &ticket : tickets_)
            if (ticket.pileCode == pile.pileCode) return DeleteStationResult::StorageError;
    }

    piles_.erase(std::remove_if(piles_.begin(), piles_.end(),
                                [stationId](const PileDto &pile) {
                                    return pile.stationId == stationId;
                                }),
                 piles_.end());
    stations_.erase(station);
    return DeleteStationResult::Deleted;
}

QList<PileDto> InMemoryRepository::listPilesByStationId(qint64 stationId) const
{
    QList<PileDto> result;
    for (const PileDto &pile : piles_) {
        if (pile.stationId == stationId) {
            result.append(pile);
        }
    }
    std::sort(result.begin(), result.end(), [](const PileDto &left, const PileDto &right) {
        return left.pileId < right.pileId;
    });
    return result;
}

QList<PileDto> InMemoryRepository::listPiles() const
{
    QList<PileDto> result = piles_;
    std::sort(result.begin(), result.end(), [](const PileDto &left,
                                               const PileDto &right) {
        return left.pileId < right.pileId;
    });
    return result;
}

// 新增桩要求站点存在且启用、编号未占用
PileDto InMemoryRepository::createPile(PileDto pile)
{
    if (pile.stationId <= 0
        || std::none_of(stations_.cbegin(), stations_.cend(), [&pile](const StationDto &station) {
               return station.stationId == pile.stationId
                   && station.status == StationStatus::Active;
           })
        || pile.pileCode.isEmpty()
        || std::any_of(piles_.cbegin(), piles_.cend(), [&pile](const PileDto &stored) {
               return stored.pileCode == pile.pileCode;
           })
        || pile.ratedPowerKw <= 0.0) {
        return {};
    }
    pile.pileId = nextPileId_++;
    pile.status = PileStatus::Idle;
    pile.chargeCount = 0;
    pile.totalChargeSeconds = 0;
    piles_.append(pile);
    return pile;
}

// 仅空闲或离线且无订单引用的桩可删除
DeletePileResult InMemoryRepository::deletePile(qint64 pileId)
{
    const auto found = std::find_if(piles_.begin(), piles_.end(), [pileId](const PileDto &pile) {
        return pile.pileId == pileId;
    });
    if (found == piles_.end()) return DeletePileResult::NotFound;
    if (found->status != PileStatus::Idle && found->status != PileStatus::Offline) {
        return DeletePileResult::Busy;
    }
    if (std::any_of(orders_.cbegin(), orders_.cend(), [pileId](const OrderDto &order) {
            return order.pileId == pileId;
        })) {
        return DeletePileResult::HasOrders;
    }
    for (const auto &ticket : tickets_)
        if (ticket.pileCode == found->pileCode) return DeletePileResult::StorageError;
    piles_.erase(found);
    return DeletePileResult::Deleted;
}

bool InMemoryRepository::updatePile(const PileDto &pile)
{
    const auto found = std::find_if(piles_.begin(), piles_.end(),
                                    [&pile](const PileDto &stored) {
                                        return stored.pileId == pile.pileId;
                                    });
    if (found == piles_.end()) {
        return false;
    }
    if (found->pileCode != pile.pileCode) {
        for (const auto &ticket : tickets_)
            if (ticket.pileCode == found->pileCode) return false;
    }
    *found = pile;
    return true;
}

// 列订单时回填站点名与桩编号，并按时间倒序
QList<OrderDto> InMemoryRepository::listOrders(std::optional<qint64> userId) const
{
    QList<OrderDto> result;
    for (OrderDto order : orders_) {
        if (userId.has_value() && order.userId != *userId) continue;
        const auto station = findStationById(order.stationId);
        if (station.has_value()) order.stationName = station->name;
        for (const PileDto &pile : piles_) {
            if (pile.pileId == order.pileId) order.pileCode = pile.pileCode;
        }
        result.append(order);
    }
    std::sort(result.begin(), result.end(), [](const OrderDto &left,
                                               const OrderDto &right) {
        if (left.createdAt == right.createdAt) return left.orderId > right.orderId;
        return left.createdAt > right.createdAt;
    });
    return result;
}

std::optional<OrderDto> InMemoryRepository::findOrderById(qint64 orderId) const
{
    for (const OrderDto &order : listOrders()) {
        if (order.orderId == orderId) return order;
    }
    return std::nullopt;
}

// 建单需在事务内，且拦截重复单号与同用户同桩冲突
OrderDto InMemoryRepository::createOrder(OrderDto order)
{
    if (!transaction_.has_value()) return {};
    const auto isCurrent = [](OrderStatus status) {
        return status == OrderStatus::Reserved || status == OrderStatus::Charging
            || status == OrderStatus::PendingPayment;
    };
    for (const OrderDto &stored : orders_) {
        if (stored.orderNo == order.orderNo
            || (stored.userId == order.userId && isCurrent(stored.status))
            || (stored.pileId == order.pileId
                && (stored.status == OrderStatus::Reserved
                    || stored.status == OrderStatus::Charging))) return {};
    }
    order.orderId = nextOrderId_++;
    orders_.append(order);
    return order;
}

// 更新订单前比对期望状态，防止状态被覆盖
bool InMemoryRepository::updateOrder(const OrderDto &order, OrderStatus expectedStatus)
{
    if (!transaction_.has_value()) return false;
    for (OrderDto &stored : orders_) {
        if (stored.orderId != order.orderId) continue;
        if (stored.status != expectedStatus) return false;
        stored.status = order.status;
        stored.startedAt = order.startedAt;
        stored.endedAt = order.endedAt;
        stored.paidAt = order.paidAt;
        stored.durationSeconds = order.durationSeconds;
        stored.energyWh = order.energyWh;
        stored.unitPriceCentsPerKwh = order.unitPriceCentsPerKwh;
        stored.amountCents = order.amountCents;
        return true;
    }
    return false;
}

// 统计站点总桩数、可用桩数与在线率
StationDto InMemoryRepository::withPileCounts(StationDto station) const
{
    qint64 total = 0;
    qint64 available = 0;
    qint64 online = 0;
    for (const PileDto &pile : piles_) {
        if (pile.stationId != station.stationId) {
            continue;
        }
        ++total;
        if (pile.status == PileStatus::Idle && station.status == StationStatus::Active) {
            ++available;
        }
        if (pile.status != PileStatus::Offline) {
            ++online;
        }
    }
    station.totalPileCount = total;
    station.availablePileCount = available;
    station.onlineRatePercent = total == 0
        ? 0.0
        : static_cast<double>(online) * 100.0 / static_cast<double>(total);
    return station;
}

bool InMemoryRepository::supportsSupportTickets() const { return true; }

std::optional<SupportTicketDto> InMemoryRepository::findSupportTicket(qint64 ticketId) const
{
    for (const auto &ticket : tickets_) if (ticket.ticketId == ticketId) return ticket;
    return std::nullopt;
}

std::optional<SupportTicketDto> InMemoryRepository::findSupportSubmission(
    qint64 userId, const QString &submissionId) const
{
    for (const auto &ticket : tickets_)
        if (ticket.userId == userId && ticket.submissionId == submissionId) return ticket;
    return std::nullopt;
}

// 按用户与游标倒序取工单，限制单页数量
QList<SupportTicketDto> InMemoryRepository::listSupportTickets(
    std::optional<qint64> userId, std::optional<qint64> beforeId, int limit) const
{
    QList<SupportTicketDto> result;
    for (auto it = tickets_.crbegin(); it != tickets_.crend(); ++it) {
        if ((!userId || it->userId == *userId) && (!beforeId || it->ticketId < *beforeId))
            result.append(*it);
        if (result.size() >= qBound(1, limit, 101)) break;
    }
    return result;
}

// 同一用户的提交编号不能重复，新工单设为待处理且无回复
SupportTicketDto InMemoryRepository::createSupportTicket(SupportTicketDto ticket)
{
    if (findSupportSubmission(ticket.userId, ticket.submissionId)) return {};
    ticket.ticketId = nextTicketId_++;
    ticket.status = TicketStatus::Open;
    ticket.reply.clear();
    tickets_.append(ticket);
    return ticket;
}

bool InMemoryRepository::updateSupportTicket(const SupportTicketDto &ticket)
{
    for (auto &stored : tickets_) {
        if (stored.ticketId != ticket.ticketId) continue;
        stored.status = ticket.status;
        stored.reply = ticket.reply;
        stored.updatedAt = ticket.updatedAt;
        return true;
    }
    return false;
}

}  // namespace charging::server
