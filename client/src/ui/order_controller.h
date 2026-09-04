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

private:
    void requestCancellation(qint64 orderId);
    void handleOrderList(const OrderListResult &result);
    void handleCancellation(const OrderResult &result);

    OrderPage &page_;
    IChargingApi &api_;
    QString pendingListRequestId_;
    QString pendingCancellationRequestId_;
    QString noticeAfterRefresh_;
};

}  // namespace charging::client
