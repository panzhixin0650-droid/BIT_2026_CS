// 文件用途：登录页控制器的声明
#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class LoginPage;

class LoginController final : public QObject {
    Q_OBJECT

public:
    // 控制器组合登录页与充电接口
    LoginController(LoginPage &page, IChargingApi &api, QObject *parent = nullptr);

signals:
    // 登录成功信号带用户信息与是否首次注册
    void loginSucceeded(const charging::protocol::UserDto &user, bool isNewUser);

private:
    // 内部完成校验、发起请求与结果处理
    void submitLogin(const QString &phone, const QString &verificationCode);
    void requestVerificationCode(const QString &phone);
    void handleLoginCompleted(const LoginResult &result);
    [[nodiscard]] QString errorMessage(const ApiResponse &response) const;

    LoginPage &page_;
    IChargingApi &api_;
    // pendingRequestId_ 防止重复提交登录
    QString pendingRequestId_;
};

}  // namespace charging::client
