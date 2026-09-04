#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi : public QObject {
    Q_OBJECT

public:
    explicit IChargingApi(QObject *parent = nullptr);
    ~IChargingApi() override;

    [[nodiscard]] virtual QString loginUser(const QString &phone) = 0;
    [[nodiscard]] virtual QString logout() = 0;
    [[nodiscard]] virtual QString getProfile() = 0;
    [[nodiscard]] virtual QString updateNickname(const QString &nickname) = 0;
    [[nodiscard]] virtual QString recharge(qint64 amountCents) = 0;
    [[nodiscard]] virtual QString listStations(const StationQuery &query) = 0;
    [[nodiscard]] virtual QString getStation(qint64 stationId) = 0;
    [[nodiscard]] virtual QString getCurrentOrder() = 0;
    [[nodiscard]] virtual QString listOrders() = 0;
    [[nodiscard]] virtual QString reserve(const QString &pileCode) = 0;
    [[nodiscard]] virtual QString cancel(qint64 orderId) = 0;
    [[nodiscard]] virtual QString startCharging(
        const QString &pileCode,
        std::optional<qint64> reservationOrderId = std::nullopt) = 0;
    [[nodiscard]] virtual QString getChargingProgress(qint64 orderId) = 0;
    [[nodiscard]] virtual QString stopCharging(qint64 orderId) = 0;
    [[nodiscard]] virtual QString payOrder(qint64 orderId) = 0;

signals:
    void loginCompleted(const charging::client::LoginResult &result);
    void logoutCompleted(const charging::client::LogoutResult &result);
    void profileCompleted(const charging::client::UserResult &result);
    void profileUpdateCompleted(const charging::client::UserResult &result);
    void rechargeCompleted(const charging::client::RechargeResult &result);
    void stationListCompleted(const charging::client::StationListResult &result);
    void stationDetailCompleted(const charging::client::StationDetailResult &result);
    void currentOrderCompleted(const charging::client::CurrentOrderResult &result);
    void orderListCompleted(const charging::client::OrderListResult &result);
    void reservationCompleted(const charging::client::OrderResult &result);
    void cancellationCompleted(const charging::client::OrderResult &result);
    void chargingStartCompleted(const charging::client::OrderResult &result);
    void chargingProgressCompleted(
        const charging::client::ChargingProgressResult &result);
    void chargingStopCompleted(const charging::client::ChargingStopResult &result);
    void paymentCompleted(const charging::client::PaymentResult &result);
};

}  // namespace charging::client
