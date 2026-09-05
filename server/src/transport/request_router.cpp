#include "request_router.h"

#include "charging/protocol/protocol_constants.h"

namespace charging::server {

RequestRouter::RequestRouter(ApplicationService *service)
    : service_(service)
{
}

charging::protocol::ResponseEnvelope RequestRouter::route(
    const charging::protocol::RequestEnvelope &request) const
{
    using namespace charging::protocol;

    ResponseEnvelope response;
    response.version = request.version;
    response.type = request.type;
    response.requestId = request.requestId;

    if (service_ == nullptr) {
        response.code = ErrorCode::InternalError;
        response.message = QStringLiteral("INTERNAL_ERROR");
        return response;
    }

    ServiceResult result;
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
    } else {
        // The remaining routes are added in later, focused changes. Keeping
        // the fallback here makes unsupported messages fail predictably.
        result = ServiceResult::failure(ErrorCode::InvalidRequest,
                                        QStringLiteral("INVALID_REQUEST"));
    }

    response.code = result.code;
    response.message = result.message;
    response.data = std::move(result.data);
    return response;
}

}  // namespace charging::server
