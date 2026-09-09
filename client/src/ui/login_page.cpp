// 文件用途：手机号加验证码的登录页界面
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

// 构造函数按手机或车载模式搭建可滚动的登录布局
LoginPage::LoginPage(QWidget *parent, LayoutMode layoutMode)
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
    const bool vehicle = layoutMode == LayoutMode::Vehicle;
    centerLayout->setContentsMargins(vehicle ? 56 : 24, vehicle ? 36 : 24,
                                     vehicle ? 56 : 24, vehicle ? 36 : 24);
    centerLayout->setSpacing(vehicle ? 56 : 0);
    QWidget *column = nullptr;
    QVBoxLayout *contentLayout = nullptr;
    if (!vehicle) {
        column = new QWidget(content);
        column->setMaximumWidth(440);
        contentLayout = new QVBoxLayout(column);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        centerLayout->addStretch();
        centerLayout->addWidget(column, 1);
        centerLayout->addStretch();
        contentLayout->setSpacing(18);
        contentLayout->addStretch();
    }
    // 品牌标语与装饰插图区
    auto *intro = new QWidget(content);
    intro->setObjectName(QStringLiteral("loginIntro"));
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
    if (!vehicle) contentLayout->addWidget(intro);

    // 登录卡片容器，承载标题与输入项
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setMaximumWidth(vehicle ? 500 : 440);
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

    // 手机号输入限制为最多11位数字
    auto *phoneLabel = new QLabel(QStringLiteral("手机号"), card);
    phoneInput_ = new QLineEdit(card);
    phoneInput_->setObjectName(QStringLiteral("phoneInput"));
    phoneInput_->setPlaceholderText(QStringLiteral("请输入11位手机号"));
    phoneInput_->setMaxLength(11);
    phoneInput_->setClearButtonEnabled(true);
    phoneInput_->setMinimumHeight(vehicle ? 52 : 42);
    phoneInput_->setAccessibleName(QStringLiteral("11位手机号"));
    phoneLabel->setBuddy(phoneInput_);
    phoneInput_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\d{0,11}")), phoneInput_));

    // 验证码输入限制为最多6位数字
    auto *codeLabel = new QLabel(QStringLiteral("验证码"), card);
    verificationCodeInput_ = new QLineEdit(card);
    verificationCodeInput_->setObjectName(QStringLiteral("verificationCodeInput"));
    verificationCodeInput_->setPlaceholderText(QStringLiteral("6位验证码"));
    verificationCodeInput_->setMaxLength(6);
    verificationCodeInput_->setMinimumHeight(vehicle ? 52 : 42);
    verificationCodeInput_->setAccessibleName(QStringLiteral("6位验证码"));
    verificationCodeInput_->setInputMethodHints(Qt::ImhDigitsOnly);
    verificationCodeInput_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,6}")), verificationCodeInput_));
    codeLabel->setBuddy(verificationCodeInput_);

    // 发送验证码为占位功能，只提示演示码
    sendCodeButton_ = new QPushButton(QStringLiteral("发送验证码"), card);
    sendCodeButton_->setObjectName(QStringLiteral("sendVerificationCodeButton"));
    sendCodeButton_->setMinimumHeight(vehicle ? 52 : 42);
    sendCodeButton_->setToolTip(QStringLiteral("占位功能：仅展示演示验证码，不发送短信"));
    auto *codeLayout = new QHBoxLayout;
    codeLayout->setSpacing(8);
    codeLayout->addWidget(verificationCodeInput_, 1);
    codeLayout->addWidget(sendCodeButton_);

    codeHintLabel_ = new QLabel(QStringLiteral("演示模式，暂不发送短信"), card);
    codeHintLabel_->setObjectName(QStringLiteral("verificationCodeHint"));
    codeHintLabel_->setWordWrap(true);
    codeHintLabel_->setStyleSheet(QStringLiteral("color: #697969;"));

    // 错误提示标签与登录主按钮
    errorLabel_ = new QLabel(card);
    errorLabel_->setObjectName(QStringLiteral("loginErrorLabel"));
    errorLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();

    loginButton_ = new QPushButton(QStringLiteral("登录"), card);
    loginButton_->setObjectName(QStringLiteral("loginButton"));
    loginButton_->setMinimumHeight(vehicle ? 56 : 44);
    // Qt style-sheet contents/border accounting can subtract a few logical
    // pixels from QWidget's fixed height. Keep the rendered hit area >= 56 px.
    if (vehicle) loginButton_->setFixedHeight(64);
    loginButton_->setDefault(true);

    // 按顺序把各控件装入卡片布局
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

    auto *footer = new QLabel(QStringLiteral("发现好站  ·  轻松补能  ·  自在出发"), content);
    footer->setAlignment(Qt::AlignCenter);
    footer->setProperty("role", "eyebrow");
    if (vehicle) {
        intro->setMaximumWidth(560);
        headline->setStyleSheet(QStringLiteral(
            "font-size: 42px; font-weight: 700; color: #203d33;"));
        auto *formColumn = new QWidget(content);
        formColumn->setMaximumWidth(500);
        auto *formLayout = new QVBoxLayout(formColumn);
        formLayout->setContentsMargins(0, 0, 0, 0);
        formLayout->setSpacing(18);
        formLayout->addStretch();
        formLayout->addWidget(card);
        formLayout->addWidget(footer);
        formLayout->addStretch();
        centerLayout->addWidget(intro, 11, Qt::AlignVCenter);
        centerLayout->addWidget(formColumn, 9);
    } else {
        contentLayout->addWidget(card);
        contentLayout->addWidget(footer);
        contentLayout->addStretch();
    }
    scroll->setWidget(content);
    pageLayout->addWidget(scroll);

    // 回车与按钮都触发提交，并设定 Tab 顺序
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

// 展示演示验证码并把焦点移到输入框
void LoginPage::showDemoVerificationCode(const QString &code)
{
    codeHintLabel_->setText(QStringLiteral("演示验证码：%1（暂不发送短信）").arg(code));
    verificationCodeInput_->setFocus();
}

void LoginPage::clearVerificationCode()
{
    verificationCodeInput_->clear();
}

// 加载中禁用输入并改按钮文案
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

// 提交前清错误提示，再发出登录信号
void LoginPage::submit()
{
    clearErrorMessage();
    if (loginButton_->isEnabled()) {
        emit loginRequested(phone(), verificationCodeInput_->text());
    }
}

}  // namespace charging::client
