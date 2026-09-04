#include "ui/profile_page.h"

#include <QDoubleValidator>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

namespace charging::client {

namespace {

QFrame *createCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(QStringLiteral(
        "QFrame { background: white; border: 1px solid #e4e7ec; "
        "border-radius: 12px; } QLabel { border: none; }"));
    return card;
}

}  // namespace

ProfilePage::ProfilePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("profilePage"));

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(20, 24, 20, 24);
    contentLayout->setSpacing(14);

    auto *heading = new QLabel(QStringLiteral("我的"), content);
    QFont headingFont = heading->font();
    headingFont.setPointSize(20);
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto *identityCard = createCard(content);
    auto *identityLayout = new QHBoxLayout(identityCard);
    identityLayout->setContentsMargins(18, 18, 18, 18);
    identityLayout->setSpacing(16);

    auto *avatarLayout = new QVBoxLayout();
    avatarLabel_ = new QLabel(QStringLiteral("用户"), identityCard);
    avatarLabel_->setObjectName(QStringLiteral("profileAvatar"));
    avatarLabel_->setFixedSize(64, 64);
    avatarLabel_->setAlignment(Qt::AlignCenter);
    avatarLabel_->setStyleSheet(QStringLiteral(
        "background: #d0d5dd; color: white; border-radius: 32px; font-weight: 600;"));
    auto *changeAvatarButton = new QPushButton(QStringLiteral("更换头像"), identityCard);
    changeAvatarButton->setObjectName(QStringLiteral("changeAvatarButton"));
    changeAvatarButton->setFlat(true);
    avatarLayout->addWidget(avatarLabel_, 0, Qt::AlignHCenter);
    avatarLayout->addWidget(changeAvatarButton, 0, Qt::AlignHCenter);

    auto *identityTextLayout = new QVBoxLayout();
    nicknameLabel_ = new QLabel(QStringLiteral("未登录"), identityCard);
    nicknameLabel_->setObjectName(QStringLiteral("profileNicknameLabel"));
    QFont nicknameFont = nicknameLabel_->font();
    nicknameFont.setPointSize(15);
    nicknameFont.setBold(true);
    nicknameLabel_->setFont(nicknameFont);
    phoneLabel_ = new QLabel(QStringLiteral("手机号：--"), identityCard);
    phoneLabel_->setObjectName(QStringLiteral("profilePhoneLabel"));
    phoneLabel_->setStyleSheet(QStringLiteral("color: #667085;"));
    identityTextLayout->addWidget(nicknameLabel_);
    identityTextLayout->addWidget(phoneLabel_);

    refreshButton_ = new QPushButton(QStringLiteral("刷新"), identityCard);
    refreshButton_->setObjectName(QStringLiteral("profileRefreshButton"));
    identityLayout->addLayout(avatarLayout);
    identityLayout->addLayout(identityTextLayout, 1);
    identityLayout->addWidget(refreshButton_, 0, Qt::AlignTop);

    auto *walletCard = createCard(content);
    auto *walletLayout = new QVBoxLayout(walletCard);
    walletLayout->setContentsMargins(18, 18, 18, 18);
    walletLayout->setSpacing(12);
    auto *walletTitle = new QLabel(QStringLiteral("钱包余额"), walletCard);
    balanceLabel_ = new QLabel(QStringLiteral("¥0.00"), walletCard);
    balanceLabel_->setObjectName(QStringLiteral("profileBalanceLabel"));
    QFont balanceFont = balanceLabel_->font();
    balanceFont.setPointSize(22);
    balanceFont.setBold(true);
    balanceLabel_->setFont(balanceFont);

    auto *quickAmounts = new QHBoxLayout();
    for (const QString &amount : {QStringLiteral("10"),
                                  QStringLiteral("20"),
                                  QStringLiteral("50"),
                                  QStringLiteral("100")}) {
        auto *button = new QPushButton(QStringLiteral("%1元").arg(amount), walletCard);
        button->setProperty("rechargeAmount", amount);
        connect(button, &QPushButton::clicked, this, [this, amount]() {
            rechargeInput_->setText(amount);
        });
        quickAmounts->addWidget(button);
    }

    auto *rechargeLayout = new QHBoxLayout();
    rechargeInput_ = new QLineEdit(walletCard);
    rechargeInput_->setObjectName(QStringLiteral("rechargeAmountInput"));
    rechargeInput_->setPlaceholderText(QStringLiteral("输入充值金额（元）"));
    rechargeInput_->setValidator(new QDoubleValidator(0.01, 10000.0, 2, rechargeInput_));
    rechargeButton_ = new QPushButton(QStringLiteral("充值"), walletCard);
    rechargeButton_->setObjectName(QStringLiteral("rechargeButton"));
    rechargeLayout->addWidget(rechargeInput_, 1);
    rechargeLayout->addWidget(rechargeButton_);

    walletLayout->addWidget(walletTitle);
    walletLayout->addWidget(balanceLabel_);
    walletLayout->addLayout(quickAmounts);
    walletLayout->addLayout(rechargeLayout);

    auto *profileCard = createCard(content);
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

    contentLayout->addWidget(heading);
    contentLayout->addWidget(identityCard);
    contentLayout->addWidget(walletCard);
    contentLayout->addWidget(profileCard);
    contentLayout->addWidget(messageLabel_);
    contentLayout->addWidget(logoutButton_);
    contentLayout->addStretch();

    scrollArea->setWidget(content);
    pageLayout->addWidget(scrollArea);

    connect(refreshButton_, &QPushButton::clicked, this, &ProfilePage::refreshRequested);
    connect(changeAvatarButton, &QPushButton::clicked, this, [this]() {
        const QString sourcePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择头像"),
            {},
            QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (!sourcePath.isEmpty()) {
            emit avatarSelected(sourcePath);
        }
    });
    connect(saveNicknameButton_, &QPushButton::clicked, this, [this]() {
        emit nicknameUpdateRequested(nicknameInput_->text());
    });
    connect(rechargeButton_, &QPushButton::clicked, this, [this]() {
        emit rechargeRequested(rechargeInput_->text());
    });
    connect(logoutButton_, &QPushButton::clicked, this, &ProfilePage::logoutRequested);
}

void ProfilePage::setUser(const protocol::UserDto &user)
{
    nicknameLabel_->setText(user.nickname);
    phoneLabel_->setText(QStringLiteral("手机号：%1").arg(user.phone));
    nicknameInput_->setText(user.nickname);
    setBalance(user.balanceCents);
}

void ProfilePage::setBalance(qint64 balanceCents)
{
    balanceLabel_->setText(formatBalance(balanceCents));
}

void ProfilePage::setAvatarPath(const QString &path)
{
    QPixmap source(path);
    if (source.isNull()) {
        avatarLabel_->setPixmap({});
        avatarLabel_->setText(QStringLiteral("用户"));
        return;
    }

    const QSize targetSize = avatarLabel_->size();
    const QPixmap scaled = source.scaled(targetSize,
                                         Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
    QPixmap circular(targetSize);
    circular.fill(Qt::transparent);
    QPainter painter(&circular);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clipPath;
    clipPath.addEllipse(circular.rect());
    painter.setClipPath(clipPath);
    const QPoint offset((scaled.width() - targetSize.width()) / 2,
                        (scaled.height() - targetSize.height()) / 2);
    painter.drawPixmap(-offset, scaled);
    avatarLabel_->setText({});
    avatarLabel_->setPixmap(circular);
}

void ProfilePage::setBusy(bool busy)
{
    refreshButton_->setDisabled(busy);
    saveNicknameButton_->setDisabled(busy);
    rechargeButton_->setDisabled(busy);
    logoutButton_->setDisabled(busy);
    nicknameInput_->setDisabled(busy);
    rechargeInput_->setDisabled(busy);
}

void ProfilePage::showMessage(const QString &message, bool error)
{
    messageLabel_->setText(message);
    messageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                       : QStringLiteral("color: #137333;"));
    messageLabel_->setVisible(!message.isEmpty());
}

QString ProfilePage::formatBalance(qint64 balanceCents) const
{
    return QStringLiteral("¥%1.%2")
        .arg(balanceCents / 100)
        .arg(balanceCents % 100, 2, 10, QChar('0'));
}

}  // namespace charging::client
