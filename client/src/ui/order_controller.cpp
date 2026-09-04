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
    connect(&api_, &IChargingApi::orderListCompleted,
            this, &OrderController::handleOrderList);
    connect(&api_, &IChargingApi::cancellationCompleted,
            this, &OrderController::handleCancellation);
}

void OrderController::refreshOrders()
{
    if (!pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()) {
        return;
    }
    page_.setLoading(true);
    pendingListRequestId_ = api_.listOrders();
}

void OrderController::requestCancellation(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()) {
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

}  // namespace charging::client
