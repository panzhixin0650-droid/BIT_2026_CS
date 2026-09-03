#include "api/mock_charging_api.h"

#include "charging/protocol/protocol_constants.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

namespace charging::client {

namespace {

const QRegularExpression kPhonePattern(QStringLiteral("^\\d{11}$"));

}  // namespace

MockChargingApi::MockChargingApi(QObject *parent)
    : IChargingApi(parent)
{
    protocol::UserDto fixtureUser;
    fixtureUser.userId = 1;
    fixtureUser.phone = QStringLiteral("13800000001");
    fixtureUser.nickname = QStringLiteral("演示用户0001");
    fixtureUser.balanceCents = 20000;
    fixtureUser.status = protocol::UserStatus::Active;
    fixtureUser.createdAt = QStringLiteral("2026-06-04T11:53:41Z");
    usersByPhone_.insert(fixtureUser.phone, fixtureUser);
}

QString MockChargingApi::loginUser(const QString &phone)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, phone]() {
        LoginResult result;
        if (!kPhonePattern.match(phone).hasMatch()) {
            result.response = response(requestId,
                                       protocol::MessageType::AuthUserLogin,
                                       protocol::ErrorCode::InvalidRequest,
                                       QStringLiteral("手机号必须为11位数字"));
            emit loginCompleted(result);
            return;
        }

        const bool isNewUser = !usersByPhone_.contains(phone);
        if (isNewUser) {
            protocol::UserDto user;
            user.userId = nextUserId_++;
            user.phone = phone;
            user.nickname = QStringLiteral("用户%1").arg(phone.right(4));
            user.balanceCents = 0;
            user.status = protocol::UserStatus::Active;
            user.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            usersByPhone_.insert(phone, user);
        }

        authenticatedPhone_ = phone;
        token_ = QStringLiteral("mock-token-%1").arg(requestId);
        result.response = response(requestId,
                                   protocol::MessageType::AuthUserLogin,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = LoginPayload{token_, isNewUser, usersByPhone_.value(phone)};
        emit loginCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::logout()
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId]() {
        LogoutResult result;
        if (token_.isEmpty()) {
            result.response = response(requestId,
                                       protocol::MessageType::AuthLogout,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("登录状态已失效"));
            emit logoutCompleted(result);
            return;
        }

        authenticatedPhone_.clear();
        token_.clear();
        result.response = response(requestId,
                                   protocol::MessageType::AuthLogout,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = LogoutPayload{true};
        emit logoutCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::getProfile()
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId]() {
        UserResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            token_.clear();
            authenticatedPhone_.clear();
            result.response = response(requestId,
                                       protocol::MessageType::UserProfileGet,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit profileCompleted(result);
            return;
        }

        result.response = response(requestId,
                                   protocol::MessageType::UserProfileGet,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = UserPayload{*user};
        emit profileCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::updateNickname(const QString &nickname)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, nickname]() {
        UserResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            token_.clear();
            authenticatedPhone_.clear();
            result.response = response(requestId,
                                       protocol::MessageType::UserProfileUpdate,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit profileUpdateCompleted(result);
            return;
        }

        if (nickname.isEmpty() || nickname.size() > 32) {
            result.response = response(requestId,
                                       protocol::MessageType::UserProfileUpdate,
                                       protocol::ErrorCode::InvalidRequest,
                                       QStringLiteral("昵称长度必须为1到32个字符"));
            emit profileUpdateCompleted(result);
            return;
        }

        auto updatedUser = *user;
        updatedUser.nickname = nickname;
        usersByPhone_.insert(updatedUser.phone, updatedUser);
        result.response = response(requestId,
                                   protocol::MessageType::UserProfileUpdate,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = UserPayload{updatedUser};
        emit profileUpdateCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::nextRequestId()
{
    return QStringLiteral("mock-%1").arg(++requestSequence_);
}

ApiResponse MockChargingApi::response(const QString &requestId,
                                      const char *type,
                                      int code,
                                      const QString &message) const
{
    return ApiResponse{requestId, QString::fromLatin1(type), code, message};
}

std::optional<protocol::UserDto> MockChargingApi::authenticatedUser() const
{
    if (token_.isEmpty() || authenticatedPhone_.isEmpty()
        || !usersByPhone_.contains(authenticatedPhone_)) {
        return std::nullopt;
    }

    return usersByPhone_.value(authenticatedPhone_);
}

}  // namespace charging::client
