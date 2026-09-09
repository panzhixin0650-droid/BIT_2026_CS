// 把接口错误转成界面提示文案的小工具
#pragma once

#include "api/api_result.h"

namespace charging::client {

inline QString apiErrorMessage(const ApiResponse &response, const QString &fallback)
{
    // 服务不可用时结果未知，提示用户刷新核对再操作
    if (response.code == protocol::ErrorCode::ServiceUnavailable) {
        return QStringLiteral("服务暂不可用，尚未确认操作结果。请恢复连接后刷新页面核对，再继续操作。");
    }
    // 其余情况优先用服务端消息，为空才用兜底文案
    return response.message.isEmpty() ? fallback : response.message;
}

}  // namespace charging::client
