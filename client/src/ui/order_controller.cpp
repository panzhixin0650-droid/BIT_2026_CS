#include "ui/order_controller.h"

#include "api/i_charging_api.h"
#include "charging/protocol/protocol_constants.h"
#include "ui/order_page.h"

namespace charging::client {

OrderController::OrderController(OrderPage &page, IChargingApi &api, QObject *parent)
    : QObject(parent), page_(page), api_(api)
{
    connect(&page_, &OrderPage::refreshRequested, this, &OrderController::refreshOrders);
    connect(&page_, &OrderPage::cancellationRequested,
            this, &OrderController::requestCancellation);
    connect(&page_, &OrderPage::stopRequested,
            this, &OrderController::requestStop);
    connect(&page_, &OrderPage::progressRequested,
            this, &OrderController::requestProgress);
    connect(&page_, &OrderPage::paymentRequested,
            this, &OrderController::requestPayment);
    connect(&page_, &OrderPage::rechargeRequested,
            this, &OrderController::rechargeRequested);
    connect(&page_, &OrderPage::navigationRequested,
            this, &OrderController::requestNavigation);
    connect(&api_, &IChargingApi::orderListCompleted,
            this, &OrderController::handleOrderList);
    connect(&api_, &IChargingApi::cancellationCompleted,
            this, &OrderController::handleCancellation);
    connect(&api_, &IChargingApi::chargingStopCompleted,
            this, &OrderController::handleStop);
    connect(&api_, &IChargingApi::chargingProgressCompleted,
            this, &OrderController::handleProgress);
    connect(&api_, &IChargingApi::paymentCompleted,
            this, &OrderController::handlePayment);
    connect(&api_, &IChargingApi::stationDetailCompleted,
            this, &OrderController::handleStationDetail);
}

void OrderController::refreshOrders()
{
    if (!pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()
        || !pendingNavigationRequestId_.isEmpty()) {
        return;
    }
    page_.setLoading(true);
    pendingListRequestId_ = api_.listOrders();
}

void OrderController::requestNavigation(qint64 stationId)
{
    if (stationId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()
        || !pendingNavigationRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在准备导航…"));
    pendingNavigationRequestId_ = api_.getStation(stationId);
}

void OrderController::requestProgress(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在刷新充电进度…"));
    pendingProgressRequestId_ = api_.getChargingProgress(orderId);
}

void OrderController::requestStop(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在结束充电并结算…"));
    pendingStopRequestId_ = api_.stopCharging(orderId);
}

void OrderController::requestPayment(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在使用钱包余额结算…"));
    pendingPaymentRequestId_ = api_.payOrder(orderId);
}

void OrderController::requestCancellation(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在取消预约…"));
    pendingCancellationRequestId_ = api_.cancel(orderId);
}

void OrderController::handleOrderList(const OrderListResult &result)
{
    if (result.response.requestId != pendingListRequestId_
        || result.response.type != QString::fromLatin1(protocol::MessageType::OrderList)) {
        return;
    }
    pendingListRequestId_.clear();
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        page_.setLoading(false);
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showError(result.response.message.isEmpty()
                            ? QStringLiteral("获取订单失败，请稍后重试")
                            : result.response.message);
        return;
    }

    page_.showOrders(result.payload->items);
    if (!noticeAfterRefresh_.isEmpty()) {
        page_.showMessage(noticeAfterRefresh_);
        noticeAfterRefresh_.clear();
    }
}

void OrderController::handleCancellation(const OrderResult &result)
{
    if (result.response.requestId != pendingCancellationRequestId_
        || result.response.type != QString::fromLatin1(protocol::MessageType::OrderCancel)) {
        return;
    }
    pendingCancellationRequestId_.clear();
    page_.setActionBusy(false);
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showDetailMessage(result.response.message.isEmpty()
                                    ? QStringLiteral("取消预约失败，请稍后重试")
                                    : result.response.message,
                                true);
        return;
    }

    page_.showListPage();
    noticeAfterRefresh_ = QStringLiteral("预约已取消，订单状态已刷新");
    refreshOrders();
}

void OrderController::handleStop(const ChargingStopResult &result)
{
    if (result.response.requestId != pendingStopRequestId_
        || result.response.type != QString::fromLatin1(protocol::MessageType::OrderStop)) {
        return;
    }
    pendingStopRequestId_.clear();
    page_.setActionBusy(false);
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showDetailMessage(result.response.message.isEmpty()
                                    ? QStringLiteral("结束充电失败，请稍后重试")
                                    : result.response.message,
                                true);
        return;
    }

    page_.showListPage();
    noticeAfterRefresh_ = result.payload->paid
        ? QStringLiteral("充电已结束并自动结算，实付 ¥%1")
              .arg(result.payload->order.amountCents / 100.0, 0, 'f', 2)
        : QStringLiteral("充电已结束，余额不足，请进入订单详情充值后结算");
    emit chargingStopped(*result.payload);
    refreshOrders();
}

void OrderController::handleProgress(const ChargingProgressResult &result)
{
    if (result.response.requestId != pendingProgressRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::OrderProgress)) {
        return;
    }
    pendingProgressRequestId_.clear();
    page_.setActionBusy(false);
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showDetailMessage(result.response.message.isEmpty()
                                    ? QStringLiteral("刷新充电进度失败，请稍后重试")
                                    : result.response.message,
                                true);
        return;
    }

    page_.updateOrderDetail(result.payload->order);
    page_.showDetailMessage(QStringLiteral("充电进度已刷新"));
}

void OrderController::handlePayment(const PaymentResult &result)
{
    if (result.response.requestId != pendingPaymentRequestId_
        || result.response.type != QString::fromLatin1(protocol::MessageType::OrderPay)) {
        return;
    }
    pendingPaymentRequestId_.clear();
    page_.setActionBusy(false);
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showDetailMessage(result.response.message.isEmpty()
                                    ? QStringLiteral("订单结算失败，请稍后重试")
                                    : result.response.message,
                                true);
        return;
    }

    page_.showListPage();
    noticeAfterRefresh_ = QStringLiteral("订单结算成功，实付 ¥%1，钱包余额 ¥%2")
                              .arg(result.payload->order.amountCents / 100.0, 0, 'f', 2)
                              .arg(result.payload->balanceCents / 100.0, 0, 'f', 2);
    refreshOrders();
}

void OrderController::handleStationDetail(const StationDetailResult &result)
{
    if (result.response.requestId != pendingNavigationRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::StationDetail)) {
        return;
    }
    pendingNavigationRequestId_.clear();
    page_.setActionBusy(false);
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showDetailMessage(
            result.response.message.isEmpty()
                ? QStringLiteral("获取导航站点失败，请稍后重试")
                : result.response.message,
            true);
        return;
    }
    emit navigationReady(result.payload->station);
}

}  // namespace charging::client
