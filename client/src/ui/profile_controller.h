#pragma once

#include "api/api_result.h"

#include <QObject>
#include <QString>

class QImage;

namespace charging::client {

class IChargingApi;
class ProfilePage;
class AvatarStorage;

// 个人中心控制器声明：串联资料页、API与本机头像存储
class ProfileController final : public QObject {
    Q_OBJECT

public:
    ProfileController(ProfilePage &page,
                      IChargingApi &api,
                      AvatarStorage &avatarStorage,
                      QObject *parent = nullptr);

    // 分别提供初始资料设置和主动刷新入口
    void setInitialUser(const protocol::UserDto &user);
    void refreshProfile();

    // Forget UI requests from the previous authenticated session.
    void reset();

// 对外信号：登录失效、资料更新、充值结果与订单结算
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
    // 待处理动作枚举，充值后串成检查订单再支付两步
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
    // 校验响应归属并解析充值金额的辅助函数
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
    // 记录在途请求ID与充值相关金额，用于匹配回调
    QString pendingRequestId_;
    qint64 rechargedBalanceCents_ = 0;
    qint64 pendingPaymentAmountCents_ = 0;
    PendingAction pendingAction_ = PendingAction::None;
};

}  // namespace charging::client
