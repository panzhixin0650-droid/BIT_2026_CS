// 集中定义协议版本、消息类型与错误码常量
#pragma once

#include <QtGlobal>

namespace charging::protocol {
// Demo 参数：充电会话 180 秒、预约 30 分钟过期与高峰规则标识
inline constexpr int DemoChargingDurationSeconds = 180;
inline constexpr int DemoReservationDurationSeconds = 30 * 60;
inline constexpr auto DemoPeakPricingRule = "DEMO_PEAK_START_V1";

// 当前协议版本号与单帧消息体的字节上限
inline constexpr int kProtocolVersion = 1;
inline constexpr quint32 kMaxFrameBodyBytes = 256U * 1024U;

// 双方约定的消息类型字符串，用于路由请求
namespace MessageType {

inline constexpr auto SystemPing = "system.ping";
inline constexpr auto AuthUserLogin = "auth.user.login";
inline constexpr auto AuthLogout = "auth.logout";
inline constexpr auto UserProfileGet = "user.profile.get";
inline constexpr auto UserProfileUpdate = "user.profile.update";
inline constexpr auto WalletRecharge = "wallet.recharge";
inline constexpr auto StationList = "station.list";
inline constexpr auto StationDetail = "station.detail";
inline constexpr auto PredictionLatest = "prediction.latest";
inline constexpr auto OrderCurrent = "order.current";
inline constexpr auto OrderReserve = "order.reserve";
inline constexpr auto OrderCancel = "order.cancel";
inline constexpr auto OrderStart = "order.start";
inline constexpr auto OrderProgress = "order.progress";
inline constexpr auto OrderStop = "order.stop";
inline constexpr auto OrderPay = "order.pay";
inline constexpr auto OrderList = "order.list";
inline constexpr auto SupportTicketCreate = "support.ticket.create";
inline constexpr auto SupportTicketList = "support.ticket.list";
inline constexpr auto SupportTicketDetail = "support.ticket.detail";

}  // namespace MessageType

// 统一错误码，0 表示成功，其余按业务分类
namespace ErrorCode {

inline constexpr int Ok = 0;
inline constexpr int InvalidRequest = 40001;
inline constexpr int InvalidSession = 40101;
inline constexpr int InvalidCredentials = 40102;
inline constexpr int Forbidden = 40301;
inline constexpr int NotFound = 40401;
inline constexpr int PileNotAvailable = 40901;
inline constexpr int CurrentOrderExists = 40902;
inline constexpr int IllegalOrderState = 40903;
inline constexpr int InsufficientBalance = 42201;
inline constexpr int InternalError = 50001;
inline constexpr int ServiceUnavailable = 50301;

}  // namespace ErrorCode

}  // namespace charging::protocol
