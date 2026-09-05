#include "ui/station_browser_controller.h"

#include "api/i_charging_api.h"
#include "charging/protocol/protocol_constants.h"
#include "ui/api_error_message.h"
#include "ui/station_browser_page.h"

namespace charging::client {

StationBrowserController::StationBrowserController(StationBrowserPage &page,
                                                   IChargingApi &api,
                                                   QObject *parent)
    : QObject(parent), page_(page), api_(api)
{
    connect(&page_, &StationBrowserPage::refreshRequested,
            this, &StationBrowserController::refreshStations);
    connect(&page_, &StationBrowserPage::stationSelected,
            this, &StationBrowserController::requestStation);
    connect(&page_, &StationBrowserPage::currentOrderNavigationRequested,
            this, &StationBrowserController::navigateToStation);
    connect(&page_, &StationBrowserPage::reservationRequested,
            this, &StationBrowserController::requestReservation);
    connect(&page_, &StationBrowserPage::cancellationRequested,
            this, &StationBrowserController::requestCancellation);
    connect(&page_, &StationBrowserPage::progressRequested,
            this, &StationBrowserController::requestProgress);
    connect(&page_, &StationBrowserPage::stopRequested,
            this, &StationBrowserController::requestStop);
    connect(&page_, &StationBrowserPage::detailBackRequested, this, [this]() {
        pendingDetailRequestId_.clear();
        page_.reset();
        refreshStations();
    });
    connect(&api_, &IChargingApi::stationListCompleted,
            this, &StationBrowserController::handleStationList);
    connect(&api_, &IChargingApi::stationDetailCompleted,
            this, &StationBrowserController::handleStationDetail);
    connect(&api_, &IChargingApi::currentOrderCompleted,
            this, &StationBrowserController::handleCurrentOrder);
    connect(&api_, &IChargingApi::reservationCompleted,
            this, &StationBrowserController::handleReservation);
    connect(&api_, &IChargingApi::cancellationCompleted,
            this, &StationBrowserController::handleCancellation);
    connect(&api_, &IChargingApi::chargingProgressCompleted,
            this, &StationBrowserController::handleProgress);
    connect(&api_, &IChargingApi::chargingStopCompleted,
            this, &StationBrowserController::handleStop);
}

void StationBrowserController::refreshStations()
{
    page_.setListLoading(true);
    pendingListRequestId_ = api_.listStations(page_.stationQuery());
    refreshCurrentOrder();
}

void StationBrowserController::requestStation(qint64 stationId)
{
    selectedStationId_ = stationId;
    page_.showDetailLoading();
    pendingDetailRequestId_ = api_.getStation(stationId);
}

void StationBrowserController::navigateToStation(qint64 stationId)
{
    if (stationId <= 0 || !pendingNavigationRequestId_.isEmpty()
        || !pendingDetailRequestId_.isEmpty()) {
        return;
    }
    page_.setReservationBusy(true);
    page_.showListMessage(QStringLiteral("正在准备导航…"));
    pendingNavigationRequestId_ = api_.getStation(stationId);
}

void StationBrowserController::requestReservation(const QString &pileCode)
{
    if (!pendingReservationRequestId_.isEmpty()
        || !pendingCancellationRequestId_.isEmpty()
        || currentOrderPurpose_ == CurrentOrderPurpose::BeforeReservation) {
        return;
    }
    pendingReservationPileCode_ = pileCode;
    currentOrderPurpose_ = CurrentOrderPurpose::BeforeReservation;
    page_.setReservationBusy(true);
    page_.showDetailMessage(QStringLiteral("正在检查当前订单…"));
    if (pendingCurrentOrderRequestId_.isEmpty()) {
        pendingCurrentOrderRequestId_ = api_.getCurrentOrder();
    }
}

void StationBrowserController::requestCancellation(qint64 orderId)
{
    if (!pendingCancellationRequestId_.isEmpty()
        || !pendingReservationRequestId_.isEmpty()
        || orderId <= 0) {
        return;
    }
    page_.setReservationBusy(true);
    page_.showListMessage(QStringLiteral("正在取消预约…"));
    pendingCancellationRequestId_ = api_.cancel(orderId);
}

void StationBrowserController::requestProgress(qint64 orderId)
{
    if (!pendingProgressRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || orderId <= 0) {
        return;
    }
    page_.setReservationBusy(true);
    page_.showListMessage(QStringLiteral("正在刷新充电进度…"));
    pendingProgressRequestId_ = api_.getChargingProgress(orderId);
}

void StationBrowserController::requestStop(qint64 orderId)
{
    if (!pendingProgressRequestId_.isEmpty()
        || !pendingStopRequestId_.isEmpty()
        || orderId <= 0) {
        return;
    }
    page_.setReservationBusy(true);
    page_.showListMessage(QStringLiteral("正在结束充电并结算…"));
    pendingStopRequestId_ = api_.stopCharging(orderId);
}

void StationBrowserController::refreshCurrentOrder()
{
    if (!pendingCurrentOrderRequestId_.isEmpty()) {
        return;
    }
    currentOrderPurpose_ = CurrentOrderPurpose::Refresh;
    pendingCurrentOrderRequestId_ = api_.getCurrentOrder();
}

void StationBrowserController::handleStationList(const StationListResult &result)
{
    if (pendingListRequestId_.isEmpty()
        || result.response.requestId != pendingListRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::StationList)) {
        return;
    }
    pendingListRequestId_.clear();
    if (handleAuthenticationFailure(result.response.code)) {
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showListError(apiErrorMessage(result.response, QStringLiteral("获取充电站失败，请稍后重试")));
        return;
    }
    page_.showStations(result.payload->items);
}

void StationBrowserController::handleStationDetail(const StationDetailResult &result)
{
    const bool navigationResponse =
        !pendingNavigationRequestId_.isEmpty()
        && result.response.requestId == pendingNavigationRequestId_;
    const bool detailResponse = !pendingDetailRequestId_.isEmpty()
        && result.response.requestId == pendingDetailRequestId_;
    if ((!navigationResponse && !detailResponse)
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::StationDetail)) {
        return;
    }
    if (navigationResponse) {
        pendingNavigationRequestId_.clear();
        page_.setReservationBusy(false);
        if (handleAuthenticationFailure(result.response.code)) {
            return;
        }
        if (!result.ok() || !result.payload.has_value()) {
            page_.showListMessage(
                apiErrorMessage(result.response, QStringLiteral("获取导航站点失败，请稍后重试")),
                true);
            return;
        }
        page_.showListPage();
        page_.showListMessage({});
        emit navigationReady(result.payload->station);
        return;
    }
    pendingDetailRequestId_.clear();
    if (handleAuthenticationFailure(result.response.code)) {
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showDetailError(apiErrorMessage(result.response, QStringLiteral("获取充电站详情失败，请稍后重试")));
        return;
    }
    page_.showStationDetail(*result.payload);
    if (!detailNoticeAfterRefresh_.isEmpty()) {
        page_.showDetailMessage(detailNoticeAfterRefresh_, true);
        detailNoticeAfterRefresh_.clear();
    }
}

void StationBrowserController::handleCurrentOrder(const CurrentOrderResult &result)
{
    if (pendingCurrentOrderRequestId_.isEmpty()
        || result.response.requestId != pendingCurrentOrderRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::OrderCurrent)) {
        return;
    }
    pendingCurrentOrderRequestId_.clear();
    const CurrentOrderPurpose purpose = currentOrderPurpose_;
    currentOrderPurpose_ = CurrentOrderPurpose::None;
    if (handleAuthenticationFailure(result.response.code)) {
        page_.setReservationBusy(false);
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.setReservationBusy(false);
        const QString message = apiErrorMessage(result.response, QStringLiteral("获取当前订单失败，请稍后重试"));
        if (purpose == CurrentOrderPurpose::BeforeReservation) {
            page_.showDetailMessage(message, true);
        } else {
            page_.showListMessage(message, true);
        }
        return;
    }

    page_.showCurrentOrder(result.payload->order);
    if (purpose != CurrentOrderPurpose::BeforeReservation) {
        return;
    }
    if (result.payload->order.has_value()) {
        page_.setReservationBusy(false);
        page_.showListPage();
        const protocol::OrderStatus status = result.payload->order->status;
        page_.showListMessage(
            status == protocol::OrderStatus::PendingPayment
                ? QStringLiteral("您有待支付订单，请先完成结算")
                : QStringLiteral("您已有进行中的订单，请先处理当前订单"),
            true);
        emit currentOrderRequiresAttention(status);
        return;
    }

    page_.showDetailMessage(QStringLiteral("正在预约…"));
    pendingReservationRequestId_ = api_.reserve(pendingReservationPileCode_);
    pendingReservationPileCode_.clear();
}

void StationBrowserController::handleReservation(const OrderResult &result)
{
    if (pendingReservationRequestId_.isEmpty()
        || result.response.requestId != pendingReservationRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::OrderReserve)) {
        return;
    }
    pendingReservationRequestId_.clear();
    page_.setReservationBusy(false);
    if (handleAuthenticationFailure(result.response.code)) {
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        const QString message = apiErrorMessage(result.response, QStringLiteral("预约失败，请稍后重试"));
        if (result.response.code == protocol::ErrorCode::CurrentOrderExists) {
            page_.showListPage();
            page_.showListMessage(message, true);
            refreshStations();
        } else if (result.response.code == protocol::ErrorCode::PileNotAvailable
                   && selectedStationId_ > 0) {
            detailNoticeAfterRefresh_ = message;
            requestStation(selectedStationId_);
        } else {
            page_.showDetailMessage(message, true);
        }
        return;
    }

    page_.showListPage();
    page_.showListMessage(QStringLiteral("预约成功"));
    refreshStations();
}

void StationBrowserController::handleCancellation(const OrderResult &result)
{
    if (pendingCancellationRequestId_.isEmpty()
        || result.response.requestId != pendingCancellationRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::OrderCancel)) {
        return;
    }
    pendingCancellationRequestId_.clear();
    page_.setReservationBusy(false);
    if (handleAuthenticationFailure(result.response.code)) {
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showListMessage(apiErrorMessage(result.response, QStringLiteral("取消预约失败，请稍后重试")),
                              true);
        refreshStations();
        return;
    }

    page_.showListMessage(QStringLiteral("预约已取消"));
    refreshStations();
}

void StationBrowserController::handleProgress(const ChargingProgressResult &result)
{
    if (pendingProgressRequestId_.isEmpty()
        || result.response.requestId != pendingProgressRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::OrderProgress)) {
        return;
    }
    pendingProgressRequestId_.clear();
    page_.setReservationBusy(false);
    if (handleAuthenticationFailure(result.response.code)) {
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showListMessage(apiErrorMessage(result.response, QStringLiteral("刷新充电进度失败，请稍后重试")),
                              true);
        refreshCurrentOrder();
        return;
    }

    page_.showCurrentOrder(result.payload->order);
    page_.showListMessage(QStringLiteral("充电进度已刷新"));
}

void StationBrowserController::handleStop(const ChargingStopResult &result)
{
    if (pendingStopRequestId_.isEmpty()
        || result.response.requestId != pendingStopRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::OrderStop)) {
        return;
    }
    pendingStopRequestId_.clear();
    page_.setReservationBusy(false);
    if (handleAuthenticationFailure(result.response.code)) {
        return;
    }
    if (!result.ok() || !result.payload.has_value()) {
        page_.showListMessage(apiErrorMessage(result.response, QStringLiteral("结束充电失败，请稍后重试")),
                              true);
        refreshCurrentOrder();
        return;
    }

    synchronizeChargingStop(*result.payload);
}

void StationBrowserController::synchronizeChargingStop(
    const ChargingStopPayload &result)
{
    if (result.paid) {
        page_.showListMessage(
            QStringLiteral("充电已结束并自动结算，实付 ¥%1")
                .arg(result.order.amountCents / 100.0, 0, 'f', 2));
    } else {
        page_.showListMessage(
            QStringLiteral("充电已结束，余额不足，还需支付 ¥%1")
                .arg(result.shortfallCents.value_or(0) / 100.0, 0, 'f', 2),
            true);
    }
    refreshStations();
}

bool StationBrowserController::handleAuthenticationFailure(int code)
{
    if (code != protocol::ErrorCode::InvalidSession) {
        return false;
    }
    reset();
    emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
    return true;
}

void StationBrowserController::reset()
{
    pendingListRequestId_.clear();
    pendingDetailRequestId_.clear();
    pendingNavigationRequestId_.clear();
    pendingCurrentOrderRequestId_.clear();
    pendingReservationRequestId_.clear();
    pendingCancellationRequestId_.clear();
    pendingProgressRequestId_.clear();
    pendingStopRequestId_.clear();
    pendingReservationPileCode_.clear();
    detailNoticeAfterRefresh_.clear();
    selectedStationId_ = 0;
    currentOrderPurpose_ = CurrentOrderPurpose::None;
    page_.setReservationBusy(false);
    page_.reset();
}

}  // namespace charging::client
