#include "local/mock_map_service.h"
#include "local/tencent_map_service.h"

#include <QSignalSpy>
#include <QFile>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QtTest>

using namespace charging;

namespace {

class MemoryReply final : public QNetworkReply {
public:
    MemoryReply(const QNetworkRequest &request, QByteArray body,
                NetworkError error, int delayMs, int *abortCount, QObject *parent)
        : QNetworkReply(parent), body_(std::move(body)), abortCount_(abortCount)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(delayMs, this, [this, error]() {
            if (error != NoError) setError(error, QStringLiteral("Simulated network failure"));
            setFinished(true);
            emit finished();
        });
    }
    void abort() override { ++*abortCount_; }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 size = qMin(maxSize, body_.size() - offset_);
        if (size <= 0) return -1;
        std::memcpy(data, body_.constData() + offset_, static_cast<size_t>(size));
        offset_ += size;
        return size;
    }
private:
    QByteArray body_;
    qint64 offset_ = 0;
    int *abortCount_;
};

class MemoryNetwork final : public QNetworkAccessManager {
public:
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QList<QUrl> urls;
    int replyDelayMs = 0;
    int abortCount = 0;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        urls.append(request.url());
        return new MemoryReply(request, body, error, replyDelayMs, &abortCount, this);
    }
};

}  // namespace

class MockMapServiceTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void geocodesPresetAndManualAddresses();
    void rejectsEmptyAndUnresolvableAddresses();
    void opensDrivingAndWalkingRoutes();
    void tencentRouteUsesEditableEndpoints();
    void tencentAdapterRejectsMissingConfiguration();
    void transitMatchesLocalFixture();
    void rejectsInvalidTransitInputs();
    void cyclingAndWalkingResponses_data();
    void cyclingAndWalkingResponses();
    void cancelsPendingRequests();
    void drivingAndTransitFailures();
};

void MockMapServiceTests::initTestCase()
{
    qRegisterMetaType<client::GeocodeResult>();
    qRegisterMetaType<client::RouteResult>();
}

void MockMapServiceTests::geocodesPresetAndManualAddresses()
{
    client::MockMapService service;
    QSignalSpy spy(&service, &client::IMapService::geocodeCompleted);

    const QString requestId = service.geocode(QStringLiteral("沈阳市和平区青年大街"));
    QVERIFY(!requestId.isEmpty());
    QTRY_COMPARE(spy.count(), 1);
    const auto result =
        qvariant_cast<client::GeocodeResult>(spy.takeFirst().at(0));
    QCOMPARE(result.requestId, requestId);
    QVERIFY(result.success);
    QVERIFY(result.location.has_value());
    QCOMPARE(result.location->address, QStringLiteral("沈阳市和平区青年大街"));
    QCOMPARE(result.location->longitude, 123.40);
    QCOMPARE(result.location->latitude, 41.79);

}

void MockMapServiceTests::rejectsEmptyAndUnresolvableAddresses()
{
    client::MockMapService service;
    QSignalSpy spy(&service, &client::IMapService::geocodeCompleted);

    (void)service.geocode(QStringLiteral("   "));
    QTRY_COMPARE(spy.count(), 1);
    auto result = qvariant_cast<client::GeocodeResult>(spy.takeFirst().at(0));
    QVERIFY(!result.success);
    QVERIFY(!result.location.has_value());
    QVERIFY(result.message.contains(QStringLiteral("请输入")));

    (void)service.geocode(QStringLiteral("无法解析的位置"));
    QTRY_COMPARE(spy.count(), 1);
    result = qvariant_cast<client::GeocodeResult>(spy.takeFirst().at(0));
    QVERIFY(!result.success);
    QVERIFY(result.message.contains(QStringLiteral("未能解析")));

    (void)service.geocode(QStringLiteral("北京市朝阳区"));
    QTRY_COMPARE(spy.count(), 1);
    result = qvariant_cast<client::GeocodeResult>(spy.takeFirst().at(0));
    QVERIFY(!result.success);
    QVERIFY(result.message.contains(QStringLiteral("需接入腾讯地图")));
}

void MockMapServiceTests::opensDrivingAndWalkingRoutes()
{
    client::MockMapService service;
    QSignalSpy spy(&service, &client::IMapService::routeCompleted);
    const client::MapLocation start{QStringLiteral("演示位置"), 123.42, 41.70};
    const client::MapLocation end{QStringLiteral("浑南演示充电站"), 123.43, 41.71};

    (void)service.openRoute(start, end, client::RouteMode::Driving);
    QTRY_COMPARE(spy.count(), 1);
    auto result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QVERIFY(result.success);
    QVERIFY(result.summary.contains(QStringLiteral("驾车路线")));

    (void)service.openRoute(start, end, client::RouteMode::Walking);
    QTRY_COMPARE(spy.count(), 1);
    result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QVERIFY(result.success);
    QVERIFY(result.summary.contains(QStringLiteral("步行路线")));

    (void)service.openRoute(start, end, client::RouteMode::Transit);
    QTRY_COMPARE(spy.count(), 1);
    result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QVERIFY(result.success);
    QVERIFY(result.summary.contains(QStringLiteral("公共交通路线")));
    QVERIFY(result.summary.contains(QStringLiteral("离线 Mock")));
    QVERIFY(result.mapScriptUrl.isEmpty());

    (void)service.openRoute(start, end, client::RouteMode::Cycling);
    QTRY_COMPARE(spy.count(), 1);
    result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QVERIFY(result.success);
    QVERIFY(result.summary.contains(QStringLiteral("骑行路线")));
    QVERIFY(result.summary.contains(QStringLiteral("离线 Mock")));

    (void)service.openRoute({}, end, client::RouteMode::Driving);
    QTRY_COMPARE(spy.count(), 1);
    result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QVERIFY(!result.success);
}

void MockMapServiceTests::tencentRouteUsesEditableEndpoints()
{
    MemoryNetwork network;
    network.body = R"({"status":0,"result":{"routes":[{"distance":180,"duration":2.1,"polyline":[41.79,123.4,1000,1000],"steps":[{"instruction":"沿演示道路前行"}]}]}})";
    client::TencentMapService service(QStringLiteral("test-browser-key"), 5000, nullptr, &network);
    QSignalSpy spy(&service, &client::IMapService::routeCompleted);
    const client::MapLocation start{QStringLiteral("临时修改的起点"), 123.401234, 41.791234};
    const client::MapLocation end{QStringLiteral("和平演示充电站"), 123.40, 41.79};
    const QString requestId = service.openRoute(start, end, client::RouteMode::Driving);
    QTRY_COMPARE(spy.count(), 1);
    const auto result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QCOMPARE(result.requestId, requestId);
    QVERIFY(result.success);
    QCOMPARE(network.urls.size(), 1);
    QCOMPARE(network.urls.first().path(), QStringLiteral("/ws/direction/v1/driving/"));
    const QUrlQuery query(network.urls.first());
    QCOMPARE(query.queryItemValue(QStringLiteral("from")), QStringLiteral("41.791234,123.401234"));
    QCOMPARE(query.queryItemValue(QStringLiteral("to")), QStringLiteral("41.790000,123.400000"));
    QCOMPARE(query.queryItemValue(QStringLiteral("key")), QStringLiteral("test-browser-key"));
    QCOMPARE(result.summary, QStringLiteral("驾车约 180 米 · 3 分钟"));
    QCOMPARE(result.paths.size(), 1);
    QCOMPARE(result.instructions, QStringList{QStringLiteral("沿演示道路前行")});
    QCOMPARE(result.mapScriptUrl.path(), QStringLiteral("/api/gljs"));
}

void MockMapServiceTests::tencentAdapterRejectsMissingConfiguration()
{
    client::TencentMapService service({});
    QSignalSpy geocodeSpy(&service, &client::IMapService::geocodeCompleted);
    QSignalSpy routeSpy(&service, &client::IMapService::routeCompleted);

    (void)service.geocode(QStringLiteral("沈阳市和平区"));
    QTRY_COMPARE(geocodeSpy.count(), 1);
    const auto geocodeResult =
        qvariant_cast<client::GeocodeResult>(geocodeSpy.takeFirst().at(0));
    QVERIFY(!geocodeResult.success);
    QVERIFY(geocodeResult.message.contains(QStringLiteral("Key 未配置")));

    (void)service.openRoute(
        {QStringLiteral("沈阳市和平区"), 123.40, 41.79},
        {QStringLiteral("浑南演示充电站"), 123.43, 41.71},
        client::RouteMode::Driving);
    QTRY_COMPARE(routeSpy.count(), 1);
    const auto routeResult =
        qvariant_cast<client::RouteResult>(routeSpy.takeFirst().at(0));
    QVERIFY(!routeResult.success);
    QVERIFY(routeResult.message.contains(QStringLiteral("Key 未配置")));

    client::TencentMapService quotedService(QStringLiteral("'test-key'"));
    QSignalSpy quotedSpy(&quotedService,
                         &client::IMapService::geocodeCompleted);
    (void)quotedService.geocode(QStringLiteral("沈阳市和平区"));
    QTRY_COMPARE(quotedSpy.count(), 1);
    const auto quotedResult =
        qvariant_cast<client::GeocodeResult>(quotedSpy.takeFirst().at(0));
    QVERIFY(!quotedResult.success);
    QVERIFY(quotedResult.message.contains(QStringLiteral("不要包含引号")));
}

void MockMapServiceTests::transitMatchesLocalFixture()
{
    QFile fixture(QFINDTESTDATA("../../contracts/examples/map-route.transit.local.json"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(fixture.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("mode")).toString(), QStringLiteral("TRANSIT"));
    const auto location = [](const QJsonObject &object) {
        return client::MapLocation{object.value(QStringLiteral("address")).toString(),
                                   object.value(QStringLiteral("longitude")).toDouble(),
                                   object.value(QStringLiteral("latitude")).toDouble()};
    };
    const auto start = location(root.value(QStringLiteral("start")).toObject());
    const auto end = location(root.value(QStringLiteral("end")).toObject());
    MemoryNetwork network;
    network.body = QJsonDocument(root.value(QStringLiteral("tencentResponse")).toObject()).toJson();
    client::TencentMapService service(QStringLiteral("test-browser-key"), 5000, nullptr, &network);
    QSignalSpy spy(&service, &client::IMapService::routeCompleted);
    const auto requestId = service.openRoute(start, end, client::RouteMode::Transit);
    QTRY_COMPARE(spy.count(), 1);
    const auto result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QCOMPARE(result.requestId, requestId);
    QVERIFY(result.success);
    QVERIFY(result.summary.contains(QStringLiteral("公共交通")));
    QCOMPARE(network.urls.size(), 1);
    QCOMPARE(network.urls.first().path(), root.value(QStringLiteral("tencentEndpoint")).toString());
    const QUrlQuery query(network.urls.first());
    QCOMPARE(query.queryItemValue(QStringLiteral("from")), QStringLiteral("41.790000,123.400000"));
    QCOMPARE(query.queryItemValue(QStringLiteral("to")), QStringLiteral("41.710000,123.430000"));
    QCOMPARE(result.paths.size(), 3);
    QVERIFY(result.paths.first().toObject().value(QStringLiteral("walking")).toBool());
    QVERIFY(!result.paths.at(1).toObject().value(QStringLiteral("walking")).toBool());
    QVERIFY(result.instructions.join(QLatin1Char('\n')).contains(QStringLiteral("演示上车站")));
    QCOMPARE(result.summary, QStringLiteral("公共交通约 1.2 公里 · 9 分钟"));
}

void MockMapServiceTests::rejectsInvalidTransitInputs()
{
    const client::MapLocation start{QStringLiteral("和平区"), 123.4, 41.79};
    const client::MapLocation end{QStringLiteral("浑南区"), 123.43, 41.71};
    client::TencentMapService missingKey({});
    QSignalSpy missingSpy(&missingKey, &client::IMapService::routeCompleted);
    (void)missingKey.openRoute(start, end, client::RouteMode::Transit);
    QTRY_COMPARE(missingSpy.count(), 1);
    const auto missing = qvariant_cast<client::RouteResult>(missingSpy.takeFirst().at(0));
    QVERIFY(!missing.success);
    QVERIFY(missing.message.contains(QStringLiteral("Key 未配置")));

    client::TencentMapService real(QStringLiteral("test-browser-key"));
    client::MockMapService mock;
    for (client::IMapService *service : {static_cast<client::IMapService *>(&real),
                                        static_cast<client::IMapService *>(&mock)}) {
        QSignalSpy spy(service, &client::IMapService::routeCompleted);
        (void)service->openRoute({start.address, 181, 41.79}, end, client::RouteMode::Transit);
        QTRY_COMPARE(spy.count(), 1);
        auto result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
        QVERIFY(!result.success);
        QVERIFY(result.mapScriptUrl.isEmpty());
        (void)service->openRoute(start, end, static_cast<client::RouteMode>(999));
        QTRY_COMPARE(spy.count(), 1);
        result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
        QVERIFY(!result.success);
        QVERIFY(result.message.contains(QStringLiteral("不支持")));
    }
}

void MockMapServiceTests::cyclingAndWalkingResponses_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<int>("networkError");
    QTest::addColumn<bool>("success");
    QTest::addColumn<bool>("cycling");
    QFile fixture(QFINDTESTDATA("../../contracts/examples/map-route.cycling.local.json"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(fixture.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("mode")).toString(), QStringLiteral("CYCLING"));
    const QByteArray valid = QJsonDocument(root.value(QStringLiteral("tencentResponse")).toObject()).toJson();
    QTest::newRow("cycling-success") << valid << 0 << true << true;
    QTest::newRow("walking-regression") << valid << 0 << true << false;
    QTest::newRow("malformed-json") << QByteArray("not-json") << 0 << false << true;
    QTest::newRow("no-route") << QByteArray(R"({"status":0,"result":{"routes":[]}})") << 0 << false << true;
    QTest::newRow("service-rejected") << QByteArray(R"({"status":311,"message":"no route"})") << 0 << false << true;
    QTest::newRow("bad-polyline") << QByteArray(R"({"status":0,"result":{"routes":[{"polyline":[41,123,1]}]}})") << 0 << false << true;
    QTest::newRow("timeout") << QByteArray() << int(QNetworkReply::TimeoutError) << false << true;
}

void MockMapServiceTests::cyclingAndWalkingResponses()
{
    QFETCH(QByteArray, body);
    QFETCH(int, networkError);
    QFETCH(bool, success);
    QFETCH(bool, cycling);
    MemoryNetwork network;
    network.body = body;
    network.error = static_cast<QNetworkReply::NetworkError>(networkError);
    client::TencentMapService service(QStringLiteral("test-browser-key"), 5000, nullptr, &network);
    QSignalSpy spy(&service, &client::IMapService::routeCompleted);
    const auto id = service.openRoute({QStringLiteral("和平区"), 123.4, 41.79},
                                     {QStringLiteral("演示终点"), 123.401, 41.791},
                                     cycling ? client::RouteMode::Cycling : client::RouteMode::Walking);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(network.urls.size(), 1);
    QCOMPARE(network.urls.first().path(), cycling ? QStringLiteral("/ws/direction/v1/bicycling/")
                                                 : QStringLiteral("/ws/direction/v1/walking/"));
    const QUrlQuery query(network.urls.first());
    QCOMPARE(query.queryItemValue(QStringLiteral("from")), QStringLiteral("41.790000,123.400000"));
    QCOMPARE(query.queryItemValue(QStringLiteral("to")), QStringLiteral("41.791000,123.401000"));
    const auto result = qvariant_cast<client::RouteResult>(spy.first().at(0));
    QCOMPARE(result.requestId, id);
    QCOMPARE(result.success, success);
    const QString label = cycling ? QStringLiteral("骑行") : QStringLiteral("步行");
    if (success) {
        QVERIFY(result.summary.contains(label));
        QCOMPARE(result.summary, label + QStringLiteral("约 180 米 · 2 分钟"));
        const auto points = result.paths.first().toObject().value(QStringLiteral("points")).toArray();
        QCOMPARE(points.last().toArray().first().toDouble(), 41.791);
        QCOMPARE(points.last().toArray().last().toDouble(), 123.401);
    } else {
        QVERIFY(result.message.contains(label));
        QVERIFY(result.paths.isEmpty());
        QVERIFY(result.mapScriptUrl.isEmpty());
    }
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 1);
}


void MockMapServiceTests::cancelsPendingRequests()
{
    MemoryNetwork network;
    client::TencentMapService service(QStringLiteral("test-key"), 5000, nullptr, &network);
    QSignalSpy routeSpy(&service, &client::IMapService::routeCompleted);
    const auto id = service.openRoute({QStringLiteral("起点"), 123.4, 41.79},
                                     {QStringLiteral("终点"), 123.43, 41.71}, client::RouteMode::Driving);
    service.cancel(id);
    QTest::qWait(20);
    QVERIFY(network.urls.isEmpty());
    QVERIFY(routeSpy.isEmpty());
    network.replyDelayMs = 200;
    const auto inFlight = service.openRoute({QStringLiteral("起点"), 123.4, 41.79},
                                           {QStringLiteral("终点"), 123.43, 41.71}, client::RouteMode::Transit);
    QTRY_COMPARE(network.urls.size(), 1);
    service.cancel(inFlight);
    QCOMPARE(network.abortCount, 1);
    QTest::qWait(220);
    QVERIFY(routeSpy.isEmpty());
    QSignalSpy geocodeSpy(&service, &client::IMapService::geocodeCompleted);
    const auto geocodeId = service.geocode(QStringLiteral("沈阳市"));
    service.cancel(geocodeId);
    QTest::qWait(20);
    QVERIFY(geocodeSpy.isEmpty());
}

void MockMapServiceTests::drivingAndTransitFailures()
{
    const QList<QByteArray> invalid = {
        R"({"status":0,"result":{"routes":[]}})",
        R"({"status":311,"message":"Key rejected"})",
        R"({"status":0,"result":{"routes":[{"distance":-1,"duration":2,"polyline":[41,123,1,1]}]}})",
        R"({"status":0,"result":{"routes":[{"distance":100,"duration":2,"steps":[{"mode":"TRANSIT","lines":[]}]}]}})"
    };
    for (auto mode : {client::RouteMode::Driving, client::RouteMode::Transit}) {
        for (const auto &body : invalid) {
            MemoryNetwork network;
            network.body = body;
            client::TencentMapService service(QStringLiteral("test-key"), 5000, nullptr, &network);
            QSignalSpy spy(&service, &client::IMapService::routeCompleted);
            (void)service.openRoute({QStringLiteral("起点"), 123.4, 41.79},
                                    {QStringLiteral("终点"), 123.43, 41.71}, mode);
            QTRY_COMPARE(spy.count(), 1);
            const auto result = qvariant_cast<client::RouteResult>(spy.first().at(0));
            QVERIFY(!result.success);
            QVERIFY(result.paths.isEmpty());
            QVERIFY(result.mapScriptUrl.isEmpty());
            QVERIFY(!result.message.isEmpty());
        }
    }
}

QTEST_GUILESS_MAIN(MockMapServiceTests)

#include "mock_map_service_tests.moc"
