#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class OrderPage;

// 订单页控制器：负责请求发起与结果分发
class OrderController final : public QObject {
    Q_OBJECT

public:
    OrderController(OrderPage &page, IChargingApi &api, QObject *parent = nullptr);

    // 刷新订单列表，或离开页面收敛状态
    void refreshOrders();
    void leavePage();

    // Forget UI requests from the previous authenticated session.
    void reset();

// 对外通知：需重新登录、去充值、导航、充电结束
signals:
    void authenticationRequired(const QString &message);
    void rechargeRequested();
    void navigationReady(const protocol::StationDto &station);
    void chargingStopped(const ChargingStopPayload &result);

private:
    // 页面动作转成接口请求，回调再更新界面
    void requestCancellation(qint64 orderId);
    void requestStop(qint64 orderId);
    void requestProgress(qint64 orderId);
    void requestPayment(qint64 orderId);
    void requestNavigation(qint64 stationId);
    void handleOrderList(const OrderListResult &result);
    void handleCancellation(const OrderResult &result);
    void handleStop(const ChargingStopResult &result);
    void handleProgress(const ChargingProgressResult &result);
    void handlePayment(const PaymentResult &result);
    void handleStationDetail(const StationDetailResult &result);

    // 页面与接口引用，以及各类在途请求编号
    OrderPage &page_;
    IChargingApi &api_;
    QString pendingListRequestId_;
    QString pendingCancellationRequestId_;
    QString pendingStopRequestId_;
    QString pendingProgressRequestId_;
    QString pendingPaymentRequestId_;
    QString pendingNavigationRequestId_;
    // 下次列表刷新完成后要显示的提示文案
    QString noticeAfterRefresh_;
};

}  // namespace charging::client
