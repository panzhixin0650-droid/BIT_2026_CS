#include "ui/scan_controller.h"

#include "api/i_charging_api.h"
#include "charging/protocol/protocol_constants.h"
#include "ui/scan_page.h"

namespace charging::client {

ScanController::ScanController(ScanPage &page, IChargingApi &api, QObject *parent)
    : QObject(parent), page_(page), api_(api)
{
    connect(&page_, &ScanPage::scanRequested, this, &ScanController::submitPileCode);
    connect(&api_, &IChargingApi::currentOrderCompleted,
            this, &ScanController::handleCurrentOrder);
    connect(&api_, &IChargingApi::chargingStartCompleted,
            this, &ScanController::handleChargingStart);
}

void ScanController::submitPileCode(const QString &pileCode)
{
    if (!pendingCurrentOrderRequestId_.isEmpty()
        || !pendingStartRequestId_.isEmpty()) {
        return;
    }
    pendingPileCode_ = pileCode.trimmed();
    if (pendingPileCode_.isEmpty() || pendingPileCode_.size() > 64) {
        page_.showMessage(QStringLiteral("请输入有效的充电桩编号"), true);
        pendingPileCode_.clear();
        return;
    }

    page_.setLoading(true);
    pendingCurrentOrderRequestId_ = api_.getCurrentOrder();
}

void ScanController::handleCurrentOrder(const CurrentOrderResult &result)
{
    if (result.response.requestId != pendingCurrentOrderRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::OrderCurrent)) {
        return;
    }
    pendingCurrentOrderRequestId_.clear();
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        finishRequest();
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        const QString message = result.response.message.isEmpty()
            ? QStringLiteral("检查当前订单失败，请稍后重试")
            : result.response.message;
        finishRequest();
        page_.showMessage(message, true);
        return;
    }

    if (!result.payload->order.has_value()) {
        start(std::nullopt);
        return;
    }

    const protocol::OrderDto &current = *result.payload->order;
    if (current.status == protocol::OrderStatus::Reserved) {
        if (current.pileCode == pendingPileCode_) {
            start(current.orderId);
            return;
        }
        finishRequest();
        page_.showMessage(
            QStringLiteral("您已预约充电桩 %1，请扫描该充电桩或先取消预约")
                .arg(current.pileCode),
            true);
        emit currentOrderRequiresAttention(current.status);
        return;
    }

    finishRequest();
    if (current.status == protocol::OrderStatus::PendingPayment) {
        page_.showMessage(QStringLiteral("您有待支付订单，请先完成结算"), true);
    } else {
        page_.showMessage(QStringLiteral("您已有充电中的订单，请先处理当前订单"), true);
    }
    emit currentOrderRequiresAttention(current.status);
}

void ScanController::handleChargingStart(const OrderResult &result)
{
    if (result.response.requestId != pendingStartRequestId_
        || result.response.type != QString::fromLatin1(protocol::MessageType::OrderStart)) {
        return;
    }
    pendingStartRequestId_.clear();
    if (result.response.code == protocol::ErrorCode::InvalidSession) {
        finishRequest();
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        const QString message = result.response.message.isEmpty()
            ? QStringLiteral("开始充电失败，请稍后重试")
            : result.response.message;
        finishRequest();
        page_.showMessage(message, true);
        return;
    }

    const protocol::OrderDto order = result.payload->order;
    finishRequest();
    page_.showMessage(QStringLiteral("充电已开始"));
    emit chargingStarted(order);
}

void ScanController::start(std::optional<qint64> reservationOrderId)
{
    page_.showMessage(QStringLiteral("正在开始充电…"));
    pendingStartRequestId_ = api_.startCharging(pendingPileCode_, reservationOrderId);
}

void ScanController::finishRequest()
{
    pendingCurrentOrderRequestId_.clear();
    pendingStartRequestId_.clear();
    pendingPileCode_.clear();
    page_.setLoading(false);
}

}  // namespace charging::client
