#pragma once

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include <QString>
#include <QMetaType>
#include <QList>

#include <optional>

namespace charging::client {

struct ApiResponse {
    QString requestId;
    QString type;
    int code = protocol::ErrorCode::InternalError;
    QString message;

    [[nodiscard]] bool ok() const noexcept
    {
        return code == protocol::ErrorCode::Ok;
    }
};

template<typename Payload>
struct ApiResult {
    ApiResponse response;
    std::optional<Payload> payload;

    [[nodiscard]] bool ok() const noexcept
    {
        return response.ok();
    }
};

struct LoginPayload {
    QString token;
    bool isNewUser = false;
    protocol::UserDto user;
};

struct LogoutPayload {
    bool success = false;
};

struct UserPayload {
    protocol::UserDto user;
};

struct RechargePayload {
    qint64 balanceCents = 0;
};

struct StationQuery {
    std::optional<double> longitude;
    std::optional<double> latitude;
    QString region;
    QString keyword;
};

struct StationListPayload {
    QList<protocol::StationDto> items;
};

struct StationDetailPayload {
    protocol::StationDto station;
    QList<protocol::PileDto> piles;
};

struct CurrentOrderPayload {
    std::optional<protocol::OrderDto> order;
};

struct OrderPayload {
    protocol::OrderDto order;
};

struct OrderListPayload {
    QList<protocol::OrderDto> items;
};

struct ChargingProgressPayload {
    protocol::OrderDto order;
    QString measuredAt;
};

struct ChargingStopPayload {
    protocol::OrderDto order;
    bool paid = false;
    qint64 balanceCents = 0;
    std::optional<qint64> shortfallCents;
};

struct PaymentPayload {
    protocol::OrderDto order;
    qint64 balanceCents = 0;
};

using LoginResult = ApiResult<LoginPayload>;
using LogoutResult = ApiResult<LogoutPayload>;
using UserResult = ApiResult<UserPayload>;
using RechargeResult = ApiResult<RechargePayload>;
using StationListResult = ApiResult<StationListPayload>;
using StationDetailResult = ApiResult<StationDetailPayload>;
using CurrentOrderResult = ApiResult<CurrentOrderPayload>;
using OrderResult = ApiResult<OrderPayload>;
using OrderListResult = ApiResult<OrderListPayload>;
using ChargingProgressResult = ApiResult<ChargingProgressPayload>;
using ChargingStopResult = ApiResult<ChargingStopPayload>;
using PaymentResult = ApiResult<PaymentPayload>;

}  // namespace charging::client

Q_DECLARE_METATYPE(charging::client::LoginResult)
Q_DECLARE_METATYPE(charging::client::LogoutResult)
Q_DECLARE_METATYPE(charging::client::UserResult)
Q_DECLARE_METATYPE(charging::client::RechargeResult)
Q_DECLARE_METATYPE(charging::client::StationListResult)
Q_DECLARE_METATYPE(charging::client::StationDetailResult)
Q_DECLARE_METATYPE(charging::client::CurrentOrderResult)
Q_DECLARE_METATYPE(charging::client::OrderResult)
Q_DECLARE_METATYPE(charging::client::OrderListResult)
Q_DECLARE_METATYPE(charging::client::ChargingProgressResult)
Q_DECLARE_METATYPE(charging::client::ChargingStopResult)
Q_DECLARE_METATYPE(charging::client::PaymentResult)
