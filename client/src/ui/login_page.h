#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace charging::client {

class LoginPage final : public QWidget {
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

    [[nodiscard]] QString phone() const;
    void setLoading(bool loading);
    void setErrorMessage(const QString &message);
    void clearErrorMessage();

signals:
    void loginRequested(const QString &phone);

private:
    void submit();

    QLineEdit *phoneInput_ = nullptr;
    QPushButton *loginButton_ = nullptr;
    QLabel *errorLabel_ = nullptr;
};

}  // namespace charging::client
