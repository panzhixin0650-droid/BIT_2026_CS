#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

namespace charging::client {

class IChargingApi;
class ProfilePage;
class AvatarStorage;

class ProfileController final : public QObject {
    Q_OBJECT

public:
    ProfileController(ProfilePage &page,
                      IChargingApi &api,
                      AvatarStorage &avatarStorage,
                      QObject *parent = nullptr);

    void setInitialUser(const protocol::UserDto &user);
    void refreshProfile();

    // Forget UI requests from the previous authenticated session.
    void reset();

signals:
    void loggedOut();
    void authenticationRequired(const QString &message);
    void profileChanged(const charging::protocol::UserDto &user);

private:
    enum class PendingAction { None, Refresh, UpdateNickname, Recharge, Logout };

    void updateNickname(const QString &nickname);
    void recharge(const QString &amountYuan);
    void saveAvatar(const QString &sourcePath);
    void logout();
    void handleProfileCompleted(const UserResult &result);
    void handleProfileUpdateCompleted(const UserResult &result);
    void handleRechargeCompleted(const RechargeResult &result);
    void handleLogoutCompleted(const LogoutResult &result);
    [[nodiscard]] bool acceptResult(const ApiResponse &response,
                                    PendingAction action,
                                    const char *type);
    [[nodiscard]] std::optional<qint64> parseAmountCents(const QString &amountYuan) const;
    void showFailure(const ApiResponse &response);
    void finishRequest();

    ProfilePage &page_;
    IChargingApi &api_;
    AvatarStorage &avatarStorage_;
    QString currentAvatarKey_;
    QString pendingRequestId_;
    PendingAction pendingAction_ = PendingAction::None;
};

}  // namespace charging::client
