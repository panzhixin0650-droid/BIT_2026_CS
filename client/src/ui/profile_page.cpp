#include "ui/profile_page.h"
#include "ui/avatar_art.h"
#include "ui/client_theme.h"
#include "ui/decorative_heading.h"

#include <QDoubleValidator>
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

// 个人中心页面：展示资料、钱包充值入口与头像详情
namespace charging::client {

namespace {

// 身份卡按钮：让整块卡片可点击且按宽度算高度
class IdentityButton final : public QPushButton {
public:
    explicit IdentityButton(QWidget *parent) : QPushButton(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }
    QSize sizeHint() const override
    {
        return layout() ? layout()->sizeHint() : QPushButton::sizeHint();
    }
    QSize minimumSizeHint() const override
    {
        return layout() ? layout()->minimumSize() : QPushButton::minimumSizeHint();
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        return layout() ? layout()->totalHeightForWidth(width) : -1;
    }
};

QFrame *createCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setProperty("role", "card");
    return card;
}

// 生成一行服务入口：图标加文字加右箭头
QPushButton *createServiceRow(const QString &name, const QString &text,
                             NavigationIcon icon, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(name);
    button->setProperty("role", "profileService");
    button->setAccessibleName(text);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(52);
    auto *row = new QHBoxLayout(button);
    row->setContentsMargins(12, 0, 12, 0);
    row->setSpacing(12);
    auto *symbol = new QLabel(button);
    symbol->setPixmap(clientNavigationIcon(icon).pixmap(QSize(22, 22)));
    symbol->setFixedSize(22, 22);
    auto *label = new QLabel(text, button);
    auto *arrow = new QLabel(button);
    arrow->setPixmap(clientNavigationIcon(NavigationIcon::ChevronRight).pixmap(QSize(16, 16)));
    arrow->setFixedSize(16, 16);
    for (auto *child : {symbol, label, arrow})
        child->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(symbol);
    row->addWidget(label, 1);
    row->addWidget(arrow);
    return button;
}

}  // namespace

// 构造页面：用堆叠布局装概览、详情、头像三个子页
ProfilePage::ProfilePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("profilePage"));

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    sections_ = new QStackedWidget(this);
    sections_->setObjectName("profilePages");
    pageLayout->addWidget(sections_);
    auto *scrollArea = new QScrollArea(sections_);
    sections_->addWidget(scrollArea);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(20, 24, 20, 24);
    contentLayout->setSpacing(14);

    auto *heading = new QLabel(QStringLiteral("我的"), content);
    heading->setObjectName(QStringLiteral("profileHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(24);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    // 顶部身份卡，点击进入详细信息页
    auto *identityCard = new IdentityButton(content);
    identityCard->setObjectName("profileDetailsButton");
    identityCard->setAccessibleName(QStringLiteral("查看个人详细信息"));
    auto *identityLayout = new QHBoxLayout(identityCard);
    identityLayout->setContentsMargins(18, 18, 18, 18);
    identityLayout->setSpacing(16);

    auto *avatarLayout = new QVBoxLayout();
    avatarLabel_ = new QLabel(QStringLiteral("用户"), identityCard);
    avatarLabel_->setObjectName(QStringLiteral("profileAvatar"));
    avatarLabel_->setFixedSize(64, 64);
    avatarLabel_->setAlignment(Qt::AlignCenter);
    avatarLabel_->setStyleSheet(QStringLiteral(
        "background: #acb8a6; color: white; border-radius: 32px; font-weight: 600;"));
    avatarLayout->addWidget(avatarLabel_, 0, Qt::AlignHCenter);

    auto *identityTextLayout = new QVBoxLayout();
    nicknameLabel_ = new QLabel(QStringLiteral("未登录"), identityCard);
    nicknameLabel_->setObjectName(QStringLiteral("profileNicknameLabel"));
    QFont nicknameFont = nicknameLabel_->font();
    nicknameFont.setPointSize(15);
    nicknameFont.setBold(true);
    nicknameLabel_->setFont(nicknameFont);
    nicknameLabel_->setWordWrap(true);
    phoneLabel_ = new QLabel(QStringLiteral("手机号：--"), identityCard);
    phoneLabel_->setObjectName(QStringLiteral("profilePhoneLabel"));
    phoneLabel_->setStyleSheet(QStringLiteral("color: #697969;"));
    phoneLabel_->setWordWrap(true);
    identityTextLayout->addWidget(nicknameLabel_);
    identityTextLayout->addWidget(phoneLabel_);

    refreshButton_ = new QPushButton(QStringLiteral("刷新"), identityCard);
    refreshButton_->setObjectName(QStringLiteral("profileRefreshButton"));
    identityLayout->addLayout(avatarLayout);
    identityLayout->addLayout(identityTextLayout, 1);
    refreshButton_->hide();
    auto *detailsHint = new QLabel(QStringLiteral("详细信息  ›"), identityCard);
    identityTextLayout->addWidget(detailsHint);
    for (auto *label : identityCard->findChildren<QLabel *>()) {
        label->setTextFormat(Qt::PlainText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    connect(identityCard, &QPushButton::clicked, this, &ProfilePage::openDetails);

    // 服务列表：订单、报修、工单，行间加分隔线
    auto *servicesCard = createCard(content);
    servicesCard->setObjectName(QStringLiteral("profileServicesCard"));
    servicesCard->setStyleSheet(profileServicesStyleSheet());
    auto *servicesLayout = new QVBoxLayout(servicesCard);
    servicesLayout->setContentsMargins(4, 4, 4, 4);
    servicesLayout->setSpacing(0);
    auto *ordersButton = createServiceRow(QStringLiteral("profileOrdersButton"),
        QStringLiteral("我的订单"), NavigationIcon::Orders, servicesCard);
    auto *repairButton = createServiceRow(QStringLiteral("profileRepairButton"),
        QStringLiteral("故障报修"), NavigationIcon::Repair, servicesCard);
    auto *ticketsButton = createServiceRow(QStringLiteral("profileTicketsButton"),
        QStringLiteral("我的工单"), NavigationIcon::Tickets, servicesCard);
    for (auto *button : {ordersButton, repairButton, ticketsButton}) {
        if (servicesLayout->count() > 0) {
            auto *divider = new QFrame(servicesCard);
            divider->setProperty("role", "profileDivider");
            divider->setFixedHeight(1);
            auto *dividerRow = new QHBoxLayout();
            dividerRow->setContentsMargins(46, 0, 12, 0);
            dividerRow->addWidget(divider);
            servicesLayout->addLayout(dividerRow);
        }
        servicesLayout->addWidget(button);
    }
    connect(ordersButton, &QPushButton::clicked, this, &ProfilePage::ordersRequested);
    connect(repairButton, &QPushButton::clicked, this, &ProfilePage::repairRequested);
    connect(ticketsButton, &QPushButton::clicked, this, &ProfilePage::ticketsRequested);

    // 钱包卡片：余额展示加快捷金额与充值输入
    auto *walletCard = createCard(content);
    walletCard->setObjectName(QStringLiteral("profileWalletCard"));
    walletCard->setStyleSheet(profileWalletStyleSheet());
    auto *walletLayout = new QVBoxLayout(walletCard);
    walletLayout->setContentsMargins(18, 18, 18, 18);
    walletLayout->setSpacing(12);
    auto *walletHeading = new QHBoxLayout();
    auto *walletTitle = new QLabel(QStringLiteral("钱包余额"), walletCard);
    walletTitle->setProperty("role", "profileSection");
    auto *walletIcon = new QLabel(walletCard);
    walletIcon->setFixedSize(24, 24);
    walletIcon->setPixmap(clientNavigationIcon(NavigationIcon::Wallet).pixmap(QSize(24, 24)));
    walletHeading->addWidget(walletTitle, 1);
    walletHeading->addWidget(walletIcon);
    balanceLabel_ = new QLabel(QStringLiteral("¥0.00"), walletCard);
    balanceLabel_->setObjectName(QStringLiteral("profileBalanceLabel"));
    QFont balanceFont = balanceLabel_->font();
    balanceFont.setPointSize(30);
    balanceFont.setBold(true);
    balanceLabel_->setFont(balanceFont);

    auto *quickAmounts = new QHBoxLayout();
    quickAmounts->setSpacing(8);
    auto *amountGroup = new QButtonGroup(walletCard);
    for (const QString &amount : {QStringLiteral("10"),
                                  QStringLiteral("20"),
                                  QStringLiteral("50"),
                                  QStringLiteral("100")}) {
        auto *button = new QPushButton(QStringLiteral("%1元").arg(amount), walletCard);
        button->setProperty("rechargeAmount", amount);
        button->setFixedHeight(32);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCheckable(true);
        amountGroup->addButton(button);
        connect(button, &QPushButton::clicked, this, [this, amount]() {
            rechargeInput_->setText(amount);
        });
        quickAmounts->addWidget(button);
    }

    auto *rechargeLayout = new QHBoxLayout();
    rechargeLayout->setSpacing(10);
    rechargeInput_ = new QLineEdit(walletCard);
    rechargeInput_->setObjectName(QStringLiteral("rechargeAmountInput"));
    rechargeInput_->setMinimumWidth(0);
    rechargeInput_->setFixedHeight(40);
    rechargeInput_->setPlaceholderText(QStringLiteral("输入充值金额（元）"));
    // 限制充值输入为0.01到10000元、两位小数
    auto *rechargeValidator =
        new QDoubleValidator(0.01, 10000.0, 2, rechargeInput_);
    rechargeValidator->setNotation(QDoubleValidator::StandardNotation);
    rechargeInput_->setValidator(rechargeValidator);
    rechargeInput_->setAccessibleName(QStringLiteral("充值金额，单位元"));
    connect(rechargeInput_, &QLineEdit::textChanged, this, [amountGroup](const QString &text) {
        amountGroup->setExclusive(false);
        for (auto *button : amountGroup->buttons()) {
            button->setChecked(text.toDouble() == button->property("rechargeAmount").toDouble());
        }
        amountGroup->setExclusive(true);
    });
    rechargeButton_ = new QPushButton(QStringLiteral("充值"), walletCard);
    rechargeButton_->setObjectName(QStringLiteral("rechargeButton"));
    rechargeButton_->setFixedSize(72, 40);
    rechargeLayout->addWidget(rechargeInput_, 1);
    rechargeLayout->addWidget(rechargeButton_);

    walletLayout->addLayout(walletHeading);
    walletLayout->addWidget(balanceLabel_);
    walletLayout->addLayout(quickAmounts);
    walletLayout->addLayout(rechargeLayout);

    // 详细信息页：返回按钮、头像入口与昵称修改
    auto *detailPage = new QWidget(sections_);
    detailPage->setObjectName("profileDetailPage");
    sections_->addWidget(detailPage);
    auto *detailsLayout = new QVBoxLayout(detailPage);
    detailsLayout->setContentsMargins(20, 16, 20, 20);
    auto *detailsBack = new QPushButton(QStringLiteral("‹ 返回我的"), detailPage);
    detailsBack->setObjectName("profileDetailBack");
    detailsLayout->addWidget(detailsBack, 0, Qt::AlignLeft);
    connect(detailsBack, &QPushButton::clicked, this, &ProfilePage::showOverview);
    auto *detailsTitle = new QLabel(QStringLiteral("详细信息"), detailPage);
    detailsTitle->setProperty("role", "discoveryHeading");
    detailsLayout->addWidget(detailsTitle);
    avatarButton_ = new QPushButton(QStringLiteral("头像    查看与修改  ›"), detailPage);
    avatarButton_->setObjectName("profileAvatarButton");
    avatarButton_->setMinimumHeight(88);
    avatarButton_->setIconSize(QSize(64,64));
    detailsLayout->addWidget(avatarButton_);
    detailPhone_ = new QLabel(detailPage);
    detailPhone_->setObjectName("profileDetailPhone");
    detailsLayout->addWidget(detailPhone_);
    auto *profileCard = createCard(detailPage);
    auto *profileLayout = new QGridLayout(profileCard);
    profileLayout->setContentsMargins(18, 18, 18, 18);
    profileLayout->setHorizontalSpacing(10);
    profileLayout->setVerticalSpacing(12);
    auto *nicknameTitle = new QLabel(QStringLiteral("修改昵称"), profileCard);
    nicknameInput_ = new QLineEdit(profileCard);
    nicknameInput_->setObjectName(QStringLiteral("nicknameInput"));
    nicknameInput_->setAttribute(Qt::WA_InputMethodEnabled, true);
    nicknameInput_->setInputMethodHints(Qt::ImhNone);
    nicknameInput_->setMaxLength(32);
    saveNicknameButton_ = new QPushButton(QStringLiteral("保存"), profileCard);
    saveNicknameButton_->setObjectName(QStringLiteral("saveNicknameButton"));
    profileLayout->addWidget(nicknameTitle, 0, 0, 1, 2);
    profileLayout->addWidget(nicknameInput_, 1, 0);
    profileLayout->addWidget(saveNicknameButton_, 1, 1);

    messageLabel_ = new QLabel(content);
    messageLabel_->setObjectName(QStringLiteral("profileMessageLabel"));
    messageLabel_->setWordWrap(true);
    messageLabel_->hide();

    logoutButton_ = new QPushButton(QStringLiteral("退出登录"), content);
    logoutButton_->setObjectName(QStringLiteral("logoutButton"));

    auto *decoratedHeading = new DecorativeHeading(heading, QStringLiteral("plant"), 66, 4, content);
    decoratedHeading->setFixedHeight(66);
    contentLayout->addWidget(decoratedHeading);
    contentLayout->addWidget(identityCard);
    contentLayout->addWidget(servicesCard);
    contentLayout->addWidget(walletCard);
    detailsLayout->addWidget(profileCard);
    detailMessage_ = new QLabel(detailPage);
    detailMessage_->setObjectName("profileDetailMessage");
    detailMessage_->setWordWrap(true);
    detailsLayout->addWidget(detailMessage_);
    detailsLayout->addStretch();
    // 头像页：大图预览与从相册更换入口
    auto *avatarPage = new QWidget(sections_);
    avatarPage->setObjectName("profileAvatarPage");
    sections_->addWidget(avatarPage);
    auto *avatarPageLayout = new QVBoxLayout(avatarPage);
    auto *avatarBack = new QPushButton(QStringLiteral("‹ 返回详细信息"), avatarPage);
    avatarBack->setObjectName("profileAvatarBack");
    avatarPageLayout->addWidget(avatarBack, 0, Qt::AlignLeft);
    connect(avatarBack, &QPushButton::clicked, this, [this, detailPage] { sections_->setCurrentWidget(detailPage); });
    fullAvatar_ = new QLabel(avatarPage);
    fullAvatar_->setObjectName("profileFullAvatar");
    fullAvatar_->setAlignment(Qt::AlignCenter);
    fullAvatar_->setMinimumSize(0,0);
    fullAvatar_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    avatarPageLayout->addWidget(fullAvatar_, 1);
    avatarMessage_ = new QLabel(avatarPage);
    avatarMessage_->setWordWrap(true);
    avatarPageLayout->addWidget(avatarMessage_);
    auto *changeAvatarButton = new QPushButton(QStringLiteral("从相册更换头像"), avatarPage);
    changeAvatarButton->setObjectName("changeAvatarButton");
    changeAvatarButton->setProperty("role", "primary");
    avatarPageLayout->addWidget(changeAvatarButton);
    connect(avatarButton_, &QPushButton::clicked, this, [this, avatarPage] {
        sections_->setCurrentWidget(avatarPage);
        updateFullAvatar();
        QTimer::singleShot(0, this, &ProfilePage::updateFullAvatar);
    });
    contentLayout->addWidget(messageLabel_);
    contentLayout->addWidget(logoutButton_);
    contentLayout->addStretch();

    scrollArea->setWidget(content);
    setAvatarPath({});

    connect(refreshButton_, &QPushButton::clicked, this, &ProfilePage::refreshRequested);
    connect(changeAvatarButton, &QPushButton::clicked, this, [this]() {
        emit avatarSelectionRequested();
    });
    connect(saveNicknameButton_, &QPushButton::clicked, this, [this]() {
        emit nicknameUpdateRequested(nicknameInput_->text());
    });
    connect(rechargeButton_, &QPushButton::clicked, this, [this]() {
        emit rechargeRequested(rechargeInput_->text());
    });
    connect(logoutButton_, &QPushButton::clicked, this, &ProfilePage::logoutRequested);
}

// 填充用户资料并同步余额显示
void ProfilePage::setUser(const protocol::UserDto &user)
{
    savedNickname_ = user.nickname;
    nicknameLabel_->setText(user.nickname);
    detailPhone_->setText(QStringLiteral("手机号：%1").arg(user.phone));
    phoneLabel_->setText(QStringLiteral("手机号：%1").arg(user.phone));
    nicknameInput_->setText(user.nickname);
    setBalance(user.balanceCents);
}

void ProfilePage::setBalance(qint64 balanceCents)
{
    balanceLabel_->setText(formatBalance(balanceCents));
}

// 重新读取头像文件，缺失时用默认头像
void ProfilePage::setAvatarPath(const QString &path)
{
    // Read the file afresh: each user replaces the same PNG on subsequent saves.
    avatarImage_ = path.isEmpty() ? QImage() : QImage(path);
    if (avatarImage_.isNull()) {
        avatarImage_ = defaultAvatar();
    }
    avatarLabel_->setText({});
    avatarLabel_->setPixmap(circularAvatar(avatarImage_, avatarLabel_->width(), devicePixelRatioF()));
    avatarButton_->setIcon(QIcon(QPixmap::fromImage(avatarImage_)));
    updateFullAvatar();
}

// 请求中禁用按钮和输入，防止重复提交
void ProfilePage::setBusy(bool busy)
{
    refreshButton_->setDisabled(busy);
    saveNicknameButton_->setDisabled(busy);
    rechargeButton_->setDisabled(busy);
    logoutButton_->setDisabled(busy);
    nicknameInput_->setDisabled(busy);
    rechargeInput_->setDisabled(busy);
}

// 三处提示标签统一显示消息，错误用红色
void ProfilePage::showMessage(const QString &message, bool error)
{
    for (auto *label : {detailMessage_, avatarMessage_}) {
        label->setText(message);
        label->setStyleSheet(error ? QStringLiteral("color: #c62828;") : QStringLiteral("color: #386a3c;"));
        label->setVisible(!message.isEmpty());
    }
    messageLabel_->setText(message);
    messageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                       : QStringLiteral("color: #386a3c;"));
    messageLabel_->setVisible(!message.isEmpty());
}

void ProfilePage::openDetails()
{
    nicknameInput_->setText(savedNickname_);
    sections_->setCurrentIndex(1);
}

void ProfilePage::showOverview()
{
    sections_->setCurrentIndex(0);
    nicknameInput_->setText(savedNickname_);
}

// 按当前控件尺寸等比缩放头像大图
void ProfilePage::updateFullAvatar()
{
    const QPixmap source = QPixmap::fromImage(avatarImage_);
    if (source.isNull()) {
        fullAvatar_->setPixmap({});
        fullAvatar_->setText(QStringLiteral("还没有设置头像"));
    } else {
        fullAvatar_->setText({});
        fullAvatar_->setPixmap(source.scaled(fullAvatar_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void ProfilePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &ProfilePage::updateFullAvatar);
}

// 整数分转成带两位小数的金额文本
QString ProfilePage::formatBalance(qint64 balanceCents) const
{
    return QStringLiteral("¥%1.%2")
        .arg(balanceCents / 100)
        .arg(balanceCents % 100, 2, 10, QChar('0'));
}

}  // namespace charging::client
