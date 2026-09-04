#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class StationBrowserPage;

class StationBrowserController final : public QObject {
    Q_OBJECT

public:
    StationBrowserController(StationBrowserPage &page,
                             IChargingApi &api,
                             QObject *parent = nullptr);

    void refreshStations();

signals:
    void authenticationRequired(const QString &message);
    void currentOrderRequiresAttention(protocol::OrderStatus status);

private:
    enum class CurrentOrderPurpose { None, Refresh, BeforeReservation };

    void requestStation(qint64 stationId);
    void requestReservation(const QString &pileCode);
    void requestCancellation(qint64 orderId);
    void requestProgress(qint64 orderId);
    void requestStop(qint64 orderId);
    void refreshCurrentOrder();
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
    QString pendingListRequestId_;
    QString pendingDetailRequestId_;
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
