#include "ui/login_controller.h"

#include "api/i_charging_api.h"
#include "charging/protocol/protocol_constants.h"
#include "ui/login_page.h"

#include <QRegularExpression>

namespace charging::client {

namespace {

const QRegularExpression kPhonePattern(QStringLiteral("^\\d{11}$"));

}  // namespace

LoginController::LoginController(LoginPage &page, IChargingApi &api, QObject *parent)
    : QObject(parent)
    , page_(page)
    , api_(api)
{
    connect(&page_, &LoginPage::loginRequested, this, &LoginController::submitLogin);
    connect(&api_,
            &IChargingApi::loginCompleted,
            this,
            &LoginController::handleLoginCompleted);
}

void LoginController::submitLogin(const QString &phone)
{
    if (!pendingRequestId_.isEmpty()) {
        return;
    }

    if (!kPhonePattern.match(phone).hasMatch()) {
        page_.setErrorMessage(QStringLiteral("请输入11位数字手机号"));
        return;
    }

    page_.clearErrorMessage();
    page_.setLoading(true);
    pendingRequestId_ = api_.loginUser(phone);
}

void LoginController::handleLoginCompleted(const LoginResult &result)
{
    if (pendingRequestId_.isEmpty()
        || result.response.requestId != pendingRequestId_
        || result.response.type
            != QString::fromLatin1(protocol::MessageType::AuthUserLogin)) {
        return;
    }

    pendingRequestId_.clear();
    page_.setLoading(false);

    if (!result.ok()) {
        page_.setErrorMessage(errorMessage(result.response));
        return;
    }

    if (!result.payload.has_value()) {
        page_.setErrorMessage(QStringLiteral("服务返回的数据不完整，请稍后重试"));
        return;
    }

    page_.clearErrorMessage();
    emit loginSucceeded(result.payload->user, result.payload->isNewUser);
}

QString LoginController::errorMessage(const ApiResponse &response) const
{
    switch (response.code) {
    case protocol::ErrorCode::InvalidRequest:
        return QStringLiteral("手机号格式不正确，请检查后重试");
    case protocol::ErrorCode::Forbidden:
        return QStringLiteral("该账号当前不可登录，请联系管理员");
    case protocol::ErrorCode::ServiceUnavailable:
        return QStringLiteral("暂时无法连接服务，请检查网络后重试");
    default:
        return response.message.isEmpty() ? QStringLiteral("登录失败，请稍后重试")
                                          : response.message;
    }
}

}  // namespace charging::client
