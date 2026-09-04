#include "local/mock_map_service.h"
#include "local/tencent_map_service.h"

#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

using namespace charging;

class MockMapServiceTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void geocodesPresetAndManualAddresses();
    void rejectsEmptyAndUnresolvableAddresses();
    void opensDrivingAndWalkingRoutes();
    void tencentRouteUsesEditableEndpoints();
    void tencentAdapterRejectsMissingConfiguration();
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

    (void)service.openRoute({}, end, client::RouteMode::Driving);
    QTRY_COMPARE(spy.count(), 1);
    result = qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QVERIFY(!result.success);
}

void MockMapServiceTests::tencentRouteUsesEditableEndpoints()
{
    client::TencentMapService service(QStringLiteral("test-browser-key"));
    QSignalSpy spy(&service, &client::IMapService::routeCompleted);
    const client::MapLocation start{
        QStringLiteral("临时修改的起点"), 123.401234, 41.791234};
    const client::MapLocation end{
        QStringLiteral("和平演示充电站"), 123.40, 41.79};

    const QString requestId =
        service.openRoute(start, end, client::RouteMode::Driving);
    QTRY_COMPARE(spy.count(), 1);
    const auto result =
        qvariant_cast<client::RouteResult>(spy.takeFirst().at(0));
    QCOMPARE(result.requestId, requestId);
    QVERIFY(result.success);
    QCOMPARE(result.routeUrl.host(), QStringLiteral("apis.map.qq.com"));
    QCOMPARE(result.routeUrl.path(), QStringLiteral("/uri/v1/routeplan"));
    const QUrlQuery query(result.routeUrl);
    QCOMPARE(query.queryItemValue(QStringLiteral("type")), QStringLiteral("drive"));
    QCOMPARE(query.queryItemValue(QStringLiteral("from")),
             QStringLiteral("临时修改的起点"));
    QCOMPARE(query.queryItemValue(QStringLiteral("fromcoord")),
             QStringLiteral("41.791234,123.401234"));
    QCOMPARE(query.queryItemValue(QStringLiteral("to")),
             QStringLiteral("和平演示充电站"));
    QCOMPARE(query.queryItemValue(QStringLiteral("referer")),
             QStringLiteral("test-browser-key"));
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

QTEST_GUILESS_MAIN(MockMapServiceTests)

#include "mock_map_service_tests.moc"
