#include "assistant/assistant_service.h"
#include "assistant_test_network.h"

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace charging::client;

class AssistantTests final : public QObject {
    Q_OBJECT
private slots:
    void knowledgePresets_data();
    void knowledgePresets();
    void knowledgeFollowUpAndNoHit();
    void configurationValidation();
    void localModeNeverConnects();
    void requestsAreBoundedAndRedacted();
    void streamingChunks_data();
    void streamingChunks();
    void failurePaths_data();
    void failurePaths();
    void nonStreamingResponse();
    void cancellationAndLateLocalCompletion();
    void timesOutAndCanRetry();
    void oversizedResponse();
    void localFixtureMatchesProtocol();
    void liveConfiguredEndpoint();
};

void AssistantTests::knowledgePresets_data()
{
    QTest::addColumn<QString>("question");
    QTest::addColumn<QString>("id");
    const auto base = KnowledgeBase::bundled();
    for (const auto &entry : base.entries()) {
        if (!entry.question.isEmpty()) {
            QTest::newRow(qPrintable(entry.id)) << entry.question << entry.id;
        }
    }
    QTest::newRow("paraphrase-reserve") << QStringLiteral("想预定一个桩，迟到会罚款吗") << QStringLiteral("reserve");
    QTest::newRow("paraphrase-wallet") << QStringLiteral("欠费之后在哪里补支付") << QStringLiteral("wallet");
    QTest::newRow("unavailable-human") << QStringLiteral("能帮我转人工客服报修吗") << QStringLiteral("fault");
    QTest::newRow("general-charging") << QStringLiteral("怎么充电") << QStringLiteral("start-charge");
    QTest::newRow("general-order") << QStringLiteral("我的订单怎么看") << QStringLiteral("orders");
}

void AssistantTests::knowledgePresets()
{
    QFETCH(QString, question);
    QFETCH(QString, id);
    const auto base = KnowledgeBase::bundled();
    QCOMPARE(base.suggestedQuestions().size(), 6);
    QVERIFY(base.entries().size() >= 14);
    const auto found = base.retrieve(question);
    QVERIFY(!found.isEmpty());
    QCOMPARE(found.first().id, id);
    QVERIFY(found.size() <= 3);
    QVERIFY(!found.first().source.isEmpty());
}

void AssistantTests::knowledgeFollowUpAndNoHit()
{
    const auto base = KnowledgeBase::bundled();
    QVERIFY(base.retrieve(QStringLiteral("今天火星上天气好吗")).isEmpty());
    QVERIFY(base.retrieve(QStringLiteral("你好啊")).first().id == QStringLiteral("assistant"));
    const auto follow = base.retrieve(QStringLiteral("那可以取消吗？"), QStringLiteral("如何预约充电？"));
    QVERIFY(!follow.isEmpty());
    QCOMPARE(follow.first().id, QStringLiteral("reserve"));
    QVERIFY(follow.first().content.contains(QStringLiteral("不实现预约自动过期")));
    const auto price = base.retrieve(QStringLiteral("充电怎么收费？")).first();
    QVERIFY(price.content.contains(QStringLiteral("+ 500")));
}

void AssistantTests::configurationValidation()
{
    auto config = assistant_test::config();
    QVERIFY(config.isReady());
    QCOMPARE(config.endpoint().path(), QStringLiteral("/codex/v1/responses"));
    config.baseUrl += QStringLiteral("/responses/");
    QCOMPARE(config.endpoint().path(), QStringLiteral("/codex/v1/responses"));
    for (const auto &url : {QStringLiteral("http://example.invalid/v1"),
            QStringLiteral("https://user:secret@example.invalid/v1"),
            QStringLiteral("https://example.invalid/v1?k=x"), QStringLiteral("https://example.invalid/v1#x")}) {
        config.baseUrl = url;
        QVERIFY(!config.isReady());
    }
    config = assistant_test::config();
    config.apiKey += QStringLiteral("\r\nExtra-Header: bad");
    QVERIFY(!config.isReady());
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("config.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"assistant\":{\"baseUrl\":\"https://example.invalid/v1\",\"apiKey\":\"test\",\"model\":\"test\",\"timeoutMs\":2000}}");
    file.close();
    QVERIFY(AssistantConfig::load(path).isReady());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{\"assistant\":{\"timeoutMs\":1.5}}");
    file.close();
    QVERIFY(!AssistantConfig::load(path).isReady());
    QVERIFY(!AssistantConfig::load(dir.filePath(QStringLiteral("missing.json"))).isReady());
}

void AssistantTests::localModeNeverConnects()
{
    assistant_test::Network network;
    AssistantService service({}, nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("如何预约充电？"), {}, false);
    QTRY_COMPARE(done.size(), 1);
    auto result = qvariant_cast<AssistantResult>(done.takeFirst()[1]);
    QVERIFY(result.success);
    QVERIFY(!result.remote);
    QVERIFY(result.answer.contains(QStringLiteral("预约")));
    QCOMPARE(network.requests.size(), 0);
    service.ask(QStringLiteral("如何预约充电？"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(!qvariant_cast<AssistantResult>(done.takeFirst()[1]).success);
    QCOMPARE(network.requests.size(), 0);
    AssistantService ready(assistant_test::config(), nullptr, &network);
    QSignalSpy readyDone(&ready, &AssistantService::finished);
    ready.ask(QStringLiteral("地球的卫星有什么颜色"), {}, true);
    QTRY_COMPARE(readyDone.size(), 1);
    result = qvariant_cast<AssistantResult>(readyDone.takeFirst()[1]);
    QVERIFY(result.success && !result.remote);
    QVERIFY(result.sources.isEmpty());
    QVERIFY(result.answer.contains(QStringLiteral("未调用 AI")));
    ready.ask(QString(1201, QLatin1Char('x')), {}, true);
    QTRY_COMPARE(readyDone.size(), 1);
    QVERIFY(!qvariant_cast<AssistantResult>(readyDone.takeFirst()[1]).success);
    QCOMPARE(network.requests.size(), 0);
}

void AssistantTests::requestsAreBoundedAndRedacted()
{
    assistant_test::Network network;
    AssistantService service(assistant_test::config(), nullptr, &network);
    QList<AssistantTurn> history;
    for (int i = 0; i < 7; ++i) {
        history.append({QStringLiteral("旧问题%1：13800000001").arg(i), QStringLiteral("sk-dummy-private")});
    }
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("如何预约？我的手机号13800000001，Key sk-dummy-private"), history, true);
    QCOMPARE(network.requests.size(), 1);
    const auto request = network.requests.first();
    QCOMPARE(request.url().path(), QStringLiteral("/codex/v1/responses"));
    QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
             int(QNetworkRequest::ManualRedirectPolicy));
    const auto bytes = network.payloads.first();
    QVERIFY(!bytes.contains("13800000001"));
    QVERIFY(!bytes.contains("sk-dummy-private"));
    QVERIFY(!bytes.contains("test-key-not-a-secret"));
    const auto body = QJsonDocument::fromJson(bytes).object();
    QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-5.6-luna"));
    QCOMPARE(body.value(QStringLiteral("input")).toArray().size(), 9);
    QVERIFY(body.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("reserve")));
    QVERIFY(body.value(QStringLiteral("stream")).toBool());
    QVERIFY(!body.value(QStringLiteral("store")).toBool());
    QVERIFY(!body.contains(QStringLiteral("tools")));
    QVERIFY(!body.contains(QStringLiteral("token")));
    QTRY_COMPARE(done.size(), 1);
}

void AssistantTests::streamingChunks_data()
{
    QTest::addColumn<int>("chunkSize");
    for (const int size : {1, 2, 3, 31, 4096}) { QTest::newRow(qPrintable(QString::number(size))) << size; }
}

void AssistantTests::streamingChunks()
{
    QFETCH(int, chunkSize);
    assistant_test::Network network;
    network.chunkSize = chunkSize;
    AssistantService service(assistant_test::config(), nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    QSignalSpy delta(&service, &AssistantService::answerUpdated);
    const auto id = service.ask(QStringLiteral("预约充电"), {}, true);
    QVERIFY(id > 0);
    QCOMPARE(service.ask(QStringLiteral("重复"), {}, true), quint64(0));
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(done.first()[0].toULongLong(), id);
    const auto result = qvariant_cast<AssistantResult>(done.first()[1]);
    QVERIFY(result.success && result.remote);
    QVERIFY(!result.answer.contains(QChar::ReplacementCharacter));
    QCOMPARE(result.answer, QStringLiteral("请在站点详情选择闲置桩进行预约。[reserve]"));
    QVERIFY(delta.size() >= 1);
    QVERIFY(!service.isBusy());
}

void AssistantTests::failurePaths_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("body");
    for (const int status : {301, 400, 401, 403, 404, 429, 500}) {
        QTest::newRow(qPrintable(QString::number(status))) << status << QByteArray("secret error details");
    }
    QTest::newRow("invalid-json") << 200 << QByteArray("data: broken\n\n");
    QTest::newRow("no-completion") << 200 << assistant_test::event({{"type", "response.output_text.delta"}, {"delta", "partial"}});
    QTest::newRow("missing-frame-end") << 200 << QByteArray("data: {\"type\":\"response.completed\"}");
    QTest::newRow("sentinel-only") << 200 << QByteArray("data: [DONE]\n\n");
    QTest::newRow("empty-answer") << 200 << assistant_test::success(QString());
    QTest::newRow("incomplete") << 200 << assistant_test::event({{"type", "response.incomplete"}});
    QTest::newRow("failed") << 200 << assistant_test::event({{"type", "response.failed"}});
    QTest::newRow("error") << 200 << assistant_test::event({{"type", "error"}, {"message", "secret error details"}});
    QTest::newRow("invalid-delta") << 200 << assistant_test::event({{"type", "response.output_text.delta"}, {"delta", 42}});
}

void AssistantTests::failurePaths()
{
    QFETCH(int, status);
    QFETCH(QByteArray, body);
    assistant_test::Network network;
    network.status = status;
    network.body = body;
    AssistantService service(assistant_test::config(), nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    const auto result = qvariant_cast<AssistantResult>(done.first()[1]);
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!result.error.contains(QStringLiteral("secret error details")));
    QVERIFY(!service.isBusy());
}

void AssistantTests::nonStreamingResponse()
{
    assistant_test::Network network;
    network.contentType = "application/json; charset=utf-8";
    network.chunkSize = 3;
    network.body = R"({"status":"completed","output":[{"type":"reasoning"},{"type":"message","content":[{"type":"output_text","text":"预约说明"}]}]})";
    AssistantService service(assistant_test::config(), nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    const auto result = qvariant_cast<AssistantResult>(done.first()[1]);
    QVERIFY(result.success);
    QCOMPARE(result.answer, QStringLiteral("预约说明"));
}

void AssistantTests::cancellationAndLateLocalCompletion()
{
    assistant_test::Network network;
    network.hang = true;
    AssistantService service(assistant_test::config(), nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("预约充电"), {}, true);
    service.cancel();
    QCOMPARE(done.size(), 1);
    QVERIFY(qvariant_cast<AssistantResult>(done.takeFirst()[1]).cancelled);
    const auto old = service.ask(QStringLiteral("预约充电"), {}, false);
    service.cancel();
    const auto current = service.ask(QStringLiteral("如何导航？"), {}, false);
    QVERIFY(current > old);
    QTRY_COMPARE(done.size(), 2);
    QCOMPARE(done.last()[0].toULongLong(), current);
    QVERIFY(qvariant_cast<AssistantResult>(done.last()[1]).success);
    QVERIFY(!service.isBusy());
}

void AssistantTests::timesOutAndCanRetry()
{
    assistant_test::Network network;
    network.hang = true;
    auto config = assistant_test::config();
    config.timeoutMs = 1000;
    AssistantService service(config, nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
    QVERIFY(qvariant_cast<AssistantResult>(done.takeFirst()[1]).error.contains(QStringLiteral("超时")));
    network.hang = false;
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(qvariant_cast<AssistantResult>(done.first()[1]).success);
}

void AssistantTests::oversizedResponse()
{
    assistant_test::Network network;
    network.body = QByteArray(1024 * 1024 + 1, 'x');
    AssistantService service(assistant_test::config(), nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(!qvariant_cast<AssistantResult>(done.first()[1]).success);
}

void AssistantTests::localFixtureMatchesProtocol()
{
    QFile file(QStringLiteral(ASSISTANT_FIXTURE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(fixture.value(QStringLiteral("request")).toObject().value(QStringLiteral("model")).toString(),
             QStringLiteral("gpt-5.6-luna"));
    assistant_test::Network network;
    network.body.clear();
    for (const auto event : fixture.value(QStringLiteral("successEvents")).toArray()) {
        network.body += assistant_test::event(event.toObject());
    }
    AssistantService service(assistant_test::config(), nullptr, &network);
    QSignalSpy done(&service, &AssistantService::finished);
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(qvariant_cast<AssistantResult>(done.takeFirst()[1]).success);
    network.body = assistant_test::event(fixture.value(QStringLiteral("failureEvent")).toObject());
    service.ask(QStringLiteral("预约充电"), {}, true);
    QTRY_COMPARE(done.size(), 1);
    QVERIFY(!qvariant_cast<AssistantResult>(done.first()[1]).success);
}

void AssistantTests::liveConfiguredEndpoint()
{
    const auto path = qEnvironmentVariable("CHARGING_ASSISTANT_LIVE_CONFIG");
    if (path.isEmpty()) { QSKIP("Live calls are opt-in; normal tests use an isolated fake network."); }
    const auto config = AssistantConfig::load(path);
    QVERIFY2(config.isReady(), qPrintable(config.validationError()));
    AssistantService service(config);
    QSignalSpy done(&service, &AssistantService::finished);
    QSignalSpy deltas(&service, &AssistantService::answerUpdated);
    service.ask(QStringLiteral("如何预约充电？迟到会罚款吗？"), {}, true);
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 125000);
    const auto result = qvariant_cast<AssistantResult>(done.first()[1]);
    QVERIFY2(result.success && result.remote, qPrintable(result.error));
    QVERIFY(!result.answer.isEmpty());
    QVERIFY(!result.sources.isEmpty());
    QVERIFY(deltas.size() > 0);
    qInfo().noquote() << "Live RAG answer:" << result.answer;
}

QTEST_GUILESS_MAIN(AssistantTests)
#include "assistant_tests.moc"
