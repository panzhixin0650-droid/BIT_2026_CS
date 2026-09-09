// 客户端API的统一返回类型定义：响应头加各类负载
#pragma once

#include "charging/protocol/dto.h"
#include "charging/protocol/support_ticket.h"
#include "charging/protocol/protocol_constants.h"

#include <QString>
#include <QMetaType>
#include <QList>

#include <optional>

namespace charging::client {

// 通用响应头：请求ID、消息类型、错误码与提示
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

// 模板结果：响应加可选负载，失败时负载为空
template<typename Payload>
struct ApiResult {
    ApiResponse response;
    std::optional<Payload> payload;

    [[nodiscard]] bool ok() const noexcept
    {
        return response.ok();
    }
};

// 登录负载：令牌、是否新用户与用户信息
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

// 充值结果只回传最新余额，单位为分
struct RechargePayload {
    qint64 balanceCents = 0;
};

// 站点查询条件：可选经纬度、区域与关键词
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

// 充电进度负载：订单快照与本次测量时间
struct ChargingProgressPayload {
    protocol::OrderDto order;
    QString measuredAt;
};

// 停止充电结果：订单、是否已扣款、余额与欠款差额
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

// 客服工单的单条与列表负载，列表带是否还有更多
struct TicketPayload { protocol::SupportTicketDto ticket; };
struct TicketListPayload { QList<protocol::SupportTicketDto> items; bool hasMore = false; };
using TicketResult = ApiResult<TicketPayload>;
using TicketListResult = ApiResult<TicketListPayload>;

}  // namespace charging::client

// 注册为元类型，便于用信号跨对象传递结果
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
Q_DECLARE_METATYPE(charging::client::TicketResult)
Q_DECLARE_METATYPE(charging::client::TicketListResult)
