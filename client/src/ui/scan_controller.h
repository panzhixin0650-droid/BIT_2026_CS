#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class ScanPage;

class ScanController final : public QObject {
    Q_OBJECT

public:
    ScanController(ScanPage &page, IChargingApi &api, QObject *parent = nullptr);

public slots:
    void submitPileCode(const QString &pileCode);

signals:
    void authenticationRequired(const QString &message);
    void chargingStarted(const protocol::OrderDto &order);
    void currentOrderRequiresAttention(protocol::OrderStatus status);

private:
    void handleCurrentOrder(const CurrentOrderResult &result);
    void handleChargingStart(const OrderResult &result);
    void start(std::optional<qint64> reservationOrderId);
    void finishRequest();

    ScanPage &page_;
    IChargingApi &api_;
    QString pendingCurrentOrderRequestId_;
    QString pendingStartRequestId_;
    QString pendingPileCode_;
};

}  // namespace charging::client
