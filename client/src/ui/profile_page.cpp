#include "ui/profile_page.h"
#include "ui/client_theme.h"

#include <QDoubleValidator>
#include <QButtonGroup>
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
    card->setProperty("role", "card");
    return card;
}

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

ProfilePage::ProfilePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("profilePage"));
    setStyleSheet(profileThemeStyleSheet());

    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("profileScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *scrollContent = new QWidget(scrollArea);
    scrollContent->setObjectName(QStringLiteral("profileScrollContent"));
    auto *scrollLayout = new QHBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    auto *content = new QWidget(scrollContent);
    content->setObjectName(QStringLiteral("profileContent"));
    content->setMaximumWidth(640);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    scrollLayout->addStretch();
    scrollLayout->addWidget(content, 1);
    scrollLayout->addStretch();
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(22, 16, 22, 20);
    contentLayout->setSpacing(16);

    auto *headingLayout = new QHBoxLayout();
    auto *heading = new QLabel(QStringLiteral("我的"), content);
    heading->setObjectName(QStringLiteral("profileHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(24);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    refreshButton_ = new QPushButton(QStringLiteral("刷新"), content);
    refreshButton_->setObjectName(QStringLiteral("profileRefreshButton"));
    refreshButton_->setFlat(true);
    refreshButton_->setFixedHeight(32);
    refreshButton_->setToolTip(QStringLiteral("刷新个人资料和钱包余额"));
    headingLayout->addWidget(heading, 1);
    headingLayout->addWidget(refreshButton_);

    auto *identityCard = createCard(content);
    identityCard->setObjectName(QStringLiteral("profileIdentityCard"));
    auto *identityLayout = new QHBoxLayout(identityCard);
    identityLayout->setContentsMargins(16, 16, 16, 16);
    identityLayout->setSpacing(12);

    avatarLabel_ = new QLabel(QStringLiteral("用户"), identityCard);
    avatarLabel_->setObjectName(QStringLiteral("profileAvatar"));
    avatarLabel_->setFixedSize(68, 68);
    avatarLabel_->setAlignment(Qt::AlignCenter);
    auto *changeAvatarButton = new QPushButton(identityCard);
    changeAvatarButton->setObjectName(QStringLiteral("changeAvatarButton"));
    changeAvatarButton->setIcon(clientNavigationIcon(NavigationIcon::ChevronRight));
    changeAvatarButton->setIconSize(QSize(18, 18));
    changeAvatarButton->setFixedSize(32, 32);
    changeAvatarButton->setAccessibleName(QStringLiteral("更换头像"));
    changeAvatarButton->setToolTip(QStringLiteral("更换头像"));
    changeAvatarButton->setCursor(Qt::PointingHandCursor);
    changeAvatarButton->setFlat(true);

    auto *identityTextLayout = new QVBoxLayout();
    identityTextLayout->setSpacing(6);
    identityTextLayout->setAlignment(Qt::AlignVCenter);
    nicknameLabel_ = new QLabel(QStringLiteral("未登录"), identityCard);
    nicknameLabel_->setObjectName(QStringLiteral("profileNicknameLabel"));
    QFont nicknameFont = nicknameLabel_->font();
    nicknameFont.setPointSize(14);
    nicknameFont.setBold(true);
    nicknameLabel_->setFont(nicknameFont);
    nicknameLabel_->setWordWrap(true);
    phoneLabel_ = new QLabel(QStringLiteral("手机号：--"), identityCard);
    phoneLabel_->setObjectName(QStringLiteral("profilePhoneLabel"));
    phoneLabel_->setProperty("role", "profileSecondary");
    phoneLabel_->setWordWrap(true);
    identityTextLayout->addWidget(nicknameLabel_);
    identityTextLayout->addWidget(phoneLabel_);

    identityLayout->addWidget(avatarLabel_);
    identityLayout->addLayout(identityTextLayout, 1);
    identityLayout->addWidget(changeAvatarButton);

    auto *servicesCard = createCard(content);
    servicesCard->setObjectName(QStringLiteral("profileServicesCard"));
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

    auto *walletCard = createCard(content);
    walletCard->setObjectName(QStringLiteral("profileWalletCard"));
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

    auto *profileCard = new QWidget(content);
    profileCard->setObjectName(QStringLiteral("profileEditor"));
    auto *profileLayout = new QGridLayout(profileCard);
    profileLayout->setContentsMargins(0, 0, 0, 0);
    profileLayout->setHorizontalSpacing(10);
    profileLayout->setVerticalSpacing(10);
    auto *nicknameTitle = new QLabel(QStringLiteral("修改昵称"), profileCard);
    nicknameTitle->setProperty("role", "profileSection");
    nicknameInput_ = new QLineEdit(profileCard);
    nicknameInput_->setObjectName(QStringLiteral("nicknameInput"));
    nicknameInput_->setAttribute(Qt::WA_InputMethodEnabled, true);
    nicknameInput_->setInputMethodHints(Qt::ImhNone);
    nicknameInput_->setMaxLength(32);
    nicknameInput_->setMinimumWidth(0);
    nicknameInput_->setFixedHeight(40);
    saveNicknameButton_ = new QPushButton(QStringLiteral("保存"), profileCard);
    saveNicknameButton_->setObjectName(QStringLiteral("saveNicknameButton"));
    saveNicknameButton_->setFixedSize(72, 40);
    profileLayout->setColumnStretch(0, 1);
    profileLayout->addWidget(nicknameTitle, 0, 0, 1, 2);
    profileLayout->addWidget(nicknameInput_, 1, 0);
    profileLayout->addWidget(saveNicknameButton_, 1, 1);

    messageLabel_ = new QLabel(content);
    messageLabel_->setObjectName(QStringLiteral("profileMessageLabel"));
    messageLabel_->setWordWrap(true);
    messageLabel_->hide();

    logoutButton_ = new QPushButton(QStringLiteral("退出登录"), content);
    logoutButton_->setObjectName(QStringLiteral("logoutButton"));
    logoutButton_->setFlat(true);
    logoutButton_->setFixedHeight(36);
    auto *footerLayout = new QHBoxLayout();
    footerLayout->setSpacing(12);
    footerLayout->addWidget(messageLabel_, 1);
    footerLayout->addWidget(logoutButton_);

    contentLayout->addLayout(headingLayout);
    contentLayout->addWidget(identityCard);
    contentLayout->addWidget(servicesCard);
    contentLayout->addWidget(walletCard);
    contentLayout->addWidget(profileCard);
    contentLayout->addLayout(footerLayout);
    contentLayout->addStretch();

    scrollArea->setWidget(scrollContent);
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
                                       : QStringLiteral("color: #386a3c;"));
    messageLabel_->setVisible(!message.isEmpty());
}

QString ProfilePage::formatBalance(qint64 balanceCents) const
{
    return QStringLiteral("¥%1.%2")
        .arg(balanceCents / 100)
        .arg(balanceCents % 100, 2, 10, QChar('0'));
}

}  // namespace charging::client
