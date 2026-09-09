// 定义请求与响应的统一消息信封结构
#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace charging::protocol {

// 请求信封：版本、类型、请求 ID、可选令牌与业务数据
struct RequestEnvelope {
    int version = 1;
    QString type;
    QString requestId;
    std::optional<QString> token;
    QJsonObject data;

    // 信封的序列化与解析，解析失败通过 error 说明
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject &json,
                                       RequestEnvelope *result,
                                       QString *error = nullptr);
};

// 响应信封：比请求多出结果码与提示信息
struct ResponseEnvelope {
    int version = 1;
    QString type;
    QString requestId;
    int code = 0;
    QString message;
    QJsonObject data;

    // 响应的序列化与校验解析
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static bool fromJson(const QJsonObject &json,
                                       ResponseEnvelope *result,
                                       QString *error = nullptr);
};

}  // namespace charging::protocol
