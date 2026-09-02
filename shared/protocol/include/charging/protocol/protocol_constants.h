#pragma once

#include <QtGlobal>

namespace charging::protocol {

inline constexpr int kProtocolVersion = 1;
inline constexpr quint32 kMaxFrameBodyBytes = 256U * 1024U;

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

}  // namespace MessageType

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
