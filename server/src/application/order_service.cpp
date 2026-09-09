// 本文件实现订单相关业务：预约、开始、进度、停止与支付
#include "application_service.h"

#include "adapters/i_pile_gateway.h"
#include "charging/protocol/protocol_constants.h"
#include "order_billing.h"
#include "persistence/i_repository.h"
#include "persistence/repository_transaction.h"

#include <QJsonArray>
#include <QUuid>
#include <QTimer>
#include <QDebug>

#include <algorithm>
#include <cmath>

namespace charging::server {
namespace {

using namespace charging::protocol;

// 把错误码翻译成统一的失败响应文案
ServiceResult orderError(int code)
{
    switch (code) {
    case ErrorCode::InvalidRequest:
        return ServiceResult::failure(code, QStringLiteral("INVALID_REQUEST"));
    case ErrorCode::Forbidden:
        return ServiceResult::failure(code, QStringLiteral("FORBIDDEN"));
    case ErrorCode::NotFound:
        return ServiceResult::failure(code, QStringLiteral("NOT_FOUND"));
    case ErrorCode::PileNotAvailable:
        return ServiceResult::failure(code, QStringLiteral("PILE_NOT_AVAILABLE"));
    case ErrorCode::CurrentOrderExists:
        return ServiceResult::failure(code, QStringLiteral("CURRENT_ORDER_EXISTS"));
    case ErrorCode::IllegalOrderState:
        return ServiceResult::failure(code, QStringLiteral("ILLEGAL_ORDER_STATE"));
    case ErrorCode::InsufficientBalance:
        return ServiceResult::failure(code, QStringLiteral("INSUFFICIENT_BALANCE"));
    default:
        return ServiceResult::failure(ErrorCode::InternalError, QStringLiteral("INTERNAL_ERROR"));
    }
}

// 从JSON读取ID，要求是精确可表示的正整数
bool readId(const QJsonObject &input, const QString &key, qint64 *id)
{
    const QJsonValue value = input.value(key);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    // IDs must survive the shared JSON representation exactly; check before
    // converting, including the out-of-range double -> qint64 case.
    if (!std::isfinite(number) || number < 1 || number > 9007199254740991.0
        || std::trunc(number) != number) return false;
    *id = static_cast<qint64>(number);
    return true;
}

// 读取并裁剪桩编号，限制长度
bool readPileCode(const QJsonObject &input, QString *code)
{
    const QJsonValue value = input.value(QStringLiteral("pileCode"));
    if (!value.isString()) return false;
    *code = value.toString().trimmed();
    return !code->isEmpty() && code->size() <= 64;
}

// 拒绝客户端自带的权威字段，金额状态只能由服务端定
bool hasAuthoritativeFields(const QJsonObject &input)
{
    // Unrelated optional fields are ignored as elsewhere in V1, but callers
    // cannot supply ownership, metering, prices, payment or target states.
    for (const char *key : {"userId", "pileId", "stationId", "status", "pileStatus",
                            "durationSeconds", "energyWh", "unitPriceCentsPerKwh",
                            "priceCentsPerKwh", "amountCents", "balanceCents", "paid",
                            "createdAt", "reservedAt", "startedAt", "endedAt", "paidAt"}) {
        if (input.contains(QString::fromLatin1(key))) return true;
    }
    return false;
}

bool isCurrent(OrderStatus status)
{
    return status == OrderStatus::Reserved || status == OrderStatus::Charging
        || status == OrderStatus::PendingPayment;
}

// 查找该用户的进行中订单（预约、充电或待支付）
std::optional<OrderDto> currentOrder(IRepository *repository, qint64 userId,
                                     ServiceResult *failure)
{
    const auto orders = repository->listOrders(userId);
    if (!repository->lastOperationSucceeded()) {
        *failure = orderError(ErrorCode::InternalError);
        return std::nullopt;
    }
    for (const OrderDto &order : orders) {
        if (isCurrent(order.status)) return order;
    }
    return std::nullopt;
}

// 按ID取订单并校验归属，区分不存在与越权
std::optional<OrderDto> ownedOrder(IRepository *repository, qint64 orderId,
                                   qint64 userId, ServiceResult *failure)
{
    const auto order = repository->findOrderById(orderId);
    if (!repository->lastOperationSucceeded()) {
        *failure = orderError(ErrorCode::InternalError);
    } else if (!order.has_value()) {
        *failure = orderError(ErrorCode::NotFound);
    } else if (order->userId != userId) {
        *failure = orderError(ErrorCode::Forbidden);
    } else {
        return order;
    }
    return std::nullopt;
}

std::optional<PileDto> findPile(IRepository *repository, const QString &code)
{
    for (const PileDto &pile : repository->listPiles()) {
        if (pile.pileCode == code) return pile;
    }
    return std::nullopt;
}

// 生成新订单骨架，订单号用UUID拼接
OrderDto newOrder(qint64 userId, const PileDto &pile, const StationDto &station,
                   const QString &now)
{
    OrderDto order;
    order.orderNo = QStringLiteral("ORD-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    order.createdAt = now;
    order.userId = userId;
    order.stationId = station.stationId;
    order.stationName = station.name;
    order.pileId = pile.pileId;
    order.pileCode = pile.pileCode;
    return order;
}

}  // namespace

// 从桩网关刷新时长与电量，并按锁定单价重算金额
bool ApplicationService::refreshOrderReading(OrderDto *order, const QDateTime &now,
                                              bool stop) const
{
    if (order->status != OrderStatus::Charging) return true;
    if (pileGateway_ == nullptr || !order->startedAt.has_value()
        || !order->unitPriceCentsPerKwh.has_value()) return false;
    const QDateTime startedAt = QDateTime::fromString(*order->startedAt, Qt::ISODate);
    if (!startedAt.isValid()) return false;
    const QDateTime measuredAt = demoAutomaticStop_ ? std::min(now, startedAt.addSecs(DemoChargingDurationSeconds)) : now;
    const PileReading reading = stop
        ? pileGateway_->stop(order->pileId, startedAt, measuredAt)
        : pileGateway_->read(order->pileId, startedAt, measuredAt);
    if (reading.durationSeconds < 0 || reading.energyWh < 0) return false;
    order->durationSeconds = std::max(order->durationSeconds, reading.durationSeconds);
    order->energyWh = std::max(order->energyWh, reading.energyWh);
    const auto amount = orderAmountCents(order->energyWh, *order->unitPriceCentsPerKwh);
    if (!amount.has_value()) return false;
    order->amountCents = *amount;
    return true;
}

// 查询当前订单，先清理到期预约再返回最新读数
ServiceResult ApplicationService::getCurrentOrder(const QString &token,
                                                  const QJsonObject &input) const
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    if (hasAuthoritativeFields(input)) return orderError(ErrorCode::InvalidRequest);
    if (expireDueReservations(nowUtc()) < 0) return orderError(ErrorCode::InternalError);
    auto order = currentOrder(repository_, *userId, &failure);
    if (!failure.ok()) return failure;
    if (!order.has_value()) {
        return ServiceResult::success({{QStringLiteral("order"), QJsonValue::Null}});
    }
    if (!refreshOrderReading(&*order, nowUtc())) {
        return orderError(ErrorCode::InternalError);
    }
    return ServiceResult::success({{QStringLiteral("order"), toJson(*order)}});
}

// 列出该用户全部订单，逐条刷新充电中读数
ServiceResult ApplicationService::listUserOrders(const QString &token,
                                                 const QJsonObject &input) const
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    if (hasAuthoritativeFields(input)) return orderError(ErrorCode::InvalidRequest);
    if (expireDueReservations(nowUtc()) < 0) return orderError(ErrorCode::InternalError);
    const auto orders = repository_->listOrders(*userId);
    if (!repository_->lastOperationSucceeded()) return orderError(ErrorCode::InternalError);
    QJsonArray items;
    const QDateTime now = nowUtc();
    for (OrderDto order : orders) {
        if (!refreshOrderReading(&order, now)) return orderError(ErrorCode::InternalError);
        items.append(toJson(order));
    }
    return ServiceResult::success({{QStringLiteral("items"), items}});
}

// 预约下单：校验桩空闲、用户无进行中订单，预约不锁价
ServiceResult ApplicationService::reserveOrder(const QString &token, const QJsonObject &input)
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    QString code;
    if (hasAuthoritativeFields(input) || !readPileCode(input, &code)) {
        return orderError(ErrorCode::InvalidRequest);
    }
    const auto now = nowUtc();
    if (expireDueReservations(now) < 0) return orderError(ErrorCode::InternalError);
    RepositoryTransaction transaction(repository_);
    if (!transaction.active()) return orderError(ErrorCode::InternalError);
    const auto existing = currentOrder(repository_, *userId, &failure);
    if (!failure.ok()) return failure;
    if (existing.has_value()) return orderError(ErrorCode::CurrentOrderExists);
    auto pile = findPile(repository_, code);
    if (!repository_->lastOperationSucceeded()) return orderError(ErrorCode::InternalError);
    if (!pile.has_value()) return orderError(ErrorCode::NotFound);
    const auto station = repository_->findStationById(pile->stationId);
    if (!repository_->lastOperationSucceeded()) return orderError(ErrorCode::InternalError);
    if (!station.has_value()) return orderError(ErrorCode::NotFound);
    if (station->status != StationStatus::Active || pile->status != PileStatus::Idle) {
        return orderError(ErrorCode::PileNotAvailable);
    }
    OrderDto order = newOrder(*userId, *pile, *station,
                               now.toString(Qt::ISODate));
    order.mode = OrderMode::Reservation;
    order.status = OrderStatus::Reserved;
    order.reservedAt = order.createdAt;
    pile->status = PileStatus::Reserved;
    if (!repository_->updatePile(*pile)) return orderError(ErrorCode::InternalError);
    order = repository_->createOrder(order);
    if (!repository_->lastOperationSucceeded() || order.orderId <= 0
        || !transaction.commit()) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({{QStringLiteral("order"), toJson(order)}});
}

// 用户取消预约，只允许处于预约状态的订单
ServiceResult ApplicationService::cancelOrder(const QString &token, const QJsonObject &input)
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    qint64 orderId = 0;
    if (hasAuthoritativeFields(input) || !readId(input, QStringLiteral("orderId"), &orderId)) {
        return orderError(ErrorCode::InvalidRequest);
    }
    if (expireDueReservations(nowUtc()) < 0) return orderError(ErrorCode::InternalError);
    RepositoryTransaction transaction(repository_);
    if (!transaction.active()) return orderError(ErrorCode::InternalError);
    auto order = ownedOrder(repository_, orderId, *userId, &failure);
    if (!order.has_value()) return failure;
    if (order->status != OrderStatus::Reserved) return orderError(ErrorCode::IllegalOrderState);
    const int cancelled = cancelReservation(&*order);
    if (cancelled != ErrorCode::Ok) return orderError(cancelled);
    if (!transaction.commit()) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({{QStringLiteral("order"), toJson(*order)}});
}

// Caller owns the transaction; manual and timed cancellation use the same writes.
int ApplicationService::cancelReservation(OrderDto *order) const
{
    auto pile = findPile(repository_, order->pileCode);
    if (!repository_->lastOperationSucceeded() || !pile || pile->pileId != order->pileId)
        return ErrorCode::InternalError;
    if (pile->status != PileStatus::Reserved) return ErrorCode::IllegalOrderState;
    order->status = OrderStatus::Cancelled;
    pile->status = PileStatus::Idle;
    return repository_->updatePile(*pile)
        && repository_->updateOrder(*order, OrderStatus::Reserved)
        ? ErrorCode::Ok : ErrorCode::InternalError;
}

// 开启预约过期巡检，先补扫一次再每秒轮询
void ApplicationService::enableReservationExpiry()
{
    if (reservationExpiryEnabled_) return;
    reservationExpiryEnabled_ = true;
    const auto sweep = [this] {
        if (expireDueReservations(nowUtc()) < 0)
            qWarning() << "Reservation expiry failed; will retry on the next sweep";
    };
    sweep(); // Catch up persisted reservations before accepting connections.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, sweep);
    timer->start(1000);
}

// 扫描超时预约并逐单取消，返回过期数量，出错返回-1
int ApplicationService::expireDueReservations(const QDateTime &now) const
{
    if (!now.isValid()) return -1;
    const auto due = [&now](const OrderDto &order) {
        if (order.status != OrderStatus::Reserved || !order.reservedAt) return false;
        const auto reserved = QDateTime::fromString(*order.reservedAt, Qt::ISODate);
        return reserved.isValid() && reserved.addSecs(DemoReservationDurationSeconds) <= now;
    };
    const auto orders = repository_->listOrders();
    if (!repository_->lastOperationSucceeded()) return -1;
    int expired = 0;
    for (const auto &candidate : orders) {
        if (!due(candidate)) continue;
        RepositoryTransaction transaction(repository_);
        if (!transaction.active()) return -1;
        // Re-read under the write transaction: never release a subsequently reused pile.
        auto order = repository_->findOrderById(candidate.orderId);
        if (!repository_->lastOperationSucceeded() || !order) return -1;
        if (!due(*order)) continue;
        if (cancelReservation(&*order) != ErrorCode::Ok || !transaction.commit()) return -1;
        ++expired;
    }
    return expired;
}

// 开始充电：可从预约转入或直接开桩
ServiceResult ApplicationService::startOrder(const QString &token, const QJsonObject &input)
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    QString code;
    qint64 reservationId = 0;
    const bool reserved = input.contains(QStringLiteral("reservationOrderId"));
    if (hasAuthoritativeFields(input) || !readPileCode(input, &code)
        || (reserved && !readId(input, QStringLiteral("reservationOrderId"), &reservationId))) {
        return orderError(ErrorCode::InvalidRequest);
    }
    // Expiry commits separately, so a rejected start cannot roll it back.
    // One clock sample defines the boundary, startedAt and the price snapshot.
    const QDateTime now = nowUtc();
    if (expireDueReservations(now) < 0) return orderError(ErrorCode::InternalError);
    RepositoryTransaction transaction(repository_);
    if (!transaction.active()) return orderError(ErrorCode::InternalError);
    std::optional<OrderDto> order;
    if (reserved) {
        order = ownedOrder(repository_, reservationId, *userId, &failure);
        if (!order.has_value()) return failure;
        if (order->status != OrderStatus::Reserved || order->pileCode != code) {
            return orderError(ErrorCode::IllegalOrderState);
        }
    } else {
        const auto existing = currentOrder(repository_, *userId, &failure);
        if (!failure.ok()) return failure;
        if (existing.has_value()) return orderError(ErrorCode::CurrentOrderExists);
    }
    auto pile = findPile(repository_, code);
    if (!repository_->lastOperationSucceeded()) return orderError(ErrorCode::InternalError);
    if (!pile.has_value()) return orderError(ErrorCode::NotFound);
    const auto station = repository_->findStationById(pile->stationId);
    if (!repository_->lastOperationSucceeded()) return orderError(ErrorCode::InternalError);
    if (!station.has_value()) return orderError(ErrorCode::NotFound);
    if (reserved && pile->status != PileStatus::Reserved) {
        return orderError(ErrorCode::IllegalOrderState);
    }
    if (station->status != StationStatus::Active
        || (!reserved && pile->status != PileStatus::Idle)) {
        return orderError(ErrorCode::PileNotAvailable);
    }
    // 此刻按高峰或平时价锁定单价，写入订单快照
    const auto price = chargingUnitPriceCents(station->priceCentsPerKwh, now);
    if (!price.has_value()) return orderError(ErrorCode::InternalError);
    if (!reserved) {
        order = newOrder(*userId, *pile, *station, now.toString(Qt::ISODate));
        order->mode = OrderMode::Direct;
    }
    order->status = OrderStatus::Charging;
    order->startedAt = now.toString(Qt::ISODate);
    order->unitPriceCentsPerKwh = *price;
    // Use the same second-resolution origin now and on subsequent reads.
    const QDateTime startedAt = QDateTime::fromString(*order->startedAt, Qt::ISODate);
    if (pileGateway_ == nullptr || !pileGateway_->start(pile->pileId, startedAt)
        || !refreshOrderReading(&*order, now)) return orderError(ErrorCode::InternalError);
    pile->status = PileStatus::Charging;
    if (!repository_->updatePile(*pile)) return orderError(ErrorCode::InternalError);
    if (reserved) {
        if (!repository_->updateOrder(*order, OrderStatus::Reserved)) {
            return orderError(ErrorCode::InternalError);
        }
    } else {
        order = repository_->createOrder(*order);
        if (!repository_->lastOperationSucceeded() || order->orderId <= 0) {
            return orderError(ErrorCode::InternalError);
        }
    }
    if (!transaction.commit()) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({{QStringLiteral("order"), toJson(*order)}});
}

// 查询充电进度，附带本次读数的采样时间
ServiceResult ApplicationService::getOrderProgress(const QString &token,
                                                  const QJsonObject &input) const
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    qint64 orderId = 0;
    if (hasAuthoritativeFields(input) || !readId(input, QStringLiteral("orderId"), &orderId)) {
        return orderError(ErrorCode::InvalidRequest);
    }
    auto order = ownedOrder(repository_, orderId, *userId, &failure);
    if (!order.has_value()) return failure;
    if (order->status != OrderStatus::Charging) return orderError(ErrorCode::IllegalOrderState);
    const QDateTime now = nowUtc();
    if (!refreshOrderReading(&*order, now)) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({
        {QStringLiteral("order"), toJson(*order)},
        {QStringLiteral("measuredAt"), now.toString(Qt::ISODate)},
    });
}

// 开启Demo自动停止定时器，每秒检查一次
void ApplicationService::enableDemoAutomaticStop()
{
    if (demoAutomaticStop_) return;
    demoAutomaticStop_ = true;
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { completeDueDemoCharges(nowUtc()); });
    timer->start(1000);
}

// 把充电满180秒的Demo会话按截止时刻结算
int ApplicationService::completeDueDemoCharges(const QDateTime &now)
{
    if (!demoAutomaticStop_ || !now.isValid()) return 0;
    const auto orders = repository_->listOrders();
    if (!repository_->lastOperationSucceeded()) return 0;
    int completed = 0;
    for (const auto &order : orders) {
        if (order.status != OrderStatus::Charging || !order.startedAt) continue;
        const auto started = QDateTime::fromString(*order.startedAt, Qt::ISODate);
        const auto deadline = started.addSecs(DemoChargingDurationSeconds);
        if (started.isValid() && deadline <= now
            && settleChargingOrder(order.orderId, order.userId, deadline).ok()) ++completed;
    }
    return completed;
}

// 用户手动停止充电，交由统一结算流程处理
ServiceResult ApplicationService::stopOrder(const QString &token, const QJsonObject &input)
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId) return failure;
    qint64 orderId = 0;
    if (hasAuthoritativeFields(input) || !readId(input, QStringLiteral("orderId"), &orderId))
        return orderError(ErrorCode::InvalidRequest);
    return settleChargingOrder(orderId, *userId, nowUtc());
}

// 结算充电单：取末次读数、置桩空闲并尝试扣费
ServiceResult ApplicationService::settleChargingOrder(qint64 orderId, qint64 userId, const QDateTime &now)
{
    RepositoryTransaction transaction(repository_);
    if (!transaction.active()) return orderError(ErrorCode::InternalError);
    ServiceResult failure;
    auto order = ownedOrder(repository_, orderId, userId, &failure);
    if (!order.has_value()) return failure;
    if (order->status != OrderStatus::Charging) return orderError(ErrorCode::IllegalOrderState);
    auto pile = findPile(repository_, order->pileCode);
    if (!repository_->lastOperationSucceeded() || !pile.has_value()) {
        return orderError(ErrorCode::InternalError);
    }
    if (pile->status != PileStatus::Charging) return orderError(ErrorCode::IllegalOrderState);
    auto user = repository_->findUserById(userId);
    if (!repository_->lastOperationSucceeded() || !user.has_value()) {
        return orderError(ErrorCode::InternalError);
    }
    if (!refreshOrderReading(&*order, now, true)) return orderError(ErrorCode::InternalError);
    order->endedAt = now.toString(Qt::ISODate);
    // 余额够则直接完成扣款，否则转为待支付
    const bool paid = user->balanceCents >= order->amountCents;
    order->status = paid ? OrderStatus::Completed : OrderStatus::PendingPayment;
    if (paid) {
        order->paidAt = order->endedAt;
        user->balanceCents -= order->amountCents;
        if (!repository_->updateUser(*user)) return orderError(ErrorCode::InternalError);
    }
    pile->status = PileStatus::Idle;
    // SQLite derives these from orders; the explicit in-memory substitute
    // updates its stored counters when the charge stops, never again on pay.
    ++pile->chargeCount;
    pile->totalChargeSeconds += order->durationSeconds;
    if (!repository_->updatePile(*pile)
        || !repository_->updateOrder(*order, OrderStatus::Charging)
        || !transaction.commit()) return orderError(ErrorCode::InternalError);
    QJsonObject data{
        {QStringLiteral("order"), toJson(*order)},
        {QStringLiteral("paid"), paid},
        {QStringLiteral("balanceCents"), static_cast<double>(user->balanceCents)},
    };
    if (!paid) data.insert(QStringLiteral("shortfallCents"),
                            static_cast<double>(order->amountCents - user->balanceCents));
    return ServiceResult::success(data);
}

// 补付待支付订单，余额不足直接返回错误
ServiceResult ApplicationService::payOrder(const QString &token, const QJsonObject &input)
{
    RepositoryTransaction transaction(repository_);
    if (!transaction.active()) return orderError(ErrorCode::InternalError);
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    qint64 orderId = 0;
    if (hasAuthoritativeFields(input) || !readId(input, QStringLiteral("orderId"), &orderId)) {
        return orderError(ErrorCode::InvalidRequest);
    }
    auto order = ownedOrder(repository_, orderId, *userId, &failure);
    if (!order.has_value()) return failure;
    if (order->status != OrderStatus::PendingPayment) return orderError(ErrorCode::IllegalOrderState);
    auto user = repository_->findUserById(*userId);
    if (!repository_->lastOperationSucceeded() || !user.has_value()) {
        return orderError(ErrorCode::InternalError);
    }
    if (user->balanceCents < order->amountCents) return orderError(ErrorCode::InsufficientBalance);
    user->balanceCents -= order->amountCents;
    order->status = OrderStatus::Completed;
    order->paidAt = nowUtc().toString(Qt::ISODate);
    if (!repository_->updateUser(*user)
        || !repository_->updateOrder(*order, OrderStatus::PendingPayment)
        || !transaction.commit()) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({
        {QStringLiteral("order"), toJson(*order)},
        {QStringLiteral("balanceCents"), static_cast<double>(user->balanceCents)},
    });
}

}  // namespace charging::server
