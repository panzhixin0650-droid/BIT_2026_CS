#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

class QImage;

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
    void rechargeSucceeded(qint64 balanceCents);
    void rechargeNeedsAttention(qint64 balanceCents,
                                const QString &message,
                                bool insufficientBalance);
    void pendingOrderSettled(const PaymentPayload &result);

private:
    enum class PendingAction {
        None,
        Refresh,
        UpdateNickname,
        Recharge,
        RechargeCheckCurrentOrder,
        RechargePayPendingOrder,
        Logout,
    };

    void updateNickname(const QString &nickname);
    void recharge(const QString &amountYuan);
    void saveAvatar(const QImage &image);
    void saveAvatar(const QString &sourcePath);
    void logout();
    void handleProfileCompleted(const UserResult &result);
    void handleProfileUpdateCompleted(const UserResult &result);
    void handleRechargeCompleted(const RechargeResult &result);
    void handleRechargeCurrentOrder(const CurrentOrderResult &result);
    void handleRechargePayment(const PaymentResult &result);
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
    qint64 rechargedBalanceCents_ = 0;
    qint64 pendingPaymentAmountCents_ = 0;
    PendingAction pendingAction_ = PendingAction::None;
};

}  // namespace charging::client
