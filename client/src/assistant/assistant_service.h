#pragma once

#include "assistant/assistant_config.h"
#include "assistant/knowledge_base.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

namespace charging::client {

struct AssistantTurn {
    QString question;
    QString answer;
};

struct AssistantResult {
    QString answer;
    QString error;
    QList<KnowledgeEntry> sources;
    bool remote = false;
    bool success = false;
    bool cancelled = false;
};

// A serial, read-only external adapter. No charging API, account or database access.
class AssistantService final : public QObject {
    Q_OBJECT
public:
    explicit AssistantService(AssistantConfig config = {}, QObject *parent = nullptr,
                              QNetworkAccessManager *network = nullptr);
    ~AssistantService() override;
    const AssistantConfig &config() const { return config_; }
    const KnowledgeBase &knowledgeBase() const { return knowledge_; }
    bool isBusy() const { return activeId_ != 0; }
    quint64 ask(const QString &question, const QList<AssistantTurn> &history,
                bool useModel);
    void cancel();

signals:
    void answerUpdated(quint64 requestId, const QString &text);
    void finished(quint64 requestId, const charging::client::AssistantResult &result);

private:
    QString redact(QString text) const;
    QJsonObject requestBody(const QString &question,
                            const QList<AssistantTurn> &history) const;
    void readAvailable();
    void networkFinished();
    void consumeEvent(const QByteArray &data);
    void completeResponse(const QJsonObject &response);
    void finish(bool success, const QString &error = {}, bool cancelled = false);

    AssistantConfig config_;
    KnowledgeBase knowledge_;
    QNetworkAccessManager *network_ = nullptr;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    quint64 sequence_ = 0;
    quint64 activeId_ = 0;
    AssistantResult result_;
    QByteArray buffer_;
    QByteArray eventData_;
    qint64 receivedBytes_ = 0;
};

}  // namespace charging::client

Q_DECLARE_METATYPE(charging::client::AssistantResult)
