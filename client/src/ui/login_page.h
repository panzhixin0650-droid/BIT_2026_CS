#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace charging::client {

class LoginPage final : public QWidget {
    Q_OBJECT

public:
    enum class LayoutMode { Mobile, Vehicle };

    explicit LoginPage(QWidget *parent = nullptr,
                       LayoutMode layoutMode = LayoutMode::Mobile);

    [[nodiscard]] QString phone() const;
    void showDemoVerificationCode(const QString &code);
    void clearVerificationCode();
    void setLoading(bool loading);
    void setErrorMessage(const QString &message);
    void clearErrorMessage();

signals:
    void loginRequested(const QString &phone, const QString &verificationCode);
    void verificationCodeRequested(const QString &phone);

private:
    void submit();

    QLineEdit *phoneInput_ = nullptr;
    QLineEdit *verificationCodeInput_ = nullptr;
    QPushButton *sendCodeButton_ = nullptr;
    QLabel *codeHintLabel_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QLabel *errorLabel_ = nullptr;
};

}  // namespace charging::client
