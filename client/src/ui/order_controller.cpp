// 订单控制器：串接订单页操作与后端订单接口
#include "ui/order_controller.h"

#include "api/i_charging_api.h"
#include "charging/protocol/protocol_constants.h"
#include "ui/api_error_message.h"
#include "ui/order_page.h"

namespace charging::client {

// 构造时连接页面操作与各类接口回调
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

// 离开页面只放弃导航意图，已提交业务仍继续
void OrderController::leavePage()
{
    // Only abandon navigation intent; submitted business operations still complete.
    pendingNavigationRequestId_.clear();
    page_.setActionBusy(!pendingListRequestId_.isEmpty()
                       || !pendingCancellationRequestId_.isEmpty()
                       || !pendingStopRequestId_.isEmpty()
                       || !pendingProgressRequestId_.isEmpty()
                       || !pendingPaymentRequestId_.isEmpty());
    page_.showListPage();
}

// 有请求在途时不重复拉取订单列表
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

// 先查站点详情，再交由主窗口打开导航
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

// 刷新指定订单的充电进度
void OrderController::requestProgress(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()
        || !pendingNavigationRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在刷新充电进度…"));
    pendingProgressRequestId_ = api_.getChargingProgress(orderId);
}

// 请求结束充电，由服务端结算金额
void OrderController::requestStop(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()
        || !pendingNavigationRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在结束充电并结算…"));
    pendingStopRequestId_ = api_.stopCharging(orderId);
}

// 用钱包余额结算待支付订单
void OrderController::requestPayment(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()
        || !pendingNavigationRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在使用钱包余额结算…"));
    pendingPaymentRequestId_ = api_.payOrder(orderId);
}

// 取消预约订单
void OrderController::requestCancellation(qint64 orderId)
{
    if (orderId <= 0 || !pendingListRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || !pendingProgressRequestId_.isEmpty()
        || !pendingPaymentRequestId_.isEmpty()
        || !pendingNavigationRequestId_.isEmpty()) {
        return;
    }
    page_.setActionBusy(true);
    page_.showDetailMessage(QStringLiteral("正在取消预约…"));
    pendingCancellationRequestId_ = api_.cancel(orderId);
}

// 回调先校验请求编号与消息类型再处理
void OrderController::handleOrderList(const OrderListResult &result)
{
    if (pendingListRequestId_.isEmpty()
        || result.response.requestId != pendingListRequestId_
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
        page_.showError(apiErrorMessage(result.response, QStringLiteral("获取订单失败，请稍后重试")));
        return;
    }

    page_.showOrders(result.payload->items);
    if (!noticeAfterRefresh_.isEmpty()) {
        page_.showMessage(noticeAfterRefresh_);
        noticeAfterRefresh_.clear();
    }
}

// 取消成功后回列表并重新刷新订单
void OrderController::handleCancellation(const OrderResult &result)
{
    if (pendingCancellationRequestId_.isEmpty()
        || result.response.requestId != pendingCancellationRequestId_
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
        page_.showDetailMessage(apiErrorMessage(result.response, QStringLiteral("取消预约失败，请稍后重试")),
                                true);
        return;
    }

    page_.showListPage();
    noticeAfterRefresh_ = QStringLiteral("预约已取消，订单状态已刷新");
    refreshOrders();
}

// 结束充电结果：按是否结清生成不同提示
void OrderController::handleStop(const ChargingStopResult &result)
{
    if (pendingStopRequestId_.isEmpty()
        || result.response.requestId != pendingStopRequestId_
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
        page_.showDetailMessage(apiErrorMessage(result.response, QStringLiteral("结束充电失败，请稍后重试")),
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

// 进度刷新成功则更新当前打开的详情
void OrderController::handleProgress(const ChargingProgressResult &result)
{
    if (pendingProgressRequestId_.isEmpty()
        || result.response.requestId != pendingProgressRequestId_
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
        page_.showDetailMessage(apiErrorMessage(result.response, QStringLiteral("刷新充电进度失败，请稍后重试")),
                                true);
        return;
    }

    if (page_.updateOrderDetail(result.payload->order)) {
        page_.showDetailMessage(QStringLiteral("充电进度已刷新"));
    }
}

// 结算成功后提示实付与余额并刷新列表
void OrderController::handlePayment(const PaymentResult &result)
{
    if (pendingPaymentRequestId_.isEmpty()
        || result.response.requestId != pendingPaymentRequestId_
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
        page_.showDetailMessage(apiErrorMessage(result.response, QStringLiteral("订单结算失败，请稍后重试")),
                                true);
        return;
    }

    page_.showListPage();
    noticeAfterRefresh_ = QStringLiteral("订单结算成功，实付 ¥%1，钱包余额 ¥%2")
                              .arg(result.payload->order.amountCents / 100.0, 0, 'f', 2)
                              .arg(result.payload->balanceCents / 100.0, 0, 'f', 2);
    refreshOrders();
}

// 站点详情返回后发出导航就绪信号
void OrderController::handleStationDetail(const StationDetailResult &result)
{
    if (pendingNavigationRequestId_.isEmpty()
        || result.response.requestId != pendingNavigationRequestId_
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
            apiErrorMessage(result.response, QStringLiteral("获取导航站点失败，请稍后重试")),
            true);
        return;
    }
    emit navigationReady(result.payload->station);
}

// 重置：清空在途请求与页面状态
void OrderController::reset()
{
    pendingListRequestId_.clear();
    pendingCancellationRequestId_.clear();
    pendingStopRequestId_.clear();
    pendingProgressRequestId_.clear();
    pendingPaymentRequestId_.clear();
    pendingNavigationRequestId_.clear();
    noticeAfterRefresh_.clear();
    page_.reset();
}

}  // namespace charging::client
