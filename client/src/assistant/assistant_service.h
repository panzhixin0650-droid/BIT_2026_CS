// 本文件声明 AI 助理服务及其会话与结果数据结构
#pragma once

#include "assistant/assistant_config.h"
#include "assistant/knowledge_base.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QThread;

namespace charging::client {

class AssistantRequestWorker;
// Test/embedding seam: invoked on the IO thread; return a newly created manager.
using AssistantNetworkFactory = std::function<QNetworkAccessManager *()>;

// AssistantTurn 保存一轮问答，用作上下文
struct AssistantTurn {
    QString question;
    QString answer;
};

// 三种用途：通用助理、模拟客服、工单摘要
enum class AssistantPurpose { General, SupportDesk, TicketSummary };

// AssistantResult 汇总答案、错误、引用知识与状态标志
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
    // 可注入网络管理器或工厂，便于测试与线程切换
    explicit AssistantService(AssistantConfig config = {}, QObject *parent = nullptr,
                              QNetworkAccessManager *network = nullptr,
                              AssistantPurpose purpose = AssistantPurpose::General,
                              AssistantNetworkFactory networkFactory = {});
    ~AssistantService() override;
    const AssistantConfig &config() const { return config_; }
    const KnowledgeBase &knowledgeBase() const { return knowledge_; }
    bool isBusy() const { return activeId_ != 0; }
    // ask 发起一次问答，useModel 决定是否调用远端模型
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
    // 私有方法负责流式读取、事件解析与统一收尾
    void readAvailable();
    void networkFinished();
    void consumeEvent(const QByteArray &data);
    void completeResponse(const QJsonObject &response);
    void startWorkerRequest(quint64 id, const QString &question, const QList<AssistantTurn> &history);
    void publishUpdate();
    void finish(bool success, const QString &error = {}, bool cancelled = false);

    AssistantConfig config_;
    AssistantPurpose purpose_ = AssistantPurpose::General;
    KnowledgeBase knowledge_;
    QNetworkAccessManager *network_ = nullptr;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    QTimer updateTimer_;
    bool updatePending_ = false;
    AssistantNetworkFactory networkFactory_;
    std::shared_ptr<std::atomic<quint64>> acceptedRequest_ =
        std::make_shared<std::atomic<quint64>>(0);
    AssistantRequestWorker *worker_ = nullptr;
    QThread *workerThread_ = nullptr;
    quint64 sequence_ = 0;
    quint64 activeId_ = 0;
    AssistantResult result_;
    // buffer_ 和 eventData_ 暂存尚未解析完的流式字节
    QByteArray buffer_;
    QByteArray eventData_;
    qint64 receivedBytes_ = 0;
};

}  // namespace charging::client

Q_DECLARE_METATYPE(charging::client::AssistantResult)
