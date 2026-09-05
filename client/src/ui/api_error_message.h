#pragma once

#include "api/api_result.h"

namespace charging::client {

inline QString apiErrorMessage(const ApiResponse &response, const QString &fallback)
{
    if (response.code == protocol::ErrorCode::ServiceUnavailable) {
        return QStringLiteral("服务暂不可用，尚未确认操作结果。请恢复连接后刷新页面核对，再继续操作。");
    }
    return response.message.isEmpty() ? fallback : response.message;
}

}  // namespace charging::client
