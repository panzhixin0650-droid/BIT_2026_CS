#pragma once

#include <QString>
#include <QUrl>

namespace charging::client {

struct AssistantConfig {
    QString baseUrl;
    QString apiKey;
    QString model;
    int timeoutMs = 45000;
    int maxOutputTokens = 2048;
    QString loadError;

    static AssistantConfig load(const QString &explicitPath = {});
    QString validationError() const;
    bool isReady() const { return validationError().isEmpty(); }
    QUrl endpoint() const;
};

}  // namespace charging::client
