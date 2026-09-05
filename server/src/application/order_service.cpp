#include "application_service.h"

#include "adapters/i_pile_gateway.h"
#include "charging/protocol/protocol_constants.h"
#include "order_billing.h"
#include "persistence/i_repository.h"
#include "persistence/repository_transaction.h"

#include <QJsonArray>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace charging::server {
namespace {

using namespace charging::protocol;

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

bool readPileCode(const QJsonObject &input, QString *code)
{
    const QJsonValue value = input.value(QStringLiteral("pileCode"));
    if (!value.isString()) return false;
    *code = value.toString().trimmed();
    return !code->isEmpty() && code->size() <= 64;
}

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

bool ApplicationService::refreshOrderReading(OrderDto *order, const QDateTime &now,
                                              bool stop) const
{
    if (order->status != OrderStatus::Charging) return true;
    if (pileGateway_ == nullptr || !order->startedAt.has_value()
        || !order->unitPriceCentsPerKwh.has_value()) return false;
    const QDateTime startedAt = QDateTime::fromString(*order->startedAt, Qt::ISODate);
    if (!startedAt.isValid()) return false;
    const PileReading reading = stop
        ? pileGateway_->stop(order->pileId, startedAt, now)
        : pileGateway_->read(order->pileId, startedAt, now);
    if (reading.durationSeconds < 0 || reading.energyWh < 0) return false;
    order->durationSeconds = std::max(order->durationSeconds, reading.durationSeconds);
    order->energyWh = std::max(order->energyWh, reading.energyWh);
    const auto amount = orderAmountCents(order->energyWh, *order->unitPriceCentsPerKwh);
    if (!amount.has_value()) return false;
    order->amountCents = *amount;
    return true;
}

ServiceResult ApplicationService::getCurrentOrder(const QString &token,
                                                  const QJsonObject &input) const
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    if (hasAuthoritativeFields(input)) return orderError(ErrorCode::InvalidRequest);
    auto order = currentOrder(repository_, *userId, &failure);
    if (!failure.ok()) return failure;
    if (!order.has_value()) {
        return ServiceResult::success({{QStringLiteral("order"), QJsonValue::Null}});
    }
    if (!refreshOrderReading(&*order, QDateTime::currentDateTimeUtc())) {
        return orderError(ErrorCode::InternalError);
    }
    return ServiceResult::success({{QStringLiteral("order"), toJson(*order)}});
}

ServiceResult ApplicationService::listUserOrders(const QString &token,
                                                 const QJsonObject &input) const
{
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    if (hasAuthoritativeFields(input)) return orderError(ErrorCode::InvalidRequest);
    const auto orders = repository_->listOrders(*userId);
    if (!repository_->lastOperationSucceeded()) return orderError(ErrorCode::InternalError);
    QJsonArray items;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (OrderDto order : orders) {
        if (!refreshOrderReading(&order, now)) return orderError(ErrorCode::InternalError);
        items.append(toJson(order));
    }
    return ServiceResult::success({{QStringLiteral("items"), items}});
}

ServiceResult ApplicationService::reserveOrder(const QString &token, const QJsonObject &input)
{
    RepositoryTransaction transaction(repository_);
    if (!transaction.active()) return orderError(ErrorCode::InternalError);
    ServiceResult failure;
    const auto userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) return failure;
    QString code;
    if (hasAuthoritativeFields(input) || !readPileCode(input, &code)) {
        return orderError(ErrorCode::InvalidRequest);
    }
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
                               QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
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

ServiceResult ApplicationService::cancelOrder(const QString &token, const QJsonObject &input)
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
    if (order->status != OrderStatus::Reserved) return orderError(ErrorCode::IllegalOrderState);
    auto pile = findPile(repository_, order->pileCode);
    if (!repository_->lastOperationSucceeded() || !pile.has_value()) {
        return orderError(ErrorCode::InternalError);
    }
    if (pile->status != PileStatus::Reserved) return orderError(ErrorCode::IllegalOrderState);
    order->status = OrderStatus::Cancelled;
    pile->status = PileStatus::Idle;
    if (!repository_->updatePile(*pile)
        || !repository_->updateOrder(*order, OrderStatus::Reserved)
        || !transaction.commit()) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({{QStringLiteral("order"), toJson(*order)}});
}

ServiceResult ApplicationService::startOrder(const QString &token, const QJsonObject &input)
{
    RepositoryTransaction transaction(repository_);
    if (!transaction.active()) return orderError(ErrorCode::InternalError);
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
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!reserved) {
        order = newOrder(*userId, *pile, *station, now.toString(Qt::ISODate));
        order->mode = OrderMode::Direct;
    }
    order->status = OrderStatus::Charging;
    order->startedAt = now.toString(Qt::ISODate);
    order->unitPriceCentsPerKwh = station->priceCentsPerKwh;
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
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!refreshOrderReading(&*order, now)) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({
        {QStringLiteral("order"), toJson(*order)},
        {QStringLiteral("measuredAt"), now.toString(Qt::ISODate)},
    });
}

ServiceResult ApplicationService::stopOrder(const QString &token, const QJsonObject &input)
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
    if (order->status != OrderStatus::Charging) return orderError(ErrorCode::IllegalOrderState);
    auto pile = findPile(repository_, order->pileCode);
    if (!repository_->lastOperationSucceeded() || !pile.has_value()) {
        return orderError(ErrorCode::InternalError);
    }
    if (pile->status != PileStatus::Charging) return orderError(ErrorCode::IllegalOrderState);
    auto user = repository_->findUserById(*userId);
    if (!repository_->lastOperationSucceeded() || !user.has_value()) {
        return orderError(ErrorCode::InternalError);
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!refreshOrderReading(&*order, now, true)) return orderError(ErrorCode::InternalError);
    order->endedAt = now.toString(Qt::ISODate);
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
    order->paidAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (!repository_->updateUser(*user)
        || !repository_->updateOrder(*order, OrderStatus::PendingPayment)
        || !transaction.commit()) return orderError(ErrorCode::InternalError);
    return ServiceResult::success({
        {QStringLiteral("order"), toJson(*order)},
        {QStringLiteral("balanceCents"), static_cast<double>(user->balanceCents)},
    });
}

}  // namespace charging::server
