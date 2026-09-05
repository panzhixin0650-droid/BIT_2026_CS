#include "assistant/assistant_config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace charging::client {

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
    const auto object = doc.object().value(QStringLiteral("assistant")).toObject();
    config.baseUrl = object.value(QStringLiteral("baseUrl")).toString().trimmed();
    config.apiKey = object.value(QStringLiteral("apiKey")).toString().trimmed();
    config.model = object.value(QStringLiteral("model")).toString().trimmed();
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

QString AssistantConfig::validationError() const
{
    if (!loadError.isEmpty()) {
        return loadError;
    }
    if (baseUrl.isEmpty() || apiKey.isEmpty() || model.isEmpty()) {
        return QStringLiteral("AI 尚未配置完整；当前可使用本地知识库。");
    }
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

}  // namespace charging::client
