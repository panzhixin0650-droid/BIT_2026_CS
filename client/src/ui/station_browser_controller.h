#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class StationBrowserPage;

// 充电站浏览控制器声明：负责列表、详情、预约与充电操作
class StationBrowserController final : public QObject {
    Q_OBJECT

public:
    StationBrowserController(StationBrowserPage &page,
                             IChargingApi &api,
                             QObject *parent = nullptr);

    // 外部可触发刷新、导航及结算后的同步
    void refreshStations();
    void navigateToStation(qint64 stationId);
    void synchronizeChargingStop(const ChargingStopPayload &result);
    void synchronizePendingOrderSettlement(const PaymentPayload &result);

    // Forget UI requests from the previous authenticated session.
    void reset();

// 对外信号：登录失效、订单冲突、导航就绪与充电结束
signals:
    void authenticationRequired(const QString &message);
    void currentOrderRequiresAttention(protocol::OrderStatus status);
    void navigationReady(const protocol::StationDto &station);
    void chargingStopped(const ChargingStopPayload &result);

private:
    // 记录当前订单查询目的：普通刷新或预约前检查
    enum class CurrentOrderPurpose { None, Refresh, BeforeReservation };

    void requestStation(qint64 stationId);
    void requestReservation(const QString &pileCode);
    void requestCancellation(qint64 orderId);
    void requestProgress(qint64 orderId);
    void requestStop(qint64 orderId);
    void refreshCurrentOrder();
    // 各类API响应的回调处理入口
    void handleStationList(const StationListResult &result);
    void handleStationDetail(const StationDetailResult &result);
    void handleCurrentOrder(const CurrentOrderResult &result);
    void handleReservation(const OrderResult &result);
    void handleCancellation(const OrderResult &result);
    void handleProgress(const ChargingProgressResult &result);
    void handleStop(const ChargingStopResult &result);
    [[nodiscard]] bool handleAuthenticationFailure(int code);

    StationBrowserPage &page_;
    IChargingApi &api_;
    // 按类型保存在途请求ID，用于匹配对应响应
    QString pendingListRequestId_;
    QString pendingHistoryRequestId_;
    QString pendingDetailRequestId_;
    QString pendingNavigationRequestId_;
    QString pendingCurrentOrderRequestId_;
    QString pendingReservationRequestId_;
    QString pendingCancellationRequestId_;
    QString pendingProgressRequestId_;
    QString pendingStopRequestId_;
    QString pendingReservationPileCode_;
    QString detailNoticeAfterRefresh_;
    qint64 selectedStationId_ = 0;
    CurrentOrderPurpose currentOrderPurpose_ = CurrentOrderPurpose::None;
};

}  // namespace charging::client
