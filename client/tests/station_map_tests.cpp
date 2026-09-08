#include "api/mock_charging_api.h"
#include "ui/main_window.h"
#include "ui/station_browser_page.h"
#include "ui/station_map_view.h"

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
#include <QTimer>
#include <QtTest>
#include <cmath>

using namespace charging::client;

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
};

namespace {
class PaintProbe final : public QObject {
public:
    int paints = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) ++paints;
        return false;
    }
};

void login(MainWindow &window)
{
    window.show();
    window.findChild<QLineEdit *>("phoneInput")->setText(QStringLiteral("13800000001"));
    window.findChild<QLineEdit *>("verificationCodeInput")->setText(QStringLiteral("123456"));
    window.findChild<QPushButton *>("loginButton")->click();
    QTRY_VERIFY(window.findChild<QAbstractButton *>("stationMarker_2"));
    QTRY_VERIFY_WITH_TIMEOUT(window.findChild<StationMapView *>()->isReady(), 2000);
}

void screenshot(MainWindow &window, const QString &name)
{
    const QString directory = qEnvironmentVariable("BIT_CLIENT_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    QDir().mkpath(directory);
    QVERIFY(window.grab().save(directory + QLatin1Char('/') + name + QStringLiteral(".png")));
}
}

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

void StationMapTests::mapHomeFitsAndSelects_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("default") << QSize(480, 860);
    QTest::newRow("small") << QSize(360, 640);
    QTest::newRow("wide") << QSize(900, 640);
}

void StationMapTests::mapHomeFitsAndSelects()
{
    QFETCH(QSize, size);
    MockChargingApi api;
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
    QVERIFY(overlay->height() < home->height() * .25);
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
    QVERIFY(window.findChild<QLabel *>("stationPreviewMetrics")->text().contains(QStringLiteral("1/2 空闲 · ¥1.35/度")));
    QVERIFY(map->usableViewport().contains(one->geometry().center()));
    QVERIFY(map->usableViewport().contains(two->geometry().center()));
    QVERIFY(preview->geometry().bottom() < home->height());
    QVERIFY(overlay->geometry().bottom() < preview->y());
    screenshot(window, QStringLiteral("selected-%1").arg(QString::fromLatin1(QTest::currentDataTag())));

    two->setFocus();
    QTest::keyClick(two, Qt::Key_Space);
    QCOMPARE(map->selectedStationId(), 2);
    QVERIFY(two->isChecked());
    QVERIFY(!one->isChecked());
    QCOMPARE(window.findChild<QLabel *>("stationPreviewName")->text(), QStringLiteral("和平演示充电站"));
    QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, QPoint(40, home->height() / 2));
    QVERIFY(!preview->isVisible());
    QCOMPARE(map->selectedStationId(), 0);
    QTest::mouseClick(one, Qt::LeftButton);
    window.findChild<QPushButton *>("stationPreviewDetailsButton")->click();
    QTRY_COMPARE(details.size(), 1);
    QTRY_VERIFY(window.findChild<QWidget *>("stationDetailPage")->isVisible());
}

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
    window.findChild<QPushButton *>("stationFilterToggle")->click();
    window.findChild<QCheckBox *>("demoLocationCheck")->setChecked(false);
    window.findChild<QPushButton *>("stationFilterToggle")->click();
    search->click();
    QTRY_VERIFY(search->isEnabled());
    window.findChild<QAbstractButton *>("stationMarker_2")->click();
    QVERIFY(window.findChild<QLabel *>("stationPreviewMetrics")->text().contains(QStringLiteral("距离待定位")));
    QVERIFY(window.findChild<QPushButton *>("stationMapLocate")->isEnabled());
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
    auto *filter = window.findChild<QPushButton *>("stationFilterToggle");
    filter->click();
    QVERIFY(!preview->isVisible());
    QCOMPARE(map->selectedStationId(), 1);
    auto *scroll = window.findChild<QScrollArea *>("stationFilterScrollArea");
    QVERIFY(scroll->isVisible());
    QTRY_VERIFY(scroll->widget()->width() <= scroll->viewport()->width());
    QTRY_VERIFY(window.findChild<QAbstractButton *>("stationMarker_2")->y()
        > window.findChild<QWidget *>("stationHomeOverlay")->geometry().bottom());
    screenshot(window, QStringLiteral("filters-small"));
    filter->click();
    QVERIFY(preview->isVisible());
    QCOMPARE(map->height(), height);
}

QTEST_MAIN(StationMapTests)
#include "station_map_tests.moc"
