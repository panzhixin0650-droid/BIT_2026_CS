// 本文件定义业务层统一返回值，供服务与传输层共享
#pragma once

#include <QJsonObject>
#include <QString>

#include <utility>

namespace charging::server {

// A small value type shared by ApplicationService and its future facades.
// Transport code converts it to the protocol ResponseEnvelope.
// 含错误码、提示文案与数据体三部分
struct ServiceResult {
    int code = 0;
    QString message = QStringLiteral("OK");
    QJsonObject data;

    // 错误码为0表示成功
    [[nodiscard]] bool ok() const noexcept { return code == 0; }

    // 构造成功结果并携带数据
    [[nodiscard]] static ServiceResult success(QJsonObject payload = {})
    {
        return {0, QStringLiteral("OK"), std::move(payload)};
    }

    // 构造失败结果，附带错误码与说明
    [[nodiscard]] static ServiceResult failure(int errorCode,
                                                QString errorMessage,
                                                QJsonObject payload = {})
    {
        return {errorCode, std::move(errorMessage), std::move(payload)};
    }
};

}  // namespace charging::server
