#include "local/mock_map_service.h"

#include <QSignalSpy>
#include <QtTest>

using namespace charging;

class MockMapServiceTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void geocodesPresetAndManualAddresses();
    void rejectsEmptyAndUnresolvableAddresses();
    void opensDrivingAndWalkingRoutes();
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

QTEST_GUILESS_MAIN(MockMapServiceTests)

#include "mock_map_service_tests.moc"
