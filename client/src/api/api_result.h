#pragma once

#include "charging/protocol/dto.h"
#include "charging/protocol/protocol_constants.h"

#include <QString>
#include <QMetaType>

#include <optional>

namespace charging::client {

struct ApiResponse {
    QString requestId;
    QString type;
    int code = protocol::ErrorCode::InternalError;
    QString message;

    [[nodiscard]] bool ok() const noexcept
    {
        return code == protocol::ErrorCode::Ok;
    }
};

template<typename Payload>
struct ApiResult {
    ApiResponse response;
    std::optional<Payload> payload;

    [[nodiscard]] bool ok() const noexcept
    {
        return response.ok();
    }
};

struct LoginPayload {
    QString token;
    bool isNewUser = false;
    protocol::UserDto user;
};

struct LogoutPayload {
    bool success = false;
};

struct UserPayload {
    protocol::UserDto user;
};

using LoginResult = ApiResult<LoginPayload>;
using LogoutResult = ApiResult<LogoutPayload>;
using UserResult = ApiResult<UserPayload>;

}  // namespace charging::client

Q_DECLARE_METATYPE(charging::client::LoginResult)
Q_DECLARE_METATYPE(charging::client::LogoutResult)
Q_DECLARE_METATYPE(charging::client::UserResult)
