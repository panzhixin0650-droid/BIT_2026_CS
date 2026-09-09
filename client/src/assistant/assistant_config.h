// 本文件定义 AI 助理的本地配置结构
#pragma once

#include <QString>
#include <QUrl>

namespace charging::client {

// AssistantConfig 保存地址、密钥、模型与限额等配置项
struct AssistantConfig {
    QString baseUrl;
    QString apiKey;
    QString model;
    int timeoutMs = 45000;
    int maxOutputTokens = 2048;
    QString loadError;
    // supportModel 是模拟客服模式使用的模型名
    QString supportModel = QStringLiteral("gpt-5.6-sol");

    // 提供加载、校验、端点拼接和客服模式派生等方法
    static AssistantConfig load(const QString &explicitPath = {});
    QString validationError() const;
    bool isReady() const { return validationError().isEmpty(); }
    QUrl endpoint() const;
    AssistantConfig forSupportDesk() const;
};

}  // namespace charging::client
