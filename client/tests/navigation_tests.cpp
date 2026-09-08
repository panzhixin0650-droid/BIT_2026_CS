#include "api/mock_charging_api.h"
#include "navigation_paint_helpers.h"
#include "local/i_map_service.h"
#include "ui/main_window.h"
#include "ui/map_controller.h"
#include "ui/route_map_view.h"
#include "ui/station_browser_page.h"
#include "ui/station_map_view.h"

#include <QJsonObject>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <memory>
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineProfile>
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
    window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"))->setText(QStringLiteral("123456"));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"))->isVisible());
}

#ifdef CHARGING_CLIENT_HAS_WEBENGINE
// Offline SDK double: exercise our real HTML and QWebEngine bridge, not Tencent or a paid Key.
class ScriptServer final : public QTcpServer {
public:
    bool respond = true;
    bool cacheable = false;
    int sdkRequestCount = 0;
    QByteArray script = R"JS(
      window.sdkCounts = {initializations:0, fits:0, zoom:12};
      window.sdkLayers = [];
      class Map {
        constructor(element, options) { sdkCounts.initializations++; window.mapOptions = options; window.sdkMap = this; this.events = {};
          window.initialViewport = {width:element.clientWidth, height:element.clientHeight}; }
        fitBounds(bounds, options) { sdkCounts.fits++; sdkCounts.zoom = 12; window.fittedBounds = bounds;
          window.fitOptions = options;
          if (!window.holdTiles) setTimeout(() => { if (this.events.tilesloaded) this.events.tilesloaded(); }, window.tileDelay || 25); }
        getZoom() { return sdkCounts.zoom; }
        setZoom(value) { sdkCounts.zoom = value; }
        setCenter(value) { window.lastCenter = value; }
        on(name, callback) { this.events[name] = callback; }
        resize() {}
      }
      class Layer {
        constructor(options) { this.options = options; this.events = {}; sdkLayers.push(this); }
        on(name, callback) { this.events[name] = callback; }
        setGeometries(geometries) { this.geometries = geometries; window.lastGeometry = geometries; }
      }
      window.TMap = {Map, LatLng: class {constructor(lat,lng){this.lat=lat;this.lng=lng;}},
        LatLngBounds: class {constructor(){this.points=[];} extend(p) {this.points.push(p);}}, MultiPolyline: Layer, MultiMarker: Layer,
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
                    socket->write(QByteArray("HTTP/1.1 200 OK\r\nContent-Type: application/javascript\r\n")
                                  + (cacheable ? "Cache-Control: public, max-age=3600\r\n" : "Cache-Control: no-store\r\n")
                                  + "Content-Length: "
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
    void switchingMainTabsKeepsNavigationState();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    void startupPreloadsHomeBeforeLogin();
    void slowTilesKeepPreviewUntilReady();
    void sdkHttpCacheIsShared();
    void failedHomePreloadRetriesAfterLogin();
    void stationMarkersAndBridgeUseSharedCanvas();
    void floatingNavigationSurvivesEmbeddedMapRepaints();
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

void NavigationTests::switchingMainTabsKeepsNavigationState()
{
    MockChargingApi api;
    DeferredMap service;
    MainWindow window(api, service);
    window.show();
    login(window);

    auto *controller = window.findChild<MapController *>();
    auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    auto *navigationPage = window.findChild<QWidget *>(
        QStringLiteral("stationNavigationPage"));
    controller->openNavigation(station());
    auto *plan = window.findChild<QPushButton *>(QStringLiteral("routePlanButton"));
    plan->click();
    QCOMPARE(service.sequence, 1);

    tabs->setCurrentIndex(3);
    QVERIFY(!service.cancelled.contains(QStringLiteral("route-1")));
    RouteResult route;
    route.requestId = QStringLiteral("route-1");
    route.success = true;
    route.message = QStringLiteral("离线路线已生成");
    route.summary = QStringLiteral("驾车约 1 公里 · 3 分钟");
    route.instructions = {QStringLiteral("沿演示道路前行")};
    emit service.routeCompleted(route);

    tabs->setCurrentIndex(0);
    QVERIFY(navigationPage->isVisible());
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("routeDisplay"))->text(),
             route.summary);
    QCOMPARE(window.findChild<QPlainTextEdit *>(
                 QStringLiteral("routeDetails"))->toPlainText(),
             route.instructions.first());
    QVERIFY(!service.cancelled.contains(QStringLiteral("route-1")));
}

#ifdef CHARGING_CLIENT_HAS_WEBENGINE
void NavigationTests::floatingNavigationSurvivesEmbeddedMapRepaints()
{
    ScriptServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MockChargingApi api;
    DeferredMap service;
    MainWindow window(api, service);
    window.resize(480, 760);
    window.show();
    login(window);
    auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    auto *bar = tabs->tabBar();
    auto image = navigation_test::presentedNavigation(*bar, QStringLiteral("before-map"));
    if (image.isNull()) {
        QSKIP("No window capture support; run with QT_QPA_PLATFORM=xcb under X11/Xvfb");
    }
    auto missing = navigation_test::missingNavigationContent(*bar, image);
    QVERIFY2(missing.isEmpty(), qPrintable(missing));

    auto *page = window.findChild<StationBrowserPage *>();
    page->showNavigation(station(), {QStringLiteral("起点"), 123.4, 41.79});
    page->showRouteResult(realRoute(server.url()));
    auto *plus = page->findChild<QPushButton *>(QStringLiteral("mapZoomInButton"));
    QTRY_VERIFY_WITH_TIMEOUT(plus->isEnabled(), 10000);
    for (int index : {0, 4, 1, 3, 0, 2, 4}) {
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(index).center());
        QTRY_COMPARE(tabs->currentIndex(), index);
        QTest::qWait(60);
        // Scan now opens a full camera page. Return from it before inspecting
        // the underlying navigation bar's presented pixels.
        if (auto *scanner = window.findChild<QDialog *>(QStringLiteral("qrScanDialog")))
            scanner->reject();
        bar->update(QRegion(bar->tabRect(0)) | QRegion(bar->tabRect(4)));
        image = navigation_test::presentedNavigation(
            *bar, QStringLiteral("map-tab-%1").arg(index));
        missing = navigation_test::missingNavigationContent(*bar, image);
        QVERIFY2(missing.isEmpty(), qPrintable(missing));
        tabs->setCurrentIndex(0);
        QTRY_VERIFY(plus->isVisible());
        QTest::mouseClick(plus, Qt::LeftButton);
    }
}

void NavigationTests::startupPreloadReusesMapForFirstRoute()
{
    ScriptServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MockChargingApi api;
    DeferredMap service;
    service.preloadUrl = server.url();
    MainWindow window(api, service);
    window.show();

    QTRY_COMPARE_WITH_TIMEOUT(server.sdkRequestCount, 2, 10000);
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
             page->findChild<RouteMapView *>(QStringLiteral("routeMapCanvas")));
    QTRY_COMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
    page->showRouteResult(realRoute(server.url()));
    auto *plus = page->findChild<QPushButton *>(QStringLiteral("mapZoomInButton"));
    QTRY_VERIFY_WITH_TIMEOUT(plus->isEnabled(), 10000);
    QCOMPARE(page->findChild<QWebEngineView *>(QStringLiteral("routeWebView")), view);
    QCOMPARE(loads.count(), 0);
    // Home uses a second canvas; the navigation canvas still reuses its preload.
    QTRY_COMPARE(server.sdkRequestCount, 2);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);

    auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    tabs->setCurrentIndex(3);
    QTRY_COMPARE(view->page()->lifecycleState(),
                 QWebEnginePage::LifecycleState::Frozen);
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(view->page()->lifecycleState(),
                 QWebEnginePage::LifecycleState::Active);
    QVERIFY(page->findChild<QWidget *>(
                QStringLiteral("stationNavigationPage"))->isVisible());
    QVERIFY(plus->isEnabled());
    QCOMPARE(page->findChild<QWebEngineView *>(QStringLiteral("routeWebView")),
             view);
    QCOMPARE(loads.count(), 0);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
}

void NavigationTests::startupPreloadsHomeBeforeLogin()
{
    ScriptServer server;
    server.script.prepend("window.tileDelay = 2000;\n"); // Realistic slow tiles, independent of SDK readiness.
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MockChargingApi api;
    DeferredMap service;
    service.preloadUrl = server.url();
    MainWindow window(api, service);
    auto *map = window.findChild<StationMapView *>();
    QSignalSpy stations(&api, &IChargingApi::stationListCompleted);
    QSignalSpy orders(&api, &IChargingApi::currentOrderCompleted);
    QElapsedTimer startup;
    startup.start();
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    phone->setFocus();
    QTest::keyClicks(phone, QStringLiteral("13800000001"));
    QTRY_VERIFY_WITH_TIMEOUT(map->isReady(), 4000);
    const qint64 warmMs = startup.elapsed();
    auto *view = map->findChild<QWebEngineView *>(QStringLiteral("stationWebView"));
    QVERIFY(view);
    QVERIFY(!view->page()->profile()->isOffTheRecord());
    QCOMPARE(view->page()->profile()->httpCacheType(), QWebEngineProfile::DiskHttpCache);
    QVERIFY(!map->isVisible());
    QCOMPARE(stations.count(), 0);
    QCOMPARE(orders.count(), 0);
    QCOMPARE(QApplication::focusWidget(), phone);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
    const QString initialSize = evaluate(view, QStringLiteral("JSON.stringify(initialViewport)")).toString();
    QVERIFY2(evaluate(view, QStringLiteral("initialViewport.width >= 320 && initialViewport.height >= 300")).toBool(),
        qPrintable(QStringLiteral("JS %1; view %2×%3; map %4×%5")
            .arg(initialSize).arg(view->width()).arg(view->height()).arg(map->width()).arg(map->height())));
    QVERIFY(evaluate(view, QStringLiteral("sdkLayers[2].geometries.length === 0 && sdkLayers[3].geometries.length === 0")).toBool());
    QVERIFY(evaluate(view, QStringLiteral("fittedBounds.points.length >= 2")).toBool());
    QSignalSpy loads(view, &QWebEngineView::loadStarted);
    // Simulate the available account-entry interval, without waiting on a
    // blocking constructor / a timed JavaScript readiness poll in production.
    QTest::qWait(qMax(0, 3200 - static_cast<int>(startup.elapsed())));
    QTRY_COMPARE(server.sdkRequestCount, 2);
    const int fitsBeforeLogin = evaluate(view, QStringLiteral("sdkCounts.fits")).toInt();
    window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"))->setText(QStringLiteral("123456"));
    QElapsedTimer transition;
    transition.start();
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(map->isVisible() && map->isReady() && stations.count() == 1, 500);
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view, QStringLiteral("sdkLayers[2].geometries.length === 2")).toBool(), 500);
    const qint64 visibleMs = transition.elapsed();
    qInfo("Home WebEngine + SDK test double warmed during login: %lld ms; login to reused map + markers: %lld ms",
        static_cast<long long>(warmMs), static_cast<long long>(visibleMs));
    QVERIFY(visibleMs < 500);
    QCOMPARE(map->findChild<QWebEngineView *>(QStringLiteral("stationWebView")), view);
    QCOMPARE(loads.count(), 0);
    QCOMPARE(server.sdkRequestCount, 2);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
    QTest::qWait(250); // Include delayed renderer resize/layout events in the assertion.
    QCOMPARE(window.size(), QSize(480, 860));
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.fits")).toInt(), fitsBeforeLogin);
    QCOMPARE(evaluate(view, QStringLiteral("fitOptions.ease.duration")).toInt(), 0);
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("stationMapStatus"))->isVisible());
}

void NavigationTests::slowTilesKeepPreviewUntilReady()
{
    ScriptServer server;
    server.script.prepend("window.holdTiles = true;\n");
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MockChargingApi api;
    DeferredMap service;
    service.preloadUrl = server.url();
    MainWindow window(api, service);
    window.show();
    auto *map = window.findChild<StationMapView *>();
    auto *canvas = map->findChild<RouteMapView *>();
    auto *preview = map->findChild<QWidget *>(QStringLiteral("stationLoadingPreview"));
    QTRY_VERIFY_WITH_TIMEOUT(canvas->isReady(), 4000);
    QVERIFY(!map->isReady()); // SDK/map existence must not claim tile readiness.
    QCOMPARE(server.sdkRequestCount, 1); // Navigation does not compete with the cold home.
    login(window);
    QTRY_VERIFY(preview->isVisible());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("stationMapMode"))->text().contains(QStringLiteral("离线预览")));
    auto *view = map->findChild<QWebEngineView *>();
    QSignalSpy loads(view, &QWebEngineView::loadStarted);
    evaluate(view, QStringLiteral("sdkMap.events.tilesloaded()"));
    QTRY_VERIFY(map->isReady());
    QTRY_VERIFY(!preview->isVisible());
    QCOMPARE(loads.count(), 0);
    QCOMPARE(map->findChild<QWebEngineView *>(), view);
    canvas->retry();
    QTRY_VERIFY(canvas->isReady() && !canvas->isBaseMapReady());
    auto *tileTimeout = canvas->findChild<QTimer *>(QStringLiteral("stationTileTimeout"));
    QVERIFY(tileTimeout && tileTimeout->isActive());
    QCOMPARE(tileTimeout->interval(), 15000);
    // Exercise the expiry slot without adding a second 15-second sleep.
    QVERIFY(QMetaObject::invokeMethod(tileTimeout, "timeout", Qt::DirectConnection));
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("stationMapRetry"))->isVisible());
    QVERIFY(preview->isVisible());
    QVERIFY(!map->isReady());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("stationMapStatus"))->text().contains(QStringLiteral("加载超时")));
}

void NavigationTests::sdkHttpCacheIsShared()
{
    ScriptServer server;
    server.cacheable = true;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    RouteMapView first, second;
    first.preload(server.url());
    QTRY_VERIFY_WITH_TIMEOUT(first.isPreloaded(), 4000);
    QCOMPARE(server.sdkRequestCount, 1);
    second.preload(server.url());
    QTRY_VERIFY_WITH_TIMEOUT(second.isPreloaded(), 4000);
    QCOMPARE(server.sdkRequestCount, 1); // Second canvas actually hits HTTP cache.
    QCOMPARE(first.findChild<QWebEngineView *>()->page()->profile(),
             second.findChild<QWebEngineView *>()->page()->profile());
}

void NavigationTests::failedHomePreloadRetriesAfterLogin()
{
    ScriptServer server;
    const QByteArray working = server.script;
    server.script = "/* Unavailable SDK in background */";
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MockChargingApi api;
    DeferredMap service;
    service.preloadUrl = server.url();
    MainWindow window(api, service);
    window.show();
    auto *map = window.findChild<StationMapView *>();
    QSignalSpy stations(&api, &IChargingApi::stationListCompleted);
    QTRY_VERIFY_WITH_TIMEOUT(server.sdkRequestCount >= 1, 4000);
    QTRY_VERIFY_WITH_TIMEOUT(!map->findChild<QWebEngineView *>(), 4000);
    QVERIFY(!map->isReady());
    QCOMPARE(stations.count(), 0);
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("stationMapStatus"))->isVisible());
    server.script = working;
    login(window);
    QTRY_VERIFY_WITH_TIMEOUT(map->isReady(), 4000);
    QVERIFY(map->findChild<QWebEngineView *>());
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("stationMapStatus"))->isVisible());
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
    // Offline geometry is prepared asynchronously; VM load need not fit an
    // arbitrary 400 ms sleep. Wait for readiness before checking no web view.
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<StationMapView *>()->isReady(), 5000);
    QVERIFY(!window.findChild<QWebEngineView *>(QStringLiteral("routeWebView")));
    QVERIFY(!window.findChild<QWebEngineView *>(QStringLiteral("stationWebView")));
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
    auto *canvas = page.findChild<RouteMapView *>(QStringLiteral("routeMapCanvas"));
    QVERIFY(canvas->height() >= 120);
    QTRY_COMPARE(evaluate(view, QStringLiteral("document.getElementById('map').clientWidth")).toInt(), view->width());
}

void NavigationTests::failedSdkReleasesBusyState()
{
    ScriptServer server;
    const QByteArray workingScript = server.script;
    server.script = "/* SDK failed: TMap is unavailable */";
    QVERIFY(server.listen(QHostAddress::LocalHost));
    StationBrowserPage page;
    page.show();
    page.showNavigation(station(), {QStringLiteral("起点"), 123.4, 41.79});
    page.showRouteResult(realRoute(server.url()));
    auto *plan = page.findChild<QPushButton *>(QStringLiteral("routePlanButton"));
    QTRY_VERIFY_WITH_TIMEOUT(plan->isEnabled(), 10000);
    const QString message =
        page.findChild<QLabel *>(QStringLiteral("routeMessage"))->text();
    QVERIFY(message.contains(QStringLiteral("SDK 未提供可用接口")));
    QVERIFY(message.contains(QStringLiteral("权限、配额")));
    QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled());
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("routeDetailsButton"))->isEnabled());
    auto *retry = page.findChild<QPushButton *>(QStringLiteral("mapRetryButton"));
    QVERIFY(retry->isVisible());
    QVERIFY(retry->isEnabled());
    server.script = workingScript;
    retry->click();
    QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled(), 10000);
    QCOMPARE(server.sdkRequestCount, 2);
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
    const QString message =
        page.findChild<QLabel *>(QStringLiteral("routeMessage"))->text();
    QVERIFY2(message.contains(QStringLiteral("加载超时（15 秒）")), qPrintable(message));
    QVERIFY(message.contains(QStringLiteral("网络较慢")));
    QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("mapZoomInButton"))->isEnabled());
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("mapRetryButton"))->isEnabled());
}
#endif

#ifdef CHARGING_CLIENT_HAS_WEBENGINE
void NavigationTests::stationMarkersAndBridgeUseSharedCanvas()
{
    ScriptServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    DeferredMap service;
    service.preloadUrl = server.url();
    MockChargingApi api;
    MainWindow window(api, service);
    window.resize(360, 640);
    window.show();
    login(window);
    auto *map = window.findChild<StationMapView *>();
    auto *canvas = window.findChild<RouteMapView *>(QStringLiteral("stationWebMapCanvas"));
    QTRY_VERIFY(window.findChild<QWebEngineView *>(QStringLiteral("stationWebView")));
    auto *view = window.findChild<QWebEngineView *>(QStringLiteral("stationWebView"));
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view, QStringLiteral("!!(window.sdkLayers && sdkLayers.length === 4 && sdkLayers[2].geometries.length === 2)")).toBool(), 10000);
    QCOMPARE(evaluate(view, QStringLiteral("sdkLayers[3].geometries[0].id")).toString(), QStringLiteral("current"));
    QCOMPARE(evaluate(view, QStringLiteral("sdkLayers[2].geometries[0].position.lat")).toDouble(), 41.71);
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
    QVERIFY(!window.findChild<QAbstractButton *>(QStringLiteral("stationMarker_1")));
    QSignalSpy selected(map, &StationMapView::stationSelected);
    QSignalSpy details(&api, &IChargingApi::stationDetailCompleted);
    // Marker/map events may arrive for the same click, in either order.
    evaluate(view, QStringLiteral("sdkMap.events.click({}); sdkLayers[2].events.click({geometry:{id:'1'}})"));
    QTRY_COMPARE(map->selectedStationId(), 1);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationPreviewCard"))->isVisible());
    QTRY_COMPARE(evaluate(view, QStringLiteral("sdkLayers[2].geometries[0].styleId")).toString(), QStringLiteral("selected"));
    QCOMPARE(details.size(), 0);
    evaluate(view, QStringLiteral("sdkLayers[2].events.click({geometry:{id:'2'}}); sdkMap.events.click({})"));
    QTRY_COMPARE(map->selectedStationId(), 2);
    QTRY_COMPARE(selected.size(), 2);
    evaluate(view, QStringLiteral("sdkMap.events.click({})"));
    QTRY_COMPARE(map->selectedStationId(), 0);
    QVERIFY(!window.findChild<QWidget *>(QStringLiteral("stationPreviewCard"))->isVisible());
    window.findChild<QLineEdit *>(QStringLiteral("stationKeywordInput"))->setText(QStringLiteral("和平"));
    window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"))->click();
    QTRY_COMPARE(evaluate(view, QStringLiteral("sdkLayers[2].geometries.length")).toInt(), 1);
    QCOMPARE(evaluate(view, QStringLiteral("sdkLayers[2].geometries[0].id")).toString(), QStringLiteral("2"));
    QVERIFY(evaluate(view, QStringLiteral("fittedBounds.points.length === 2")).toBool());
    map->zoomIn();
    QTRY_COMPARE(evaluate(view, QStringLiteral("sdkCounts.zoom")).toInt(), 13);
    window.findChild<QPushButton *>(QStringLiteral("stationMapLocate"))->click();
    QTRY_VERIFY(evaluate(view, QStringLiteral("!!window.lastCenter")).toBool());
    auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    tabs->setCurrentIndex(1);
    QTRY_COMPARE(view->page()->lifecycleState(), QWebEnginePage::LifecycleState::Frozen);
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(view->page()->lifecycleState(), QWebEnginePage::LifecycleState::Active);
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"))->isEnabled());
    QCOMPARE(evaluate(view, QStringLiteral("sdkCounts.initializations")).toInt(), 1);
    const QByteArray workingSdk = server.script;
    server.script = "/* SDK unavailable */";
    canvas->retry();
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QPushButton *>(QStringLiteral("stationMapRetry"))->isVisible(), 10000);
    server.script = workingSdk;
    window.findChild<QPushButton *>(QStringLiteral("stationMapRetry"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QWebEngineView *>(QStringLiteral("stationWebView")), 10000);
    view = window.findChild<QWebEngineView *>(QStringLiteral("stationWebView"));
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view, QStringLiteral("!!(window.sdkLayers && sdkLayers[2] && sdkLayers[2].geometries.length === 1)")).toBool(), 10000);
    QCOMPARE(evaluate(view, QStringLiteral("sdkLayers[2].geometries[0].id")).toString(), QStringLiteral("2"));
    QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("stationMapRetry"))->isVisible());
}
#endif

QTEST_MAIN(NavigationTests)
#include "navigation_tests.moc"
