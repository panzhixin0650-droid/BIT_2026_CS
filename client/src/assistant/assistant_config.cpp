// 本文件负责读取并校验本地 AI 助理配置
#include "assistant/assistant_config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QRegularExpression>

namespace charging::client {

// load 依次尝试几个候选路径查找本地配置文件
AssistantConfig AssistantConfig::load(const QString &explicitPath)
{
    AssistantConfig config;
    QString path = explicitPath;
    if (path.isEmpty()) {
        const QStringList candidates{
            QDir::current().filePath(QStringLiteral("client/config.local.json")),
            QDir::current().filePath(QStringLiteral("config.local.json")),
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                + QStringLiteral("/assistant.json")};
        for (const auto &candidate : candidates) {
            if (QFile::exists(candidate)) {
                path = candidate;
                break;
            }
        }
    }
    if (path.isEmpty()) {
        return config;
    }
    QFile file(path);
    // 打开失败或文件过大都视为配置不可用
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) {
        config.loadError = QStringLiteral("无法读取 AI 本地配置，请检查文件和权限。");
        return config;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()
        || !doc.object().value(QStringLiteral("assistant")).isObject()) {
        config.loadError = QStringLiteral("AI 配置格式错误，请参考 config.example.json。");
        return config;
    }
    // 读取 assistant 节点里的地址、密钥与模型名
    const auto object = doc.object().value(QStringLiteral("assistant")).toObject();
    config.baseUrl = object.value(QStringLiteral("baseUrl")).toString().trimmed();
    config.apiKey = object.value(QStringLiteral("apiKey")).toString().trimmed();
    config.model = object.value(QStringLiteral("model")).toString().trimmed();
    if (object.contains(QStringLiteral("supportModel")))
        config.supportModel = object.value(QStringLiteral("supportModel")).toString().trimmed();
    // 超时与输出上限必须是整数，否则报错返回
    for (const auto &name : {QStringLiteral("timeoutMs"), QStringLiteral("maxOutputTokens")}) {
        if (!object.contains(name)) {
            continue;
        }
        const auto value = object.value(name);
        if (!value.isDouble() || value.toDouble() != value.toInt(-1)) {
            config.loadError = QStringLiteral("AI 超时和输出上限必须为整数。");
            return config;
        }
        if (name == QStringLiteral("timeoutMs")) {
            config.timeoutMs = value.toInt();
        } else {
            config.maxOutputTokens = value.toInt();
        }
    }
    return config;
}

// validationError 逐项校验配置，返回中文提示或空串
QString AssistantConfig::validationError() const
{
    if (!loadError.isEmpty()) {
        return loadError;
    }
    if (baseUrl.isEmpty() || apiKey.isEmpty() || model.isEmpty()) {
        return QStringLiteral("AI 尚未配置完整；当前可使用本地知识库。");
    }
    // 只接受不含账号、查询和片段的 HTTPS 地址
    const QUrl url(baseUrl, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https")
        || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.hasQuery() || url.hasFragment()) {
        return QStringLiteral("AI 地址必须是无账号、参数和片段的 HTTPS 地址。");
    }
    for (const auto ch : apiKey) {
        if (ch.unicode() < 33 || ch.unicode() > 126) {
            return QStringLiteral("AI Key 格式不正确。");
        }
    }
    if (apiKey.size() > 512 || model.size() > 120
        || timeoutMs < 1000 || timeoutMs > 120000
        || maxOutputTokens < 128 || maxOutputTokens > 8192) {
        return QStringLiteral("AI 配置超出允许范围，请检查超时、模型和输出上限。");
    }
    return {};
}

// endpoint 规范化路径，保证以 /responses 结尾
QUrl AssistantConfig::endpoint() const
{
    QUrl url(baseUrl);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (!path.endsWith(QStringLiteral("/responses"))) {
        path += QStringLiteral("/responses");
    }
    url.setPath(path);
    return url;
}

// 客服模式复用同一凭证与地址，只替换模型名并校验
AssistantConfig AssistantConfig::forSupportDesk() const
{
    AssistantConfig copy = *this;
    copy.model = supportModel; // Same provider, base URL, credentials and Responses adapter.
    static const QRegularExpression validModel(QStringLiteral("^[A-Za-z0-9._:-]{1,120}\\z"));
    if (!validModel.match(copy.model).hasMatch())
        copy.loadError = QStringLiteral("客服模型名称无效，请检查 supportModel 配置。");
    return copy;
}

}  // namespace charging::client
