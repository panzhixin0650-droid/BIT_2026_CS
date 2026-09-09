// 本文件声明 AI 助理的串行 HTTP 工作线程对象
#pragma once

#include "assistant/assistant_service.h"

namespace charging::client {

// One serial HTTP worker per assistant. No widgets, charging API or database access.
class AssistantRequestWorker final : public QObject {
    Q_OBJECT
public:
    // 构造参数含配置、用途和共享的已接受请求编号
    AssistantRequestWorker(AssistantConfig config, AssistantPurpose purpose,
                           AssistantNetworkFactory factory,
                           std::shared_ptr<std::atomic<quint64>> acceptedRequest);
    void ask(quint64 id, const QString &question, const QList<AssistantTurn> &history);
    void cancel(quint64 id);
    void shutdown();
signals:
    // 通过信号把增量文本与最终结果回传给调用线程
    void answerUpdated(quint64 id, const QString &answer);
    void finished(quint64 id, const charging::client::AssistantResult &result);
private:
    AssistantConfig config_;
    AssistantPurpose purpose_;
    AssistantNetworkFactory factory_;
    // acceptedRequest_ 用原子量标记当前仍有效的请求
    std::shared_ptr<std::atomic<quint64>> acceptedRequest_;
    AssistantService *service_ = nullptr;
    // activeId_ 记录正在处理的请求编号
    quint64 activeId_ = 0;
};

}  // namespace charging::client
