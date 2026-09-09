// 本文件测试离线站点地图首页：预热、手势、筛选与发现列表排序
#include "api/mock_charging_api.h"
#include "ui/main_window.h"
#include "ui/station_browser_page.h"
#include "ui/station_map_view.h"
#include "ui/station_discovery_policy.h"
#include <limits>

#include <QAbstractButton>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPainter>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QSettings>
#include <QStackedWidget>
#include <QScrollBar>
#include <QScopeGuard>
#include <QtTest>
#include <cmath>

using namespace charging::client;

// 站点地图相关测试集合
class StationMapTests final : public QObject {
    Q_OBJECT
private slots:
    void offlineHomePreloadsDuringLogin();
    void offlineBackdropCachesViewport();
    void closingDuringPreloadIsSafe();
    void mapHomeFitsAndSelects_data();
    void mapHomeFitsAndSelects();
    void filteringClearsSelectionAndRetainsMap();
    void mockPanZoomAndRecenter();
    void unavailableAndMissingPrediction();
    void compactOrderAndFiltersKeepMapUsable();
    void discoverySheetDragAndSearchReturn();
    void searchHistoryIsPerUserAndVisitsUseCharging();
    void mapGesturesAndDiscoverySelection();
    void discoveryRankingLimitsAvailability();
};

namespace {
// 事件过滤器，统计控件的重绘次数
class PaintProbe final : public QObject {
public:
    int paints = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) ++paints;
        return false;
    }
};

// 登录并等待地图与站点标记就绪
void login(MainWindow &window)
{
    window.show();
    window.findChild<QLineEdit *>("phoneInput")->setText(QStringLiteral("13800000001"));
    window.findChild<QLineEdit *>("verificationCodeInput")->setText(QStringLiteral("123456"));
    window.findChild<QPushButton *>("loginButton")->click();
    QTRY_VERIFY(window.findChild<QAbstractButton *>("stationMarker_2"));
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<StationMapView *>()->isReady(), 2000);
}

// 设置环境变量时保存界面截图
void screenshot(MainWindow &window, const QString &name)
{
    const QString directory = qEnvironmentVariable("BIT_CLIENT_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    QVERIFY(window.grab().save(directory + QLatin1Char('/') + name + QStringLiteral(".png")));
}
}

// 登录输入期间预热离线首页地图，且不阻塞界面事件循环
void StationMapTests::offlineHomePreloadsDuringLogin()
{
    MockChargingApi api;
    MainWindow window(api);
    auto *map = window.findChild<StationMapView *>();
    auto *phone = window.findChild<QLineEdit *>("phoneInput");
    QVERIFY(map);
    QSignalSpy stations(&api, &IChargingApi::stationListCompleted);
    QSignalSpy orders(&api, &IChargingApi::currentOrderCompleted);
    QSignalSpy ready(map, &StationMapView::mapReady);
    QElapsedTimer startup;
    startup.start();
    QElapsedTimer heartbeatGap;
    heartbeatGap.start();
    qint64 longestGap = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    connect(&heartbeat, &QTimer::timeout, &window, [&] {
        longestGap = qMax(longestGap, heartbeatGap.restart());
    });
    heartbeat.start();
    window.show();
    phone->setFocus();
    QTest::keyClicks(phone, QStringLiteral("13800000001"));
    QTRY_VERIFY_WITH_TIMEOUT(map->isReady(), 1000);
    const qint64 warmMs = startup.elapsed();
    QTest::qWait(25);
    QVERIFY2(longestGap < 250, "Default-map preparation blocked the GUI event loop");
    QVERIFY(!map->isVisible());
    QCOMPARE(ready.count(), 1);
    QCOMPARE(stations.count(), 0);
    QCOMPARE(orders.count(), 0);
    QCOMPARE(QApplication::focusWidget(), phone);
    QCOMPARE(phone->text(), QStringLiteral("13800000001"));

    PaintProbe painted;
    map->installEventFilter(&painted);
    window.findChild<QLineEdit *>("verificationCodeInput")->setText(QStringLiteral("123456"));
    QElapsedTimer transition;
    transition.start();
    window.findChild<QPushButton *>("loginButton")->click();
    QTRY_VERIFY_WITH_TIMEOUT(map->isVisible() && painted.paints > 0
        && window.findChild<QAbstractButton *>("stationMarker_2"), 500);
    const qint64 firstPaintMs = transition.elapsed();
    qInfo("Offline home ready during login: %lld ms; login to painted map + markers: %lld ms",
        static_cast<long long>(warmMs), static_cast<long long>(firstPaintMs));
    QVERIFY(firstPaintMs < 500);
    QVERIFY(map->isReady());
}

// 离线底图按视口缓存，同参数绘制结果一致
void StationMapTests::offlineBackdropCachesViewport()
{
    DemoMapBackdrop backdrop;
    const double pi = std::acos(-1.0);
    const QPointF center((123.42 + 180) / 360,
        (1 - std::asinh(std::tan(41.75 * pi / 180)) / pi) / 2);
    const QSize size(480, 750);
    QElapsedTimer timer;
    timer.start();
    backdrop.prepare(size, center, 1600000, 1);
    const qint64 coldMs = timer.elapsed();
    QVERIFY(backdrop.isReady());
    QVERIFY(backdrop.featureCount() > 1000); // Actual bundled API extract, not a procedural grid.
    timer.restart();
    backdrop.prepare(size, center, 1600000, 1);
    qInfo("Offline backdrop preparation: %lld ms; cached viewport: %lld ms",
        static_cast<long long>(coldMs), static_cast<long long>(timer.elapsed()));
    QImage first(size, QImage::Format_RGB32), second(size, QImage::Format_RGB32);
    {
        QPainter painter(&first);
        backdrop.paint(painter, size, center, 1600000, 1);
    }
    {
        QPainter painter(&second);
        backdrop.paint(painter, size, center, 1600000, 1);
    }
    QCOMPARE(first, second);
    {
        QPainter painter(&second);
        backdrop.paint(painter, size, center + QPointF(.0001, .0001), 1600000, 1);
    }
    QVERIFY(first != second);
}

// 预加载途中销毁视图不应崩溃
void StationMapTests::closingDuringPreloadIsSafe()
{
    {
        StationMapView map;
        map.resize(480, 750);
        map.preload();
        QVERIFY(!map.isReady());
    } // Joins the data/image-only worker; queued replies cannot reach a deleted widget.
    QCoreApplication::processEvents();
}

// 为不同窗口尺寸和高峰/平时时段准备用例
void StationMapTests::mapHomeFitsAndSelects_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<QDateTime>("now");
    QTest::addColumn<QString>("expectedPrice");
    const auto regular = QDateTime::fromString(QStringLiteral("2026-09-08T04:00:00Z"), Qt::ISODate);
    const auto peak = QDateTime::fromString(QStringLiteral("2026-09-08T02:59:00Z"), Qt::ISODate);
    QTest::newRow("default") << QSize(480, 860) << regular << QStringLiteral("1.35");
    QTest::newRow("small") << QSize(360, 640) << regular << QStringLiteral("1.35");
    QTest::newRow("wide") << QSize(900, 640) << regular << QStringLiteral("1.35");
    QTest::newRow("peak-default") << QSize(480, 860) << peak << QStringLiteral("1.62");
    QTest::newRow("peak-small") << QSize(360, 640) << peak << QStringLiteral("1.62");
    QTest::newRow("peak-wide") << QSize(900, 640) << peak << QStringLiteral("1.62");
}

// 首页地图自适应尺寸，选中标记只显示预览不拉详情
void StationMapTests::mapHomeFitsAndSelects()
{
    QFETCH(QSize, size);
    QFETCH(QDateTime, now);
    QFETCH(QString, expectedPrice);
    MockChargingApi api(nullptr, [now] { return now; });
    MainWindow window(api);
    window.resize(size);
    login(window);
    auto *map = window.findChild<StationMapView *>();
    auto *home = window.findChild<QWidget *>("stationListPage");
    auto *preview = window.findChild<QWidget *>("stationPreviewCard");
    auto *overlay = window.findChild<QWidget *>("stationHomeOverlay");
    auto *tabs = window.findChild<QTabWidget *>("mainNavigation");
    QVERIFY(map && preview && home && overlay);
    QCOMPARE(tabs->count(), 5);
    QCOMPARE(window.size(), size);
    QVERIFY(!window.findChild<QScrollArea *>("stationHomeScrollArea"));
    QTRY_VERIFY(map->height() >= home->height() * .8);
    QVERIFY(!preview->isVisible());
    QVERIFY(overlay->height() > home->height() * .35);
    QVERIFY(overlay->height() < home->height() * .55);
    auto *one = window.findChild<QAbstractButton *>("stationMarker_1");
    auto *two = window.findChild<QAbstractButton *>("stationMarker_2");
    QTRY_VERIFY(map->usableViewport().contains(one->geometry().center()));
    QVERIFY(map->usableViewport().contains(two->geometry().center()));
    QVERIFY(map->usableViewport().contains(map->pointForLocation({{}, 123.42, 41.70})));
    screenshot(window, QStringLiteral("home-%1").arg(QString::fromLatin1(QTest::currentDataTag())));

    QSignalSpy details(&api, &IChargingApi::stationDetailCompleted);
    QTest::mouseClick(one, Qt::LeftButton);
    QTRY_VERIFY(preview->isVisible());
    QCOMPARE(details.size(), 0); // Selecting never fetches or opens the detail page.
    QCOMPARE(map->selectedStationId(), 1);
    QVERIFY(one->isChecked());
    QVERIFY(!two->isChecked());
    QCOMPARE(window.findChild<QLabel *>("stationPreviewName")->text(), QStringLiteral("浑南演示充电站"));
    QVERIFY(window.findChild<QLabel *>("stationPreviewMetrics")->text().contains(
        QStringLiteral("1/2 空闲 · ¥%1/度").arg(expectedPrice)));
    QVERIFY(QLineF(QPointF(one->pos()) + QPointF(26,22), map->usableViewport().center()).length() < 2);
    QVERIFY(!map->usableViewport().contains(two->geometry().center()));
    QVERIFY(preview->geometry().bottom() < home->height());
    QVERIFY(overlay->isAncestorOf(preview));
    screenshot(window, QStringLiteral("selected-%1").arg(QString::fromLatin1(QTest::currentDataTag())));

    two->setFocus();
    QTest::keyClick(two, Qt::Key_Space);
    QCOMPARE(map->selectedStationId(), 2);
    QVERIFY(two->isChecked());
    QVERIFY(!one->isChecked());
    QCOMPARE(window.findChild<QLabel *>("stationPreviewName")->text(), QStringLiteral("和平演示充电站"));
    QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, QPoint(40, home->height() / 2));
    QVERIFY(!preview->isVisible());
    QCOMPARE(map->selectedStationId(), 2);
    QVERIFY(!overlay->isVisible());
    QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, QPoint(40, 100));
    QVERIFY(preview->isVisible());
    QTest::mouseClick(one, Qt::LeftButton);
    window.findChild<QPushButton *>("stationPreviewDetailsButton")->click();
    QTRY_COMPARE(details.size(), 1);
    QTRY_VERIFY(window.findChild<QWidget *>("stationDetailPage")->isVisible());
}

// 筛选后清除选中但保留地图，无结果时给出提示
void StationMapTests::filteringClearsSelectionAndRetainsMap()
{
    MockChargingApi api;
    MainWindow window(api);
    login(window);
    auto *map = window.findChild<StationMapView *>();
    auto *keyword = window.findChild<QLineEdit *>("stationKeywordInput");
    auto *search = window.findChild<QPushButton *>("stationRefreshButton");
    auto *preview = window.findChild<QWidget *>("stationPreviewCard");
    window.findChild<QAbstractButton *>("stationMarker_1")->click();
    keyword->setText(QStringLiteral("和平"));
    QTest::keyClick(keyword, Qt::Key_Return);
    QTRY_VERIFY(search->isEnabled());
    QVERIFY(!window.findChild<QAbstractButton *>("stationMarker_1"));
    QVERIFY(window.findChild<QAbstractButton *>("stationMarker_2"));
    QVERIFY(!preview->isVisible());
    QVERIFY(map->isVisible());
    QVERIFY(window.findChild<StationBrowserPage *>()->stationQuery().longitude.has_value());
    window.findChild<QAbstractButton *>("stationMarker_2")->click();
    QVERIFY(!window.findChild<QLabel *>("stationPreviewMetrics")->text().contains(QStringLiteral("距离待定位")));
    keyword->setText(QStringLiteral("不存在的站点"));
    search->click();
    QTRY_VERIFY(search->isEnabled());
    QVERIFY(!preview->isVisible());
    QVERIFY(map->isVisible());
    QCOMPARE(window.findChild<QLabel *>("stationListMessage")->text(), QStringLiteral("没有找到符合条件的充电站"));
    screenshot(window, QStringLiteral("no-results"));
    keyword->clear(); search->click();
    QTRY_VERIFY(window.findChild<QAbstractButton *>("stationMarker_1"));
    QVERIFY(!window.findChild<QLabel *>("stationListMessage")->isVisible());
}

// 离线地图的缩放、拖拽与回到当前位置
void StationMapTests::mockPanZoomAndRecenter()
{
    StationMapView map;
    map.resize(480, 750);
    map.show();
    const MapLocation current{QStringLiteral("演示位置"), 123.42, 41.70};
    map.setCurrentLocation(current);
    map.fitStations();
    const MapLocation other{{}, 123.43, 41.71};
    const double before = QLineF(map.pointForLocation(current), map.pointForLocation(other)).length();
    map.zoomIn();
    QVERIFY(QLineF(map.pointForLocation(current), map.pointForLocation(other)).length() > before);
    map.zoomOut();
    QVERIFY(qAbs(QLineF(map.pointForLocation(current), map.pointForLocation(other)).length() - before) < .01);
    const QPointF oldPoint = map.pointForLocation(current);
    QTest::mousePress(&map, Qt::LeftButton, Qt::NoModifier, QPoint(200, 300));
    QMouseEvent move(QEvent::MouseMove, QPointF(240, 330), QPointF(240, 330), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&map, &move);
    QTest::mouseRelease(&map, Qt::LeftButton, Qt::NoModifier, QPoint(240, 330));
    QVERIFY(QLineF(oldPoint, map.pointForLocation(current)).length() > 40);
    map.findChild<QPushButton *>("stationMapLocate")->click();
    QVERIFY(QLineF(map.usableViewport().center(), map.pointForLocation(current)).length() < .01);
    map.setCurrentLocation(std::nullopt);
    QVERIFY(!map.findChild<QPushButton *>("stationMapLocate")->isEnabled());
}

// 无空闲桩且缺预测数据时的显示与纯文本转义
void StationMapTests::unavailableAndMissingPrediction()
{
    StationBrowserPage page;
    page.resize(480, 750);
    page.show();
    charging::protocol::StationDto station;
    station.stationId = 42;
    station.name = QStringLiteral("<b>站点名称按纯文本显示</b>");
    station.longitude = 123.42; station.latitude = 41.71;
    station.availablePileCount = 0; station.totalPileCount = 2;
    station.priceCentsPerKwh = 135;
    page.showStations({station});
    page.findChild<QAbstractButton *>("stationMarker_42")->click();
    QCOMPARE(page.findChild<QLabel *>("stationPreviewName")->textFormat(), Qt::PlainText);
    QVERIFY(page.findChild<QLabel *>("stationPreviewMetrics")->text().contains(QStringLiteral("0/2 空闲")));
    QCOMPARE(page.findChild<QLabel *>("stationPreviewPrediction")->text(), QStringLiteral("拥堵预测暂不可用"));
    page.reset();
    QVERIFY(!page.findChild<QAbstractButton *>("stationMarker_42"));
    QVERIFY(!page.findChild<QWidget *>("stationPreviewCard")->isVisible());
}

// 小屏下当前订单卡片展开不挤压地图可用区域
void StationMapTests::compactOrderAndFiltersKeepMapUsable()
{
    MockChargingApi api;
    MainWindow window(api);
    window.resize(360, 640);
    login(window);
    QSignalSpy reserved(&api, &IChargingApi::reservationCompleted);
    QVERIFY(!api.reserve(QStringLiteral("PILE-A-01")).isEmpty());
    QTRY_COMPARE(reserved.size(), 1);
    window.findChild<QPushButton *>("stationRefreshButton")->click();
    auto *order = window.findChild<QWidget *>("currentOrderCard");
    auto *toggle = window.findChild<QPushButton *>("currentOrderToggle");
    auto *actions = window.findChild<QWidget *>("currentOrderDetails");
    auto *map = window.findChild<StationMapView *>();
    QTRY_VERIFY(order->isVisible());
    QVERIFY(!actions->isVisible());
    QTRY_VERIFY(order->height() <= 52);
    const int height = map->height();
    screenshot(window, QStringLiteral("current-order-small"));
    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(actions->isVisible());
    QVERIFY(window.findChild<QPushButton *>("cancelReservationButton")->isVisible());
    QVERIFY(window.findChild<QPushButton *>("startReservedChargingButton")->isVisible());
    QCOMPARE(map->height(), height);
    QVERIFY(map->usableViewport().height() >= 150);
    screenshot(window, QStringLiteral("order-expanded-small"));
    QTest::mouseClick(window.findChild<QAbstractButton *>("stationMarker_1"), Qt::LeftButton);
    QVERIFY(!actions->isVisible());
    auto *preview = window.findChild<QWidget *>("stationPreviewCard");
    QVERIFY(preview->isVisible());
    auto *filter = window.findChild<QPushButton *>("stationLocationEntry");
    filter->click();
    QVERIFY(!preview->isVisible());
    QCOMPARE(map->selectedStationId(), 1);
    auto *scroll = window.findChild<QScrollArea *>("stationLocationScrollArea");
    QVERIFY(scroll->isVisible());
    QTRY_VERIFY(scroll->widget()->width() <= scroll->viewport()->width());
    QVERIFY(window.findChild<QWidget *>("stationLocationPage")->isVisible());
    screenshot(window, QStringLiteral("filters-small"));
    window.findChild<QPushButton *>("stationLocationBack")->click();
    QVERIFY(preview->isVisible());
    QCOMPARE(map->height(), height);
}

// 发现面板可拖拽折叠，搜索返回后保留关键词
void StationMapTests::discoverySheetDragAndSearchReturn()
{
    MockChargingApi api;
    MainWindow window(api);
    window.resize(360, 640);
    login(window);
    auto *page = window.findChild<StationBrowserPage *>();
    auto *map = window.findChild<StationMapView *>();
    auto *sheet = window.findChild<QWidget *>("stationHomeOverlay");
    auto *handle = window.findChild<QPushButton *>("stationSheetHandle");
    auto *entry = window.findChild<QPushButton *>("stationSearchEntry");
    auto *content = window.findChild<QStackedWidget *>("stationSheetPages");
    const int initialHeight = sheet->height();
    const int mapHeight = map->height();
    const QPoint from = handle->rect().center();
    const QPoint global = handle->mapToGlobal(from);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent down(QEvent::MouseMove, QPointF(from + QPoint(0, 140)), QPointF(global + QPoint(0, 140)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(handle, &down);
    QTest::mouseRelease(handle, Qt::LeftButton);
    QVERIFY(sheet->height() < initialHeight);
    QVERIFY(!content->isVisible());
    QVERIFY(entry->isVisible());
    QCOMPARE(map->height(), mapHeight);
    screenshot(window, "sheet-collapsed");
    entry->click();
    QVERIFY(window.findChild<QWidget *>("stationSearchPage")->isVisible());
    auto *keyword = window.findChild<QLineEdit *>("stationKeywordInput");
    keyword->setText(QStringLiteral("和平"));
    window.findChild<QPushButton *>("stationRefreshButton")->click();
    QTRY_VERIFY(window.findChild<QPushButton *>("discoveryStation_search_results_2")
        && window.findChild<QPushButton *>("discoveryStation_search_results_2")->isVisible());
    auto *result = window.findChild<QPushButton *>("discoveryStation_search_results_2");
    result->click();
    QCOMPARE(map->selectedStationId(), 2);
    QVERIFY(window.findChild<QWidget *>("stationPreviewCard")->isVisible());
    QVERIFY(content->isVisible());
    window.findChild<QPushButton *>("stationPreviewClose")->click();
    QCOMPARE(page->stationQuery().keyword, QStringLiteral("和平"));
    QCOMPARE(map->selectedStationId(), 0);
    page->setSheetPosition(2);
    QVERIFY(sheet->height() > initialHeight);
    screenshot(window, "sheet-expanded");
    entry->click();
    keyword->clear();
    QTRY_VERIFY(window.findChild<QPushButton *>("discoveryStation_search_nearby_1")
        && window.findChild<QPushButton *>("discoveryStation_search_nearby_1")->isVisible());
    screenshot(window, "search-discovery");
    window.findChild<QPushButton *>("stationSearchBack")->click();
    QCOMPARE(page->stationQuery().keyword, QStringLiteral("和平"));
    QCOMPARE(map->height(), mapHeight);
    QVERIFY(window.findChild<QPushButton *>("stationMapOverview"));
    QVERIFY(window.findChild<QPushButton *>("stationMapLocate"));
}

// 搜索历史按用户隔离，只有实际充电过才算访问记录
void StationMapTests::searchHistoryIsPerUserAndVisitsUseCharging()
{
    const qint64 userA = 987654321, userB = 987654322;
    auto clean = qScopeGuard([=] {
        QSettings().remove(QStringLiteral("stationDiscovery/users/%1").arg(userA));
        QSettings().remove(QStringLiteral("stationDiscovery/users/%1").arg(userB));
    });
    StationBrowserPage page;
    page.resize(360, 600); page.show();
    page.setUserId(userA);
    auto *entry = page.findChild<QPushButton *>("stationSearchEntry");
    auto *input = page.findChild<QLineEdit *>("stationKeywordInput");
    auto *search = page.findChild<QPushButton *>("stationRefreshButton");
    entry->click();
    for (int i = 0; i < 10; ++i) { input->setText(QStringLiteral("站点%1").arg(i)); search->click(); }
    input->setText(QStringLiteral("站点5")); search->click();
    auto terms = QSettings().value(QStringLiteral("stationDiscovery/users/%1/searches").arg(userA)).toStringList();
    QCOMPARE(terms.size(), 8);
    QCOMPARE(terms.first(), QStringLiteral("站点5"));
    page.reset(); page.setUserId(userB); entry->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(page.findChildren<QPushButton *>("stationHistoryTerm").isEmpty());
    page.reset(); page.setUserId(userA); entry->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(page.findChildren<QPushButton *>("stationHistoryTerm").size(), 8);
    page.findChild<QPushButton *>("stationHistoryClear")->click();
    QVERIFY(!QSettings().contains(QStringLiteral("stationDiscovery/users/%1/searches").arg(userA)));
    charging::protocol::StationDto station;
    station.stationId = 10; station.name = QStringLiteral("历史站点"); station.longitude = 123.42; station.latitude = 41.70;
    page.showStations({station});
    charging::protocol::OrderDto reservation;
    reservation.stationId = 10; reservation.userId = userA;
    page.showVisitHistory({reservation});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!page.findChild<QPushButton *>("discoveryStation_search_visited_10"));
    reservation.startedAt = QStringLiteral("2026-09-08T00:00:00Z");
    page.showVisitHistory({reservation});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(page.findChild<QPushButton *>("discoveryStation_search_visited_10"));
}

// 地图手势进入全屏与发现列表选站的联动
void StationMapTests::mapGesturesAndDiscoverySelection()
{
    MockChargingApi api;
    MainWindow window(api);
    window.resize(480, 860);
    login(window);
    auto *page = window.findChild<StationBrowserPage *>();
    auto *map = window.findChild<StationMapView *>();
    auto *panel = window.findChild<QWidget *>("stationHomeOverlay");
    auto *content = window.findChild<QStackedWidget *>("stationSheetPages");
    auto *controls = window.findChild<QWidget *>("stationMapControls");
    QVERIFY(panel->isVisible()); QVERIFY(content->isVisible());
    QVERIFY(controls->y() < 24);
    QVERIFY(controls->x() > map->width() / 2);
    window.findChild<QPushButton *>("stationMapZoomIn")->click();
    QVERIFY(panel->isVisible()); QVERIFY(!content->isVisible());
    auto *navigation = window.findChild<QTabWidget *>("mainNavigation");
    auto *bar = navigation->tabBar();
    const QSize canvasSize = map->size();
    QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, QPoint(40, 100));
    QVERIFY(!panel->isVisible());
    QTRY_VERIFY(!bar->isVisible());
    QVERIFY(!window.findChild<QWidget *>("navigationContainer")->isVisible());
    QCOMPARE(map->size(), canvasSize);
    map->zoomOut();
    QVERIFY(!panel->isVisible()); // Fullscreen stays fullscreen during map gestures.
    QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, QPoint(40, 100));
    QVERIFY(panel->isVisible()); QVERIFY(content->isVisible());
    QTRY_VERIFY(bar->isVisible());
    QCOMPARE(map->size(), canvasSize);
    QTest::mousePress(map, Qt::LeftButton, Qt::NoModifier, QPoint(80, 120));
    QMouseEvent move(QEvent::MouseMove, QPointF(105, 140), QPointF(map->mapToGlobal(QPoint(105,140))),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(map, &move);
    QTest::mouseRelease(map, Qt::LeftButton, Qt::NoModifier, QPoint(105, 140));
    QVERIFY(panel->isVisible()); QVERIFY(!content->isVisible()); // Drag release is not a blank click.
    page->setSheetPosition(1);
    QTRY_VERIFY(window.findChild<QPushButton *>("discoveryStation_home_recommended_1"));
    window.findChild<QPushButton *>("discoveryStation_home_recommended_1")->click();
    QCOMPARE(map->selectedStationId(), 1);
    QVERIFY(content->isVisible());
    const auto *marker = window.findChild<QAbstractButton *>("stationMarker_1");
    QVERIFY(QLineF(QPointF(marker->pos()) + QPointF(26,22), map->usableViewport().center()).length() < 2);
    window.findChild<QPushButton *>("stationPreviewClose")->click();
    QTRY_VERIFY(window.findChild<QPushButton *>("discoveryStation_home_nearby_2"));
    window.findChild<QPushButton *>("discoveryStation_home_nearby_2")->click();
    QCOMPARE(map->selectedStationId(), 2);
    marker = window.findChild<QAbstractButton *>("stationMarker_2");
    QVERIFY(QLineF(QPointF(marker->pos()) + QPointF(26,22), map->usableViewport().center()).length() < 2);
    QVERIFY(panel->isVisible()); QVERIFY(content->isVisible());
    window.findChild<QPushButton *>("stationMapZoomOut")->click();
    QVERIFY(!content->isVisible());
    window.findChild<QAbstractButton *>("stationMarker_2")->click();
    QVERIFY(content->isVisible());
    screenshot(window, "gesture-selected");
    page->showDetailLoading();
    map->zoomOut();
    StationDetailPayload lateDetail;
    lateDetail.station.stationId = 2;
    page->showStationDetail(lateDetail);
    QVERIFY(panel->isVisible()); QVERIFY(!content->isVisible());
    QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, QPoint(40, 100));
    page->showDetailError(QStringLiteral("暂时无法读取详情"));
    QVERIFY(!panel->isVisible());
}

// 推荐与附近排序：排除不可用、限制距离并按价格距离比较
void StationMapTests::discoveryRankingLimitsAvailability()
{
    using namespace charging::protocol;
    auto station = [](qint64 id, double distance, qint64 idle, qint64 price) {
        StationDto s; s.stationId=id; s.name=QString::number(id); s.distanceKm=distance;
        s.availablePileCount=idle; s.totalPileCount=4; s.priceCentsPerKwh=price;
        return s;
    };
    auto busy=station(1,.1,0,0); busy.recommended=true;
    auto disabled=station(2,.2,4,0); disabled.status=StationStatus::Disabled;
    auto far=station(3,300,4,0);
    auto lessUseful=station(4,2,1,200);
    auto best=station(5,2,4,100);
    auto edge=station(8,30,1,200);
    auto missing=station(10,1,4,0); missing.distanceKm.reset();
    QList<StationDto> items{busy,disabled,far,lessUseful,best,station(6,9,1,900),
        station(7,11,2,100),edge,station(9,30.01,4,0),missing,
        station(11,std::numeric_limits<double>::quiet_NaN(),4,0),station(12,-1,4,0)};
    QCOMPARE(discovery::recommend(items), 5);
    const auto nearby=discovery::nearby(items);
    QCOMPARE(nearby.size(), 4);
    QCOMPARE(nearby[0].stationId, 5);
    QCOMPARE(nearby[3].stationId, 7); // Broaden gently when fewer than four are within 10 km.
    for (const auto &s:nearby) QVERIFY(s.availablePileCount > 0 && *s.distanceKm <= 30);
    QVERIFY(discovery::availableWithin(edge,30));
    QVERIFY(!discovery::availableWithin(items[8],30));
    QCOMPARE(discovery::recommend({items[6],edge}), 7);
    QCOMPARE(discovery::recommend({busy,disabled,far,missing}), 0);
    auto tie=best; tie.stationId=15;
    QCOMPARE(discovery::recommend({tie,best}), 5);
    std::reverse(items.begin(), items.end());
    QCOMPARE(discovery::recommend(items), 5);
    auto closer=best; closer.stationId=16; closer.distanceKm=1;
    QCOMPARE(discovery::recommend({best,closer}), 16);
    auto cheaper=best; cheaper.stationId=17; cheaper.priceCentsPerKwh=50;
    QCOMPARE(discovery::recommend({best,cheaper}), 17);

    StationBrowserPage page; page.showStations(items);
    OrderDto old; old.stationId=4; old.startedAt="2026-09-01T00:00:00Z";
    OrderDto latest; latest.stationId=1; latest.startedAt="2026-09-08T00:00:00Z";
    page.showVisitHistory({old,latest});
    int used=0, recommended=0, close=0;
    for(auto *button:page.findChildren<QPushButton *>()) {
        used+=button->objectName().startsWith("discoveryStation_home_visited_");
        recommended+=button->objectName().startsWith("discoveryStation_home_recommended_");
        close+=button->objectName().startsWith("discoveryStation_home_nearby_");
    }
    QCOMPARE(used,1); QCOMPARE(recommended,1); QCOMPARE(close,4);
    QVERIFY(page.findChild<QPushButton *>("discoveryStation_home_visited_1"));
}

QTEST_MAIN(StationMapTests)
#include "station_map_tests.moc"
