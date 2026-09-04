#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class OrderPage;

class OrderController final : public QObject {
    Q_OBJECT

public:
    OrderController(OrderPage &page, IChargingApi &api, QObject *parent = nullptr);

    void refreshOrders();

signals:
    void authenticationRequired(const QString &message);
    void rechargeRequested();
    void navigationReady(const protocol::StationDto &station);
    void chargingStopped(const ChargingStopPayload &result);

private:
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

    OrderPage &page_;
    IChargingApi &api_;
    QString pendingListRequestId_;
    QString pendingCancellationRequestId_;
    QString pendingStopRequestId_;
    QString pendingProgressRequestId_;
    QString pendingPaymentRequestId_;
    QString pendingNavigationRequestId_;
    QString noticeAfterRefresh_;
};

}  // namespace charging::client
