#pragma once

#include "assistant/assistant_config.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

#include <cstring>

namespace assistant_test {

inline charging::client::AssistantConfig config()
{
    charging::client::AssistantConfig value;
    value.baseUrl = QStringLiteral("https://example.invalid/codex/v1");
    value.apiKey = QStringLiteral("test-key-not-a-secret");
    value.model = QStringLiteral("gpt-5.6-luna");
    return value;
}

inline QByteArray event(const QJsonObject &object)
{
    return "data: " + QJsonDocument(object).toJson(QJsonDocument::Compact) + "\r\n\r\n";
}

inline QByteArray success(const QString &text = QStringLiteral("请在站点详情选择闲置桩进行预约。[reserve]"))
{
    return event({{QStringLiteral("type"), QStringLiteral("response.output_text.delta")},
                  {QStringLiteral("delta"), text}})
        + event({{QStringLiteral("type"), QStringLiteral("response.completed")},
                 {QStringLiteral("response"), QJsonObject{{QStringLiteral("status"), QStringLiteral("completed")}}}});
}

class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QByteArray body, int status,
          const QByteArray &contentType, int chunkSize, bool hang,
          NetworkError error, QObject *parent)
        : QNetworkReply(parent), body_(std::move(body)), chunkSize_(chunkSize), error_(error)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!hang) { QTimer::singleShot(0, this, [this]() { deliver(); }); }
    }
    void abort() override
    {
        if (isFinished()) { return; }
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return available_.size() + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto size = qMin(maximum, qint64(available_.size()));
        if (size == 0) { return isFinished() ? -1 : 0; }
        std::memcpy(data, available_.constData(), size_t(size));
        available_.remove(0, size);
        return size;
    }
private:
    void deliver()
    {
        if (isFinished()) { return; }
        const auto count = chunkSize_ > 0 ? qMin(qsizetype(chunkSize_), body_.size()) : body_.size();
        available_.append(body_.left(count));
        body_.remove(0, count);
        emit readyRead();
        if (isFinished()) { return; }
        if (!body_.isEmpty()) {
            QTimer::singleShot(0, this, [this]() { deliver(); });
        } else {
            if (error_ != NoError) { setError(error_, QStringLiteral("fake error")); }
            setFinished(true);
            emit finished();
        }
    }
    QByteArray body_;
    QByteArray available_;
    int chunkSize_;
    NetworkError error_;
};

class Network final : public QNetworkAccessManager {
public:
    QByteArray body = success();
    int status = 200;
    QByteArray contentType = "text/event-stream";
    int chunkSize = 0;
    bool hang = false;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QList<QNetworkRequest> requests;
    QList<QByteArray> payloads;
    QPointer<Reply> lastReply;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *outgoing) override
    {
        requests.append(request);
        payloads.append(outgoing ? outgoing->readAll() : QByteArray());
        lastReply = new Reply(request, body, status, contentType, chunkSize, hang, error, this);
        return lastReply;
    }
};

}  // namespace assistant_test
