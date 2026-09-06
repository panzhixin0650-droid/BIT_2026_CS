#include "api/mock_charging_api.h"
#include "local/i_map_service.h"
#include "ui/main_window.h"
#include "ui/map_controller.h"
#include "ui/route_map_view.h"
#include "ui/station_browser_page.h"

#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <memory>
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

using namespace charging::client;

namespace {
class DeferredMap final : public IMapService {
public:
    QUrl mapScriptUrl() const override { return preloadUrl; }
    QString geocode(const QString &) override { return QStringLiteral("geo-%1").arg(++sequence); }
    QString openRoute(const MapLocation &, const MapLocation &, RouteMode) override
    { return QStringLiteral("route-%1").arg(++sequence); }
    void cancel(const QString &id) override { if (!id.isEmpty()) cancelled.append(id); }
    int sequence = 0;
    QStringList cancelled;
    QUrl preloadUrl;
};

charging::protocol::StationDto station(const QString &name = QStringLiteral("演示站"))
{
    charging::protocol::StationDto result;
    result.stationId = 1;
    result.name = name;
    result.address = QStringLiteral("沈阳市和平区青年大街创新园区充电站入口");
    result.longitude = 123.4;
    result.latitude = 41.79;
    return result;
}

void login(MainWindow &window)
{
    window.findChild<QLineEdit *>(QStringLiteral("phoneInput"))->setText(QStringLiteral("13800000001"));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"))->isVisible());
}

#ifdef CHARGING_CLIENT_HAS_WEBENGINE
// Offline SDK double: exercise our real HTML and QWebEngine bridge, not Tencent or a paid Key.
class ScriptServer final : public QTcpServer {
public:
    bool respond = true;
    int sdkRequestCount = 0;
    QByteArray script = R"JS(
      window.sdkCounts = {initializations:0, fits:0, zoom:12};
      class Map {
        constructor(element, options) { sdkCounts.initializations++; window.mapOptions = options; }
        fitBounds() { sdkCounts.fits++; sdkCounts.zoom = 12; }
        getZoom() { return sdkCounts.zoom; }
        setZoom(value) { sdkCounts.zoom = value; }
        resize() {}
      }
      class Layer { constructor() {} setGeometries(geometries) { window.lastGeometry = geometries; } }
      window.TMap = {Map, LatLng: class {constructor(lat,lng){this.lat=lat;this.lng=lng;}},
        LatLngBounds: class {extend() {}}, MultiPolyline: Layer, MultiMarker: Layer,
        PolylineStyle: class {}, MarkerStyle: class {}};
    )JS";
    ScriptServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    const QByteArray request = socket->readAll();
                    if (!respond) return;
                    if (socket->property("replied").toBool()) return;
                    socket->setProperty("replied", true);
                    if (request.startsWith("GET /sdk.js ")) ++sdkRequestCount;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\nContent-Length: "
                                  + QByteArray::number(script.size()) + "\r\nConnection: close\r\n\r\n" + script);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/sdk.js").arg(serverPort())); }
};

QVariant evaluate(QWebEngineView *view, const QString &script)
{
    auto result = std::make_shared<QVariant>();
    auto done = std::make_shared<bool>(false);
    view->page()->runJavaScript(script, [result, done](const QVariant &value) { *result = value; *done = true; });
    QElapsedTimer timer;
    timer.start();
    while (!*done && timer.elapsed() < 3000) QTest::qWait(10);
    return *result;
}

RouteResult realRoute(const QUrl &sdk)
{
    RouteResult result;
    result.success = true;
    result.summary = QStringLiteral("驾车约 180 米 · 2 分钟");
    result.mapScriptUrl = sdk;
    result.paths = QJsonArray{QJsonObject{
        {QStringLiteral("points"), QJsonArray{QJsonArray{41.79, 123.4}, QJsonArray{41.791, 123.401}}},
        {QStringLiteral("walking"), false}}};
    result.instructions = {QStringLiteral("沿演示道路前行")};
    return result;
}
#endif
}  // namespace

class NavigationTests final : public QObject {
    Q_OBJECT
private slots:
    void smallWindowFitsWithDetails_data();
    void smallWindowFitsWithDetails();
    void leavingRejectsStaleRoutesAndGeocodes();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    void startupPreloadReusesMapForFirstRoute();
    void failedPreloadIsSilentAndRetries();
    void mockModeDoesNotPreloadMap();
    void zoomFitAndRepeatedRoutesReuseMap();
    void failedSdkReleasesBusyState();
    void mapTimeoutReleasesBusyState();
#endif
};

void NavigationTests::smallWindowFitsWithDetails_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("minimum") << QSize(360, 640);
    QTest::newRow("default") << QSize(480, 860);
    QTest::newRow("landscape") << QSize(1000, 700);
}

void NavigationTests::smallWindowFitsWithDetails()
{
    QFETCH(QSize, size);
    MockChargingApi api;
    MainWindow window(api);
    window.resize(size);
    window.show();
    login(window);
    window.findChild<MapController *>()->openNavigation(station());
    auto *page = window.findChild<StationBrowserPage *>();
    RouteResult route;
    route.success = true;
    route.summary = QStringLiteral("离线 Mock 路线");
    route.message = QStringLiteral("Mock 路线已生成");
    route.instructions = {QStringLiteral("离线布局测试说明，不是真实道路或公交班次。")};
    page->showRouteResult(route);
    auto *details = window.findChild<QPlainTextEdit *>(QStringLiteral("routeDetails"));
    auto *toggle = window.findChild<QPushButton *>(QStringLiteral("routeDetailsButton"));
    auto *map = window.findChild<QStackedWidget *>(QStringLiteral("routeDisplayStack"));
    auto *navigation = window.findChild<QWidget *>(QStringLiteral("stationNavigationPage"));
    for (bool expanded : {false, true, false}) {
        toggle->setChecked(expanded);
        QTRY_COMPARE(details->isVisible(), expanded);
        QTRY_VERIFY(map->height() >= 120);
        QVERIFY(map->geometry().bottom() <= navigation->contentsRect().bottom());
        if (expanded) QVERIFY(details->geometry().bottom() <= navigation->contentsRect().bottom());
        const int mapBottom = map->mapTo(&window, QPoint(0, map->height())).y();
        QVERIFY(mapBottom <= window.findChild<QTabBar *>()->mapTo(&window, QPoint()).y());
    }
    QCOMPARE(window.size(), size);
    QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled());
}

void NavigationTests::leavingRejectsStaleRoutesAndGeocodes()
{
    DeferredMap service;
    StationBrowserPage page;
    MapController controller(page, service);
    page.show();
    controller.openNavigation(station());
    auto *plan = page.findChild<QPushButton *>(QStringLiteral("routePlanButton"));
    plan->click();
    QVERIFY(!plan->isEnabled());
    page.findChild<QPushButton *>(QStringLiteral("navigationBackButton"))->click();
    QVERIFY(service.cancelled.contains(QStringLiteral("route-1")));
    controller.openNavigation(station(QStringLiteral("新站点")));
    RouteResult old;
    old.requestId = QStringLiteral("route-1");
    old.success = true;
    old.summary = QStringLiteral("错误的旧路线");
    emit service.routeCompleted(old);
    QVERIFY(!page.findChild<QLabel *>(QStringLiteral("routeDisplay"))->text().contains(old.summary));
    page.findChild<QLineEdit *>(QStringLiteral("routeStartInput"))->setText(QStringLiteral("新的起点"));
    plan->click();
    QCOMPARE(service.sequence, 2);
    controller.openNavigation(station(QStringLiteral("第三站")));
    QVERIFY(service.cancelled.contains(QStringLiteral("geo-2")));
    emit service.geocodeCompleted({QStringLiteral("geo-2"), true, {}, MapLocation{QStringLiteral("旧起点"), 123.4, 41.79}});
    QCOMPARE(service.sequence, 2);
    QVERIFY(plan->isEnabled());
}

#ifdef CHARGING_CLIENT_HAS_WEBENGINE
void NavigationTests::startupPreloadReusesMapForFirstRoute()
{
    ScriptServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MockChargingApi api;
    DeferredMap service;
    service.preloadUrl = server.url();
    MainWindow window(api, service);
    window.show();

    QTRY_COMPARE_WITH_TIMEOUT(server.sdkRequestCount, 1, 10000);
    auto *view = window.findChild<QWebEngineView *>(QStringLiteral("routeWebView"));
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(view, QStringLiteral("!!(window.bitMap && bitMap.state.sdkReady && typeof TMap !== 'undefined')")).toBool(),
        10000);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 0);
    QCOMPARE(view->page()->lifecycleState(), QWebEnginePage::LifecycleState::Active);
    QSignalSpy loads(view, &QWebEngineView::loadStarted);

    login(window);
    auto *page = window.findChild<StationBrowserPage *>();
    page->showNavigation(station(), {QStringLiteral("起点"), 123.4, 41.79});
    QCOMPARE(page->findChild<QStackedWidget *>(QStringLiteral("routeDisplayStack"))->currentWidget(),
             page->findChild<RouteMapView *>());
    QTRY_COMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
    page->showRouteResult(realRoute(server.url()));
    auto *plus = page->findChild<QPushButton *>(QStringLiteral("mapZoomInButton"));
    QTRY_VERIFY_WITH_TIMEOUT(plus->isEnabled(), 10000);
    QCOMPARE(page->findChild<QWebEngineView *>(QStringLiteral("routeWebView")), view);
    QCOMPARE(loads.count(), 0);
    QCOMPARE(server.sdkRequestCount, 1);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
}

void NavigationTests::failedPreloadIsSilentAndRetries()
{
    ScriptServer server;
    const QByteArray workingScript = server.script;
    server.script = "/* SDK failed: TMap is unavailable */";
    QVERIFY(server.listen(QHostAddress::LocalHost));
    StationBrowserPage page;
    page.show();
    page.preloadMap(server.url());

    QTRY_COMPARE_WITH_TIMEOUT(server.sdkRequestCount, 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !page.findChild<QWebEngineView *>(QStringLiteral("routeWebView")), 10000);
    QCOMPARE(page.findChild<QLabel *>(QStringLiteral("routeMessage"))->text(), QString{});

    server.script = workingScript;
    page.showNavigation(station(), {QStringLiteral("起点"), 123.4, 41.79});
    page.showRouteResult(realRoute(server.url()));
    QTRY_VERIFY_WITH_TIMEOUT(
        page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled(),
        10000);
    QCOMPARE(server.sdkRequestCount, 2);
}

void NavigationTests::mockModeDoesNotPreloadMap()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    QTest::qWait(400);
    QVERIFY(!window.findChild<QWebEngineView *>(QStringLiteral("routeWebView")));
}

void NavigationTests::zoomFitAndRepeatedRoutesReuseMap()
{
    ScriptServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    StationBrowserPage page;
    page.resize(480, 796);
    page.show();
    page.showNavigation(station(), {QStringLiteral("起点"), 123.4, 41.79});
    const auto route = realRoute(server.url());
    page.showRouteResult(route);
    auto *plus = page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"));
    auto *minus = page.findChild<QPushButton *>(QStringLiteral("mapZoomOutButton"));
    auto *fit = page.findChild<QPushButton *>(QStringLiteral("mapFitRouteButton"));
    auto *plan = page.findChild<QPushButton *>(QStringLiteral("routePlanButton"));
    QVERIFY(!plan->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(plus->isEnabled(), 10000);
    QVERIFY(plan->isEnabled());
    auto *view = page.findChild<QWebEngineView *>(QStringLiteral("routeWebView"));
    QVERIFY(view);
    QSignalSpy loads(view, &QWebEngineView::loadStarted);
    plus->click();
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.zoom")).toInt(), 13);
    minus->click();
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.zoom")).toInt(), 12);
    for (int i = 0; i < 30; ++i) plus->click();
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.zoom")).toInt(), 20);
    for (int i = 0; i < 30; ++i) minus->click();
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.zoom")).toInt(), 3);
    fit->click();
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.zoom")).toInt(), 12);
    QCOMPARE(view->zoomFactor(), 1.0);
    QVERIFY(evaluate(view, QStringLiteral("mapOptions.draggable && mapOptions.scrollable && mapOptions.doubleClickZoom")).toBool());
    page.setRouteBusy(true);
    page.setRouteBusy(false);
    page.showRouteResult(route);
    QTRY_VERIFY(plus->isEnabled());
    QCOMPARE(page.findChild<QWebEngineView *>(QStringLiteral("routeWebView")), view);
    QCOMPARE(loads.count(), 0);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
    page.resize(360, 576);
    page.findChild<QPushButton *>(QStringLiteral("routeDetailsButton"))->click();
    QTest::qWait(50);
    auto *canvas = page.findChild<RouteMapView *>();
    QVERIFY(canvas->height() >= 120);
    QTRY_COMPARE(evaluate(view, QStringLiteral("document.getElementById('map').clientWidth")).toInt(), view->width());
}

void NavigationTests::failedSdkReleasesBusyState()
{
    ScriptServer server;
    server.script = "/* SDK failed: TMap is unavailable */";
    QVERIFY(server.listen(QHostAddress::LocalHost));
    StationBrowserPage page;
    page.show();
    page.showNavigation(station(), {QStringLiteral("起点"), 123.4, 41.79});
    page.showRouteResult(realRoute(server.url()));
    auto *plan = page.findChild<QPushButton *>(QStringLiteral("routePlanButton"));
    QTRY_VERIFY_WITH_TIMEOUT(plan->isEnabled(), 10000);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("routeMessage"))->text().contains(QStringLiteral("失败")));
    QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled());
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("routeDetailsButton"))->isEnabled());
    server.script = ScriptServer().script;
    page.showRouteResult(realRoute(server.url()));
    QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled(), 10000);
}

void NavigationTests::mapTimeoutReleasesBusyState()
{
    ScriptServer server;
    server.respond = false;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    StationBrowserPage page;
    page.show();
    page.showNavigation(station(), {QStringLiteral("起点"), 123.4, 41.79});
    page.showRouteResult(realRoute(server.url()));
    auto *plan = page.findChild<QPushButton *>(QStringLiteral("routePlanButton"));
    QVERIFY(!plan->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(plan->isEnabled(), 17000);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("routeMessage"))->text().contains(QStringLiteral("超时")));
    QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled());
}
#endif

QTEST_MAIN(NavigationTests)
#include "navigation_tests.moc"
