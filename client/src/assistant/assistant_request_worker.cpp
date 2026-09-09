// 本文件在独立线程中串行执行 AI 助理的网络请求
#include "assistant/assistant_request_worker.h"

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QThread>
#include <utility>

namespace charging::client {

// 构造只记录配置，网络对象留到线程切换后再建
AssistantRequestWorker::AssistantRequestWorker(AssistantConfig config, AssistantPurpose purpose,
                                               AssistantNetworkFactory factory,
                                               std::shared_ptr<std::atomic<quint64>> acceptedRequest)
    : config_(std::move(config)), purpose_(purpose), factory_(std::move(factory)),
      acceptedRequest_(std::move(acceptedRequest))
{
    // Network objects must be constructed after moveToThread(), not in this constructor.
}

// ask 首次调用时才创建网络管理器与助理服务
void AssistantRequestWorker::ask(quint64 id, const QString &question,
                                  const QList<AssistantTurn> &history)
{
    if (acceptedRequest_->load() != id) return; // Cancelled before dispatch: never send it.
    if (!service_) {
        auto *network = factory_ ? factory_() : new QNetworkAccessManager;
        if (!network) {
            AssistantResult result;
            result.remote = true;
            result.error = QStringLiteral("AI 网络初始化失败，请重试。");
            emit finished(id, result);
            return;
        }
        network->setParent(this);
        if (!factory_) {
            // Only this AI adapter bypasses desktop PAC/WPAD discovery. Never change
            // application/system proxies used by TCP, Tencent maps or other apps.
            network->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        }
        // Explicit manager selects the in-thread executor, not another worker.
        // 在工作线程内构造服务，并把流式片段与结果转发出去
        service_ = new AssistantService(config_, this, network, purpose_);
        connect(service_, &AssistantService::answerUpdated, this,
                [this](quint64, const QString &answer) { emit answerUpdated(activeId_, answer); });
        connect(service_, &AssistantService::finished, this,
                [this](quint64, const AssistantResult &result) {
            const auto id = activeId_;
            activeId_ = 0;
            emit finished(id, result);
        });
    }
    service_->cancel();
    // Initialization may have been slow; logout/timeout can invalidate us meanwhile.
    if (acceptedRequest_->load() != id) return;
    activeId_ = id;
    service_->ask(question, history, true);
}

// cancel 只取消当前正在处理的那个请求
void AssistantRequestWorker::cancel(quint64 id)
{
    if (service_ && activeId_ == id) service_->cancel();
}

// shutdown 中止请求并退出工作线程的事件循环
void AssistantRequestWorker::shutdown()
{
    if (service_) service_->cancel();
    QThread::currentThread()->quit();
}

}  // namespace charging::client
