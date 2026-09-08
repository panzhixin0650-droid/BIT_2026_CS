#include "assistant/assistant_service.h"
#include "assistant_test_network.h"
#include "api/mock_charging_api.h"
#include "ui/support_desk_page.h"
#include "ui/support_page.h"

#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTextBrowser>
#include <QThread>
#include <QtTest>
#include <atomic>
#include <memory>

using namespace charging::client;

namespace {
struct NetworkState {
    std::atomic<bool> constructing{false}, constructed{false}, destroyed{false};
    std::atomic<bool> offUiThread{false}, posted{false};
    std::atomic<int> requests{0};
};

class SlowNetwork final : public assistant_test::Network {
public:
    SlowNetwork(std::shared_ptr<NetworkState> state, int postDelay, bool hanging)
        : state_(std::move(state)), postDelay_(postDelay) { hang = hanging; }
    ~SlowNetwork() override { state_->destroyed = true; }
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *body) override
    {
        state_->offUiThread = QThread::currentThread() != qApp->thread();
        ++state_->requests;
        if (postDelay_) QThread::msleep(postDelay_); // Simulate a blocked native startup call.
        auto *reply = Network::createRequest(op, request, body);
        state_->posted = true;
        return reply;
    }
private:
    std::shared_ptr<NetworkState> state_;
    int postDelay_;
};

AssistantNetworkFactory slowFactory(const std::shared_ptr<NetworkState> &state,
                                    int initDelay, int postDelay, bool hang = false)
{
    return [state, initDelay, postDelay, hang] {
        state->constructing = true;
        state->offUiThread = QThread::currentThread() != qApp->thread();
        if (initDelay) QThread::msleep(initDelay);
        auto *network = new SlowNetwork(state, postDelay, hang);
        state->constructed = true;
        return network;
    };
}

class PaintCounter final : public QObject {
public:
    int paints = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) ++paints;
        return false;
    }
};

template<typename T> T *child(QWidget &widget, const char *name)
{ return widget.findChild<T *>(QString::fromLatin1(name)); }

void capture(QWidget &widget, const char *name)
{
    const auto directory = qEnvironmentVariable("CHARGING_RESPONSIVENESS_SCREENSHOTS");
    if (!directory.isEmpty()) QVERIFY(widget.grab().save(QDir(directory).filePath(QString::fromLatin1(name))));
}
}

class AssistantResponsivenessTests final : public QObject {
    Q_OBJECT
private slots:
    void initializationAndPostStayOffUiThread_data();
    void initializationAndPostStayOffUiThread();
    void cancelledQueuedRequestsAreNeverSent();
    void deadlineAndDestructionDoNotWaitForStartup();
    void ordinaryPageAnimatesAndCanStop();
    void deskAndSummaryCanStopFromAnyTab();
    void denseStreamDoesNotRepaintPerToken();
    void deskStreamDoesNotRebuildHistory();
};

void AssistantResponsivenessTests::initializationAndPostStayOffUiThread_data()
{
    QTest::addColumn<int>("purpose");
    QTest::newRow("ordinary") << int(AssistantPurpose::General);
    QTest::newRow("desk") << int(AssistantPurpose::SupportDesk);
    QTest::newRow("summary") << int(AssistantPurpose::TicketSummary);
}

void AssistantResponsivenessTests::initializationAndPostStayOffUiThread()
{
    QFETCH(int, purpose);
    const auto state = std::make_shared<NetworkState>();
    auto service = std::make_unique<AssistantService>(assistant_test::config(), nullptr, nullptr,
        AssistantPurpose(purpose), slowFactory(state, 600, 600));
    QSignalSpy done(service.get(), &AssistantService::finished);
    bool deliveredOnUiThread = false;
    connect(service.get(), &AssistantService::finished, this, [&] {
        deliveredOnUiThread = QThread::currentThread() == qApp->thread();
    });
    QElapsedTimer elapsed; elapsed.start();
    QVERIFY(service->ask(QStringLiteral("如何预约充电"), {}, true));
    QVERIFY2(elapsed.elapsed() < 250, "ask() must return before network initialization");
    QTRY_VERIFY(state->constructing.load());
    QTimer heartbeat;
    int ticks = 0;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    heartbeat.start(10);
    QTest::qWait(200);
    QVERIFY(ticks >= 5);
    QVERIFY(done.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
    QVERIFY(state->offUiThread.load());
    QVERIFY(deliveredOnUiThread);
    const auto result = qvariant_cast<AssistantResult>(done.first()[1]);
    QVERIFY(result.success && result.remote);
    QCOMPARE(result.answer, QStringLiteral("请在站点详情选择闲置桩进行预约。[reserve]"));
    service.reset();
    QTRY_VERIFY(state->destroyed.load());
}

void AssistantResponsivenessTests::cancelledQueuedRequestsAreNeverSent()
{
    const auto state = std::make_shared<NetworkState>();
    auto service = std::make_unique<AssistantService>(assistant_test::config(), nullptr, nullptr,
        AssistantPurpose::General, slowFactory(state, 800, 0));
    QSignalSpy done(service.get(), &AssistantService::finished);
    const auto first = service->ask(QStringLiteral("预约充电"), {}, true);
    QTRY_VERIFY(state->constructing.load());
    service->cancel();
    const auto second = service->ask(QStringLiteral("取消预约"), {}, true);
    service->cancel();
    const auto current = service->ask(QStringLiteral("如何导航"), {}, true);
    QVERIFY(current > second && second > first);
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 2500);
    QCOMPARE(state->requests.load(), 1); // Two cancelled, queued questions never reached POST.
    QCOMPARE(done.last()[0].toULongLong(), current);
    QVERIFY(qvariant_cast<AssistantResult>(done[0][1]).cancelled);
    QVERIFY(qvariant_cast<AssistantResult>(done[1][1]).cancelled);
    QVERIFY(qvariant_cast<AssistantResult>(done.last()[1]).success);
    service.reset();
    QTRY_VERIFY(state->destroyed.load());
}

void AssistantResponsivenessTests::deadlineAndDestructionDoNotWaitForStartup()
{
    const auto state = std::make_shared<NetworkState>();
    auto config = assistant_test::config(); config.timeoutMs = 1000;
    auto service = std::make_unique<AssistantService>(config, nullptr, nullptr,
        AssistantPurpose::General, slowFactory(state, 2000, 0));
    QSignalSpy done(service.get(), &AssistantService::finished);
    service->ask(QStringLiteral("预约充电"), {}, true);
    QTRY_VERIFY(state->constructing.load());
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 1700);
    QVERIFY(!state->constructed.load());
    QVERIFY(qvariant_cast<AssistantResult>(done.first()[1]).error.contains(QStringLiteral("超时")));
    QElapsedTimer elapsed; elapsed.start();
    service.reset();
    QVERIFY2(elapsed.elapsed() < 250, "Closing must not join a blocked IO thread");
    QTRY_VERIFY_WITH_TIMEOUT(state->destroyed.load(), 3000);
    QCOMPARE(state->requests.load(), 0);
    QCOMPARE(done.size(), 1);
}

void AssistantResponsivenessTests::ordinaryPageAnimatesAndCanStop()
{
    const auto state = std::make_shared<NetworkState>();
    auto service = std::make_unique<AssistantService>(assistant_test::config(), nullptr, nullptr,
        AssistantPurpose::General, slowFactory(state, 0, 1200, true));
    {
        SupportPage page(*service); page.resize(380, 650); page.show();
        child<QPlainTextEdit>(page, "assistantInput")->setPlainText(QStringLiteral("如何预约充电"));
        auto *spinner = child<QWidget>(page, "assistantBusySpinner");
        PaintCounter paints; spinner->installEventFilter(&paints);
        QElapsedTimer elapsed; elapsed.start();
        child<QPushButton>(page, "assistantSend")->click();
        QVERIFY(elapsed.elapsed() < 250);
        QVERIFY(spinner->isVisible());
        QTRY_VERIFY(state->requests.load() == 1);
        QTest::qWait(250);
        QVERIFY(paints.paints >= 2);
        page.resize(400, 680);
        QCOMPARE(page.size(), QSize(400, 680));
        capture(page, "ordinary-waiting.png");
        elapsed.restart();
        child<QPushButton>(page, "assistantStop")->click();
        QVERIFY(elapsed.elapsed() < 250);
        QVERIFY(!service->isBusy());
        QVERIFY(!spinner->isVisible());
        page.resetConversation();
        QTRY_VERIFY_WITH_TIMEOUT(state->posted.load(), 2500);
        QTest::qWait(50);
        QVERIFY(child<QWidget>(page, "supportCard")->isVisible());
        QVERIFY(!child<QLabel>(page, "assistantAnswerText"));
    }
    service.reset();
    QTRY_VERIFY(state->destroyed.load());
}

void AssistantResponsivenessTests::deskAndSummaryCanStopFromAnyTab()
{
    MockChargingApi api;
    const auto deskState = std::make_shared<NetworkState>();
    const auto summaryState = std::make_shared<NetworkState>();
    auto desk = std::make_unique<AssistantService>(assistant_test::config(), nullptr, nullptr,
        AssistantPurpose::SupportDesk, slowFactory(deskState, 0, 1000, true));
    auto summary = std::make_unique<AssistantService>(assistant_test::config(), nullptr, nullptr,
        AssistantPurpose::TicketSummary, slowFactory(summaryState, 0, 1000, true));
    QSignalSpy created(&api, &IChargingApi::supportTicketCreated);
    {
        SupportDeskPage dialog(api, *desk, *summary);
        dialog.resize(390, 650);
        dialog.openDesk({{QStringLiteral("结算提示异常"), QStringLiteral("请核实订单结果")}});
        auto *draft = child<QPlainTextEdit>(dialog, "ticketSummary");
        draft->setPlainText(QStringLiteral("原草稿必须保留"));
        child<QPushButton>(dialog, "ticketGenerate")->click();
        QTRY_VERIFY(summaryState->requests.load() == 1);
        auto *spinner = child<QWidget>(dialog, "deskBusySpinner");
        PaintCounter paints; spinner->installEventFilter(&paints);
        QTest::qWait(200);
        QVERIFY(spinner->isVisible());
        QVERIFY(paints.paints >= 2);
        QCOMPARE(child<QTabWidget>(dialog, "deskTabs")->currentIndex(), 1);
        capture(dialog, "summary-waiting.png");
        auto *stop = child<QPushButton>(dialog, "deskCancelWaiting");
        QVERIFY(stop->isVisible());
        stop->click();
        QVERIFY(!summary->isBusy());
        QVERIFY(!spinner->isVisible());
        QCOMPARE(draft->toPlainText(), QStringLiteral("原草稿必须保留"));
        child<QTabWidget>(dialog, "deskTabs")->setCurrentIndex(0);
        child<QPlainTextEdit>(dialog, "deskInput")->setPlainText(QStringLiteral("如何预约充电"));
        child<QPushButton>(dialog, "deskSend")->click();
        QTRY_VERIFY(deskState->requests.load() == 1);
        capture(dialog, "desk-waiting.png");
        QElapsedTimer elapsed; elapsed.start();
        dialog.close();
        QVERIFY(elapsed.elapsed() < 250);
        QVERIFY(!desk->isBusy());
        dialog.resetSession();
        QTRY_VERIFY_WITH_TIMEOUT(deskState->posted.load() && summaryState->posted.load(), 2500);
        QTest::qWait(50);
        QVERIFY(draft->toPlainText().isEmpty());
        QCOMPARE(created.size(), 0);
    }
    desk.reset(); summary.reset();
    QTRY_VERIFY(deskState->destroyed.load() && summaryState->destroyed.load());
}

void AssistantResponsivenessTests::denseStreamDoesNotRepaintPerToken()
{
    assistant_test::Network network;
    network.body.clear();
    for (int i = 0; i < 4000; ++i)
        network.body += assistant_test::event({{"type", "response.output_text.delta"}, {"delta", QStringLiteral("充")}});
    network.body += assistant_test::event({{"type", "response.completed"},
        {"response", QJsonObject{{"status", "completed"}}}});
    AssistantService service(assistant_test::config(), nullptr, &network);
    QSignalSpy updates(&service, &AssistantService::answerUpdated);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    const auto result = qvariant_cast<AssistantResult>(done.first()[1]);
    QVERIFY(result.success);
    QCOMPARE(result.answer, QString(4000, QChar(0x5145)));
    QVERIFY(updates.size() >= 1 && updates.size() < 20);
    QCOMPARE(updates.last()[1].toString(), result.answer);
}

void AssistantResponsivenessTests::deskStreamDoesNotRebuildHistory()
{
    MockChargingApi api;
    assistant_test::Network network; network.hang = true;
    AssistantService desk(assistant_test::config(), nullptr, &network, AssistantPurpose::SupportDesk);
    AssistantService summary;
    SupportDeskPage dialog(api, desk, summary); dialog.openDesk();
    child<QPlainTextEdit>(dialog, "deskInput")->setPlainText(QStringLiteral("预约充电"));
    child<QPushButton>(dialog, "deskSend")->click();
    auto *chat = child<QTextBrowser>(dialog, "deskChat");
    QVERIFY(!chat->toPlainText().contains(QStringLiteral("再提交。你")));
    QSignalSpy changes(chat->document(), &QTextDocument::contentsChange);
    desk.answerUpdated(1, QStringLiteral("<script>充电说明"));
    desk.answerUpdated(1, QStringLiteral("<script>充电说明，不会重复生成历史"));
    QVERIFY(chat->toPlainText().contains(QStringLiteral("<script>充电说明，不会重复生成历史")));
    QVERIFY(chat->toPlainText().contains(QStringLiteral("我是客服小悦")));
    QVERIFY(!changes.isEmpty());
    for (const auto &change : changes) QVERIFY(change[0].toInt() > 0);
    desk.cancel();
}

QTEST_MAIN(AssistantResponsivenessTests)
#include "assistant_responsiveness_tests.moc"
