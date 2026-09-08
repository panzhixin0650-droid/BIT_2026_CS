#include "ui/login_page.h"
#include "ui/charging_art.h"
#include "ui/decorative_heading.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSizePolicy>
#include <QScrollArea>
#include <QVBoxLayout>

namespace charging::client {

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("loginPage"));

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("loginScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *centerLayout = new QHBoxLayout(content);
    centerLayout->setContentsMargins(24, 24, 24, 24);
    auto *column = new QWidget(content);
    column->setMaximumWidth(440);
    auto *contentLayout = new QVBoxLayout(column);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addStretch();
    centerLayout->addWidget(column, 1);
    centerLayout->addStretch();
    contentLayout->setSpacing(18);
    contentLayout->addStretch();
    auto *intro = new QWidget(content);
    intro->setMaximumWidth(440);
    auto *introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(0, 0, 0, 0);
    introLayout->setSpacing(16);
    auto *wordmark = new QLabel(QStringLiteral("BIT  /  CHARGE     悦充"), intro);
    wordmark->setStyleSheet(QStringLiteral("color: #245c45; font-size: 13px; font-weight: 700;"));
    auto *headline = new QLabel(QStringLiteral("每一程，\n都满电出发。"), intro);
    headline->setStyleSheet(QStringLiteral("font-size: 32px; font-weight: 700; color: #203d33;"));
    introLayout->addWidget(wordmark);
    introLayout->addWidget(new DecorativeHeading(headline, QStringLiteral("clover"),
                                                104, -8, intro));
    introLayout->addWidget(new ChargingArt(ChargingArt::Scene::Welcome, intro));
    contentLayout->addWidget(intro);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setMaximumWidth(440);
    card->setProperty("role", "card");
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(22, 22, 22, 22);
    cardLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("欢迎来到悦充"), card);
    title->setObjectName(QStringLiteral("loginTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *brandBadge = new QLabel(QStringLiteral("EV CHARGE · DEMO"), card);
    brandBadge->setObjectName(QStringLiteral("loginBrandBadge"));
    brandBadge->setAlignment(Qt::AlignCenter);
    brandBadge->setStyleSheet(QStringLiteral(
        "padding: 5px 10px; color: #245c45; background: #edf4e4; "
        "border-radius: 10px; font-size: 12px; font-weight: 600;"));
    brandBadge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    auto *subtitle = new QLabel(QStringLiteral("手机号 + 验证码登录，首次登录自动注册"), card);
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    subtitle->setStyleSheet(QStringLiteral("color: #697969;"));
    subtitle->setWordWrap(true);

    auto *phoneLabel = new QLabel(QStringLiteral("手机号"), card);
    phoneInput_ = new QLineEdit(card);
    phoneInput_->setObjectName(QStringLiteral("phoneInput"));
    phoneInput_->setPlaceholderText(QStringLiteral("请输入11位手机号"));
    phoneInput_->setMaxLength(11);
    phoneInput_->setClearButtonEnabled(true);
    phoneInput_->setMinimumHeight(42);
    phoneInput_->setAccessibleName(QStringLiteral("11位手机号"));
    phoneLabel->setBuddy(phoneInput_);
    phoneInput_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{0,11}")), phoneInput_));

    auto *codeLabel = new QLabel(QStringLiteral("验证码"), card);
    verificationCodeInput_ = new QLineEdit(card);
    verificationCodeInput_->setObjectName(QStringLiteral("verificationCodeInput"));
    verificationCodeInput_->setPlaceholderText(QStringLiteral("6位验证码"));
    verificationCodeInput_->setMaxLength(6);
    verificationCodeInput_->setMinimumHeight(42);
    verificationCodeInput_->setAccessibleName(QStringLiteral("6位验证码"));
    verificationCodeInput_->setInputMethodHints(Qt::ImhDigitsOnly);
    verificationCodeInput_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,6}")), verificationCodeInput_));
    codeLabel->setBuddy(verificationCodeInput_);

    sendCodeButton_ = new QPushButton(QStringLiteral("发送验证码"), card);
    sendCodeButton_->setObjectName(QStringLiteral("sendVerificationCodeButton"));
    sendCodeButton_->setMinimumHeight(42);
    sendCodeButton_->setToolTip(QStringLiteral("占位功能：仅展示演示验证码，不发送短信"));
    auto *codeLayout = new QHBoxLayout;
    codeLayout->setSpacing(8);
    codeLayout->addWidget(verificationCodeInput_, 1);
    codeLayout->addWidget(sendCodeButton_);

    codeHintLabel_ = new QLabel(QStringLiteral("演示模式，暂不发送短信"), card);
    codeHintLabel_->setObjectName(QStringLiteral("verificationCodeHint"));
    codeHintLabel_->setWordWrap(true);
    codeHintLabel_->setStyleSheet(QStringLiteral("color: #697969;"));

    errorLabel_ = new QLabel(card);
    errorLabel_->setObjectName(QStringLiteral("loginErrorLabel"));
    errorLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();

    loginButton_ = new QPushButton(QStringLiteral("登录"), card);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginButton_->setMinimumHeight(44);
    loginButton_->setDefault(true);

    cardLayout->addWidget(brandBadge, 0, Qt::AlignLeft);
    cardLayout->addWidget(title);
    cardLayout->addWidget(subtitle);
    cardLayout->addWidget(phoneLabel);
    cardLayout->addWidget(phoneInput_);
    cardLayout->addWidget(codeLabel);
    cardLayout->addLayout(codeLayout);
    cardLayout->addWidget(codeHintLabel_);
    cardLayout->addWidget(errorLabel_);
    cardLayout->addWidget(loginButton_);

    contentLayout->addWidget(card);
    auto *footer = new QLabel(QStringLiteral("发现好站  ·  轻松补能  ·  自在出发"), content);
    footer->setAlignment(Qt::AlignCenter);
    footer->setProperty("role", "eyebrow");
    contentLayout->addWidget(footer);
    contentLayout->addStretch();
    scroll->setWidget(content);
    pageLayout->addWidget(scroll);

    connect(loginButton_, &QPushButton::clicked, this, &LoginPage::submit);
    connect(phoneInput_, &QLineEdit::returnPressed, this, &LoginPage::submit);
    connect(verificationCodeInput_, &QLineEdit::returnPressed, this, &LoginPage::submit);
    connect(sendCodeButton_, &QPushButton::clicked, this, [this]() {
        emit verificationCodeRequested(phone());
    });
    setTabOrder(phoneInput_, verificationCodeInput_);
    setTabOrder(verificationCodeInput_, sendCodeButton_);
    setTabOrder(sendCodeButton_, loginButton_);
}

QString LoginPage::phone() const
{
    return phoneInput_->text().trimmed();
}

void LoginPage::showDemoVerificationCode(const QString &code)
{
    codeHintLabel_->setText(QStringLiteral("演示验证码：%1（暂不发送短信）").arg(code));
    verificationCodeInput_->setFocus();
}

void LoginPage::clearVerificationCode()
{
    verificationCodeInput_->clear();
}

void LoginPage::setLoading(bool loading)
{
    phoneInput_->setDisabled(loading);
    verificationCodeInput_->setDisabled(loading);
    sendCodeButton_->setDisabled(loading);
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
    if (loginButton_->isEnabled()) {
        emit loginRequested(phone(), verificationCodeInput_->text());
    }
}

}  // namespace charging::client
