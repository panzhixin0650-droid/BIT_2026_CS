#include "ui/profile_controller.h"

#include "api/i_charging_api.h"
#include "charging/protocol/protocol_constants.h"
#include "local/avatar_storage.h"
#include "ui/profile_page.h"

#include <QRegularExpression>

namespace charging::client {

ProfileController::ProfileController(ProfilePage &page,
                                     IChargingApi &api,
                                     AvatarStorage &avatarStorage,
                                     QObject *parent)
    : QObject(parent)
    , page_(page)
    , api_(api)
    , avatarStorage_(avatarStorage)
{
    connect(&page_, &ProfilePage::refreshRequested, this, &ProfileController::refreshProfile);
    connect(&page_,
            &ProfilePage::nicknameUpdateRequested,
            this,
            &ProfileController::updateNickname);
    connect(&page_, &ProfilePage::rechargeRequested, this, &ProfileController::recharge);
    connect(&page_, &ProfilePage::avatarSelected, this, &ProfileController::saveAvatar);
    connect(&page_, &ProfilePage::logoutRequested, this, &ProfileController::logout);
    connect(&api_,
            &IChargingApi::profileCompleted,
            this,
            &ProfileController::handleProfileCompleted);
    connect(&api_,
            &IChargingApi::profileUpdateCompleted,
            this,
            &ProfileController::handleProfileUpdateCompleted);
    connect(&api_,
            &IChargingApi::rechargeCompleted,
            this,
            &ProfileController::handleRechargeCompleted);
    connect(&api_,
            &IChargingApi::logoutCompleted,
            this,
            &ProfileController::handleLogoutCompleted);
}

void ProfileController::setInitialUser(const protocol::UserDto &user)
{
    currentAvatarKey_ = QStringLiteral("%1:%2").arg(user.userId).arg(user.phone);
    page_.setUser(user);
    page_.setAvatarPath(avatarStorage_.avatarPath(currentAvatarKey_));
    page_.showMessage({});
}

void ProfileController::refreshProfile()
{
    if (pendingAction_ != PendingAction::None) {
        return;
    }

    page_.setBusy(true);
    page_.showMessage(QStringLiteral("正在刷新…"));
    pendingAction_ = PendingAction::Refresh;
    pendingRequestId_ = api_.getProfile();
}

void ProfileController::updateNickname(const QString &nickname)
{
    if (pendingAction_ != PendingAction::None) {
        return;
    }

    const QString normalizedNickname = nickname.trimmed();
    if (normalizedNickname.isEmpty() || normalizedNickname.size() > 32) {
        page_.showMessage(QStringLiteral("昵称长度必须为1到32个字符"), true);
        return;
    }

    page_.setBusy(true);
    page_.showMessage(QStringLiteral("正在保存…"));
    pendingAction_ = PendingAction::UpdateNickname;
    pendingRequestId_ = api_.updateNickname(normalizedNickname);
}

void ProfileController::recharge(const QString &amountYuan)
{
    if (pendingAction_ != PendingAction::None) {
        return;
    }

    const auto amountCents = parseAmountCents(amountYuan.trimmed());
    if (!amountCents.has_value()) {
        page_.showMessage(QStringLiteral("请输入0.01元到10000元之间的有效金额"), true);
        return;
    }

    page_.setBusy(true);
    page_.showMessage(QStringLiteral("正在充值…"));
    pendingAction_ = PendingAction::Recharge;
    pendingRequestId_ = api_.recharge(*amountCents);
}

void ProfileController::saveAvatar(const QString &sourcePath)
{
    if (currentAvatarKey_.isEmpty()) {
        page_.showMessage(QStringLiteral("请先登录后再修改头像"), true);
        return;
    }

    QString savedPath;
    QString error;
    if (!avatarStorage_.saveAvatar(currentAvatarKey_, sourcePath, &savedPath, &error)) {
        page_.showMessage(error, true);
        return;
    }

    page_.setAvatarPath(savedPath);
    page_.showMessage(QStringLiteral("头像已保存到本机"));
}

void ProfileController::logout()
{
    if (pendingAction_ != PendingAction::None) {
        return;
    }

    page_.setBusy(true);
    page_.showMessage(QStringLiteral("正在退出…"));
    pendingAction_ = PendingAction::Logout;
    pendingRequestId_ = api_.logout();
}

void ProfileController::handleProfileCompleted(const UserResult &result)
{
    if (!acceptResult(result.response,
                      PendingAction::Refresh,
                      protocol::MessageType::UserProfileGet)) {
        return;
    }

    finishRequest();
    if (!result.ok() || !result.payload.has_value()) {
        showFailure(result.response);
        return;
    }

    page_.setUser(result.payload->user);
    currentAvatarKey_ = QStringLiteral("%1:%2")
                            .arg(result.payload->user.userId)
                            .arg(result.payload->user.phone);
    page_.setAvatarPath(avatarStorage_.avatarPath(currentAvatarKey_));
    page_.showMessage(QStringLiteral("资料已刷新"));
    emit profileChanged(result.payload->user);
}

void ProfileController::handleProfileUpdateCompleted(const UserResult &result)
{
    if (!acceptResult(result.response,
                      PendingAction::UpdateNickname,
                      protocol::MessageType::UserProfileUpdate)) {
        return;
    }

    finishRequest();
    if (!result.ok() || !result.payload.has_value()) {
        showFailure(result.response);
        return;
    }

    page_.setUser(result.payload->user);
    page_.showMessage(QStringLiteral("昵称已更新"));
    emit profileChanged(result.payload->user);
}

void ProfileController::handleRechargeCompleted(const RechargeResult &result)
{
    if (!acceptResult(result.response,
                      PendingAction::Recharge,
                      protocol::MessageType::WalletRecharge)) {
        return;
    }

    finishRequest();
    if (!result.ok() || !result.payload.has_value()) {
        showFailure(result.response);
        return;
    }

    page_.setBalance(result.payload->balanceCents);
    page_.showMessage(QStringLiteral("充值成功，余额已刷新"));
}

void ProfileController::handleLogoutCompleted(const LogoutResult &result)
{
    if (!acceptResult(result.response,
                      PendingAction::Logout,
                      protocol::MessageType::AuthLogout)) {
        return;
    }

    finishRequest();
    if (!result.ok() || !result.payload.has_value() || !result.payload->success) {
        showFailure(result.response);
        return;
    }

    emit loggedOut();
}

bool ProfileController::acceptResult(const ApiResponse &response,
                                     PendingAction action,
                                     const char *type)
{
    return pendingAction_ == action && !pendingRequestId_.isEmpty()
        && response.requestId == pendingRequestId_
        && response.type == QString::fromLatin1(type);
}

std::optional<qint64> ProfileController::parseAmountCents(const QString &amountYuan) const
{
    static const QRegularExpression amountPattern(
        QStringLiteral("^(0|[1-9]\\d{0,4})(?:\\.(\\d{1,2}))?$"));
    const auto match = amountPattern.match(amountYuan);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    bool wholeOk = false;
    const qint64 wholeYuan = match.captured(1).toLongLong(&wholeOk);
    QString fractionalPart = match.captured(2);
    fractionalPart = fractionalPart.leftJustified(2, QChar('0'));
    const qint64 cents = fractionalPart.isEmpty() ? 0 : fractionalPart.toLongLong();
    const qint64 totalCents = wholeYuan * 100 + cents;
    if (!wholeOk || totalCents < 1 || totalCents > 1000000) {
        return std::nullopt;
    }

    return totalCents;
}

void ProfileController::showFailure(const ApiResponse &response)
{
    if (response.code == protocol::ErrorCode::InvalidSession) {
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return;
    }

    page_.showMessage(response.message.isEmpty() ? QStringLiteral("操作失败，请稍后重试")
                                                  : response.message,
                      true);
}

void ProfileController::finishRequest()
{
    pendingRequestId_.clear();
    pendingAction_ = PendingAction::None;
    page_.setBusy(false);
}

}  // namespace charging::client
