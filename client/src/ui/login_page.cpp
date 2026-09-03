#include "ui/login_page.h"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace charging::client {

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("loginPage"));

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(28, 32, 28, 32);
    pageLayout->addStretch();

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setMaximumWidth(380);
    card->setStyleSheet(QStringLiteral(
        "QFrame#loginCard { background: white; border: 1px solid #dde3ea; "
        "border-radius: 16px; }"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(28, 32, 28, 32);
    cardLayout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("欢迎使用"), card);
    title->setObjectName(QStringLiteral("loginTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *subtitle = new QLabel(QStringLiteral("手机号登录，新用户将自动注册"), card);
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    subtitle->setStyleSheet(QStringLiteral("color: #667085;"));

    auto *phoneLabel = new QLabel(QStringLiteral("手机号"), card);
    phoneInput_ = new QLineEdit(card);
    phoneInput_->setObjectName(QStringLiteral("phoneInput"));
    phoneInput_->setPlaceholderText(QStringLiteral("请输入11位手机号"));
    phoneInput_->setMaxLength(11);
    phoneInput_->setClearButtonEnabled(true);
    phoneInput_->setMinimumHeight(42);
    phoneInput_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{0,11}")), phoneInput_));

    errorLabel_ = new QLabel(card);
    errorLabel_->setObjectName(QStringLiteral("loginErrorLabel"));
    errorLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();

    loginButton_ = new QPushButton(QStringLiteral("登录"), card);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginButton_->setMinimumHeight(44);
    loginButton_->setDefault(true);
    loginButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1677ff; color: white; border: none; "
        "border-radius: 8px; font-weight: 600; } "
        "QPushButton:disabled { background: #9abff3; }"));

    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(phoneLabel);
    cardLayout->addWidget(phoneInput_);
    cardLayout->addWidget(errorLabel_);
    cardLayout->addWidget(loginButton_);

    pageLayout->addWidget(card, 0, Qt::AlignHCenter);
    pageLayout->addStretch();

    connect(loginButton_, &QPushButton::clicked, this, &LoginPage::submit);
    connect(phoneInput_, &QLineEdit::returnPressed, this, &LoginPage::submit);
}

QString LoginPage::phone() const
{
    return phoneInput_->text().trimmed();
}

void LoginPage::setLoading(bool loading)
{
    phoneInput_->setDisabled(loading);
    loginButton_->setDisabled(loading);
    loginButton_->setText(loading ? QStringLiteral("登录中…") : QStringLiteral("登录"));
}

void LoginPage::setErrorMessage(const QString &message)
{
    errorLabel_->setText(message);
    errorLabel_->setVisible(!message.isEmpty());
}

void LoginPage::clearErrorMessage()
{
    setErrorMessage({});
}

void LoginPage::submit()
{
    clearErrorMessage();
    emit loginRequested(phone());
}

}  // namespace charging::client
