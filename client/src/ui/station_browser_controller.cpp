#include "ui/station_browser_controller.h"

#include "api/i_charging_api.h"
#include "charging/protocol/protocol_constants.h"
#include "ui/api_error_message.h"
#include "ui/station_browser_page.h"
#include "ui/reservation_hint.h"

#include <QAbstractButton>
#include <QMessageBox>

namespace charging::client {

// 充电站浏览控制器：串联站点列表、详情与预约充电流程
StationBrowserController::StationBrowserController(StationBrowserPage &page,
                                                   IChargingApi &api,
                                                   QObject *parent)
    : QObject(parent), page_(page), api_(api)
{
    // 把页面操作与API回调逐个连接起来
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
        selectedStationId_ = 0;
        page_.showListPage();
    });
    // 订单列表用于展示到访历史
    connect(&api_, &IChargingApi::orderListCompleted, this, [this](const OrderListResult &result) {
        if (pendingHistoryRequestId_.isEmpty() || result.response.requestId != pendingHistoryRequestId_
            || result.response.type != QString::fromLatin1(protocol::MessageType::OrderList)) return;
        pendingHistoryRequestId_.clear();
        if (handleAuthenticationFailure(result.response.code)) return;
        if (result.ok() && result.payload) page_.showVisitHistory(result.payload->items);
        else page_.showVisitHistoryError();
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

// 刷新站点：拉全量目录供地图和本地搜索使用
void StationBrowserController::refreshStations()
{
    page_.setListLoading(true);
    auto query = page_.stationQuery();
    // Keep one authoritative catalog for the map, discovery groups and local search.
    query.keyword.clear(); query.region.clear();
    pendingListRequestId_ = api_.listStations(query);
    if (pendingHistoryRequestId_.isEmpty()) pendingHistoryRequestId_ = api_.listOrders();
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

// 预约前先查当前订单，避免重复占用
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

// 取消预约由服务端确认结果，客户端只发请求
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

// 当前订单只允许一个在途查询
void StationBrowserController::refreshCurrentOrder()
{
    if (!pendingCurrentOrderRequestId_.isEmpty()) {
        return;
    }
    currentOrderPurpose_ = CurrentOrderPurpose::Refresh;
    pendingCurrentOrderRequestId_ = api_.getCurrentOrder();
}

// 校验请求ID与类型后再更新站点列表
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

// 详情响应需区分是普通查看还是导航准备
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

// 当前订单回调按用途分流：刷新展示或继续预约
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
    // 已有进行中订单时提示先处理，不发起预约
    if (result.payload->order.has_value()) {
        page_.setReservationBusy(false);
        const protocol::OrderStatus status = result.payload->order->status;
        if (status == protocol::OrderStatus::PendingPayment) {
            emit currentOrderRequiresAttention(status);
            return;
        }
        page_.showListPage();
        page_.showListMessage(
            QStringLiteral("您已有进行中的订单，请先处理当前订单"),
            true);
        emit currentOrderRequiresAttention(status);
        return;
    }

    // 确认无在途订单后才真正发出预约请求
    page_.showDetailMessage(QStringLiteral("正在预约…"));
    pendingReservationRequestId_ = api_.reserve(pendingReservationPileCode_);
    pendingReservationPileCode_.clear();
}

// 预约结果处理：区分订单冲突与电桩不可用
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

    // 弹窗提示预约成功并显示30分钟保留提示
    QMessageBox confirmation(QMessageBox::Information,
                             QStringLiteral("预约成功"),
                             QStringLiteral("已成功预约充电桩 %1。\n%2")
                                 .arg(result.payload->order.pileCode,
                                      reservationHint(result.payload->order)),
                             QMessageBox::Ok,
                             &page_);
    confirmation.setObjectName(QStringLiteral("reservationSuccessDialog"));
    confirmation.button(QMessageBox::Ok)->setText(QStringLiteral("知道了"));
    confirmation.exec();

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

// 刷新充电进度，失败时回查当前订单兜底
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

// 结束充电由服务端结算，客户端只展示结果
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
    emit chargingStopped(*result.payload);
}

// 按是否已支付显示实付或欠款，并刷新站点状态
void StationBrowserController::synchronizeChargingStop(
    const ChargingStopPayload &result)
{
    const bool refreshSelectedDetail = selectedStationId_ > 0
        && page_.isShowingStationDetail();
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
    if (refreshSelectedDetail) {
        requestStation(selectedStationId_);
    }
}

void StationBrowserController::synchronizePendingOrderSettlement(
    const PaymentPayload &result)
{
    page_.showListMessage(
        QStringLiteral("待支付订单已自动结算，实付 ¥%1")
            .arg(result.order.amountCents / 100.0, 0, 'f', 2));
    refreshStations();
}

// 会话失效则清空状态并请求重新登录
bool StationBrowserController::handleAuthenticationFailure(int code)
{
    if (code != protocol::ErrorCode::InvalidSession) {
        return false;
    }
    reset();
    emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
    return true;
}

// 重置所有在途请求，切换账号时避免串数据
void StationBrowserController::reset()
{
    pendingListRequestId_.clear();
    pendingHistoryRequestId_.clear();
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
