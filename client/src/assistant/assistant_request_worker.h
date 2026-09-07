#pragma once

#include "assistant/assistant_service.h"

namespace charging::client {

// One serial HTTP worker per assistant. No widgets, charging API or database access.
class AssistantRequestWorker final : public QObject {
    Q_OBJECT
public:
    AssistantRequestWorker(AssistantConfig config, AssistantPurpose purpose,
                           AssistantNetworkFactory factory,
                           std::shared_ptr<std::atomic<quint64>> acceptedRequest);
    void ask(quint64 id, const QString &question, const QList<AssistantTurn> &history);
    void cancel(quint64 id);
    void shutdown();
signals:
    void answerUpdated(quint64 id, const QString &answer);
    void finished(quint64 id, const charging::client::AssistantResult &result);
private:
    AssistantConfig config_;
    AssistantPurpose purpose_;
    AssistantNetworkFactory factory_;
    std::shared_ptr<std::atomic<quint64>> acceptedRequest_;
    AssistantService *service_ = nullptr;
    quint64 activeId_ = 0;
};

}  // namespace charging::client
