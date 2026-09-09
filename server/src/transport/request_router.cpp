// 本文件把协议请求按消息类型分发到应用服务方法
#include "request_router.h"

#include "charging/protocol/protocol_constants.h"

namespace charging::server {

RequestRouter::RequestRouter(ApplicationService *service)
    : service_(service)
{
}

// 路由入口：先复制版本、类型与请求ID到响应
charging::protocol::ResponseEnvelope RequestRouter::route(
    const charging::protocol::RequestEnvelope &request) const
{
    using namespace charging::protocol;

    ResponseEnvelope response;
    response.version = request.version;
    response.type = request.type;
    response.requestId = request.requestId;

    // 应用服务缺失时直接返回内部错误
    if (service_ == nullptr) {
        response.code = ErrorCode::InternalError;
        response.message = QStringLiteral("INTERNAL_ERROR");
        return response;
    }

    ServiceResult result;
    // 按消息类型逐一匹配对应的业务方法，令牌随请求传入
    if (request.type == MessageType::SystemPing) {
        result = service_->ping(request.data);
    } else if (request.type == MessageType::AuthUserLogin) {
        result = service_->loginUser(request.data);
    } else if (request.type == MessageType::AuthLogout) {
        result = service_->logout(request.token.value_or(QString{}));
    } else if (request.type == MessageType::UserProfileGet) {
        result = service_->getProfile(request.token.value_or(QString{}));
    } else if (request.type == MessageType::UserProfileUpdate) {
        result = service_->updateProfile(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::WalletRecharge) {
        result = service_->recharge(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::StationList) {
        result = service_->listStations(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::StationDetail) {
        result = service_->getStation(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderCurrent) {
        result = service_->getCurrentOrder(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderList) {
        result = service_->listUserOrders(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderReserve) {
        result = service_->reserveOrder(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderCancel) {
        result = service_->cancelOrder(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderStart) {
        result = service_->startOrder(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderProgress) {
        result = service_->getOrderProgress(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderStop) {
        result = service_->stopOrder(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::OrderPay) {
        result = service_->payOrder(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::SupportTicketCreate) {
        result = service_->createSupportTicket(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::SupportTicketList) {
        result = service_->listSupportTickets(request.token.value_or(QString{}), request.data);
    } else if (request.type == MessageType::SupportTicketDetail) {
        result = service_->getSupportTicket(request.token.value_or(QString{}), request.data);
    } else {
        // The remaining routes are added in later, focused changes. Keeping
        // the fallback here makes unsupported messages fail predictably.
        // 未实现的消息类型统一按无效请求处理
        result = ServiceResult::failure(ErrorCode::InvalidRequest,
                                        QStringLiteral("INVALID_REQUEST"));
    }

    // 把业务结果的错误码与数据填入响应信封
    response.code = result.code;
    response.message = result.message;
    response.data = std::move(result.data);
    return response;
}

}  // namespace charging::server
