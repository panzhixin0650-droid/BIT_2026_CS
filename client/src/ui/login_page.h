// 文件用途：登录页界面的声明
#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace charging::client {

// 页面只管输入与提示，校验逻辑在控制器
class LoginPage final : public QWidget {
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

    // 提供取号、演示码提示、加载与错误状态设置
    [[nodiscard]] QString phone() const;
    void showDemoVerificationCode(const QString &code);
    void clearVerificationCode();
    void setLoading(bool loading);
    void setErrorMessage(const QString &message);
    void clearErrorMessage();

// 登录与获取验证码的请求信号
signals:
    void loginRequested(const QString &phone, const QString &verificationCode);
    void verificationCodeRequested(const QString &phone);

private:
    // submit 汇总输入并发出登录请求
    void submit();

    // 输入框、按钮与提示标签成员
    QLineEdit *phoneInput_ = nullptr;
    QLineEdit *verificationCodeInput_ = nullptr;
    QPushButton *sendCodeButton_ = nullptr;
    QLabel *codeHintLabel_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QLabel *errorLabel_ = nullptr;
};

}  // namespace charging::client
