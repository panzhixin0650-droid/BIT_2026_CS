#pragma once

#include "api/i_charging_api.h"

#include <QHash>

#include <optional>

namespace charging::client {

class MockChargingApi final : public IChargingApi {
    Q_OBJECT

public:
    explicit MockChargingApi(QObject *parent = nullptr);

    [[nodiscard]] QString loginUser(const QString &phone) override;
    [[nodiscard]] QString logout() override;
    [[nodiscard]] QString getProfile() override;
    [[nodiscard]] QString updateNickname(const QString &nickname) override;
    [[nodiscard]] QString recharge(qint64 amountCents) override;
    [[nodiscard]] QString listStations(const StationQuery &query) override;
    [[nodiscard]] QString getStation(qint64 stationId) override;
    [[nodiscard]] QString getCurrentOrder() override;
    [[nodiscard]] QString listOrders() override;
    [[nodiscard]] QString reserve(const QString &pileCode) override;
    [[nodiscard]] QString cancel(qint64 orderId) override;

private:
    [[nodiscard]] QString nextRequestId();
    [[nodiscard]] ApiResponse response(const QString &requestId,
                                       const char *type,
                                       int code,
                                       const QString &message) const;
    [[nodiscard]] std::optional<protocol::UserDto> authenticatedUser() const;
    [[nodiscard]] protocol::StationDto station(qint64 stationId) const;
    [[nodiscard]] QList<protocol::PileDto> piles(qint64 stationId) const;
    [[nodiscard]] std::optional<protocol::OrderDto> currentOrder(qint64 userId) const;

    QHash<QString, protocol::UserDto> usersByPhone_;
    QHash<QString, protocol::PileDto> pilesByCode_;
    QHash<qint64, protocol::OrderDto> ordersById_;
    QString authenticatedPhone_;
    QString token_;
    qint64 nextUserId_ = 2;
    qint64 nextOrderId_ = 1001;
    quint64 requestSequence_ = 0;
};

}  // namespace charging::client
