#include "api/mock_charging_api.h"
#include "local/mock_map_service.h"
#include "ui/station_map_view.h"
#include "vehicle/vehicle_main_window.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTimer>

using namespace charging;

namespace {

template<typename T>
T *child(QObject &parent, const char *name)
{
    auto *value = parent.findChild<T *>(QString::fromLatin1(name));
    Q_ASSERT(value);
    return value;
}

void login(client::VehicleMainWindow &window)
{
    child<QLineEdit>(window, "phoneInput")->setText(QStringLiteral("13800000001"));
    child<QLineEdit>(window, "verificationCodeInput")->setText(QStringLiteral("123456"));
    child<QPushButton>(window, "loginButton")->click();
    QTRY_VERIFY(child<QTabWidget>(window, "vehicleNavigation")->isVisible());
}

QRect globalRect(QWidget *widget)
{
    return QRect(widget->mapToGlobal(QPoint()), widget->size());
}

}  // namespace

class VehicleUiTests final : public QObject {
    Q_OBJECT
private slots:
    void navigationAndNoScanner();
    void landscapeGeometry();
    void stationToChargingFlow();
    void reservationCanBeCancelled();
    void invalidSessionReturnsToLogin();
};

void VehicleUiTests::navigationAndNoScanner()
{
    client::MockChargingApi api;
    client::MockMapService map;
    client::VehicleMainWindow window(api, map);
    window.resize(1280, 720); window.show(); login(window);
    auto *navigation = child<QTabWidget>(window, "vehicleNavigation");
    QCOMPARE(navigation->count(), 3);
    QCOMPARE(navigation->tabText(0), QStringLiteral("首页"));
    QCOMPARE(navigation->tabText(1), QStringLiteral("充电"));
    QCOMPARE(navigation->tabText(2), QStringLiteral("我的"));
    QVERIFY(window.findChild<QObject *>(QStringLiteral("scanPage")) == nullptr);
    for (auto *button : window.findChildren<QPushButton *>())
        QVERIFY2(!button->text().contains(QStringLiteral("扫码")), qPrintable(button->text()));
    QVERIFY(navigation->tabBar()->height() >= 56);
}

void VehicleUiTests::landscapeGeometry()
{
    for (const QSize size : {QSize(1280, 720), QSize(1024, 600)}) {
        client::MockChargingApi api;
        client::MockMapService map;
        client::VehicleMainWindow window(api, map);
        window.resize(size); window.show(); login(window); QTest::qWait(150);
        auto *mapStack = child<QStackedWidget>(window, "vehicleMapStack");
        auto *side = child<QWidget>(window, "vehicleHomeSidePanel");
        auto *attribution = child<QLabel>(window, "stationMapMode");
        auto *navigation = child<QTabWidget>(window, "vehicleNavigation");
        QVERIFY(mapStack->isVisible()); QVERIFY(side->isVisible());
        QVERIFY(!globalRect(mapStack).intersects(globalRect(side)));
        QVERIFY(globalRect(mapStack).bottom() < globalRect(navigation->tabBar()).top());
        QVERIFY(globalRect(mapStack).contains(globalRect(attribution)));
        QVERIFY(!globalRect(attribution).intersects(globalRect(navigation->tabBar())));
        QVERIFY(mapStack->width() > side->width());
        QVERIFY(side->width() >= 310);
        for (auto *button : window.findChildren<QPushButton *>()) {
            if (!button->isVisible()) continue;
            QVERIFY2(button->height() >= 48, qPrintable(button->objectName()));
        }
    }
}

void VehicleUiTests::stationToChargingFlow()
{
    client::MockChargingApi api;
    client::MockMapService map;
    client::VehicleMainWindow window(api, map);
    window.resize(1280, 720); window.show(); login(window);
    auto *stationMap = child<client::StationMapView>(window, "vehicleStationMap");
    QVERIFY(QMetaObject::invokeMethod(stationMap, "stationSelected", Q_ARG(qint64, 1)));
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01")) != nullptr);
    window.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01"))->click();
    auto *navigation = child<QTabWidget>(window, "vehicleNavigation");
    QCOMPARE(navigation->currentIndex(), 1);
    auto *start = child<QPushButton>(window, "vehicleStartButton");
    QTRY_VERIFY(start->isVisible());
    start->click();
    auto *stop = child<QPushButton>(window, "vehicleStopButton");
    QTRY_VERIFY(stop->isVisible());
    QTimer::singleShot(50, [] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (box && box->objectName() == QStringLiteral("vehicleStopConfirmation"))
                box->done(QMessageBox::Yes);
        }
    });
    stop->click();
    QTRY_VERIFY(!stop->isVisible());
    QVERIFY(child<QProgressBar>(window, "vehicleChargingProgress")
                ->format().contains(QStringLiteral("Demo 会话进度")));
}

void VehicleUiTests::reservationCanBeCancelled()
{
    client::MockChargingApi api;
    client::MockMapService map;
    client::VehicleMainWindow window(api, map);
    window.resize(1280, 720); window.show(); login(window);
    auto *stationMap = child<client::StationMapView>(window, "vehicleStationMap");
    QVERIFY(QMetaObject::invokeMethod(stationMap, "stationSelected", Q_ARG(qint64, 1)));
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("vehicleReserve_PILE-A-01")) != nullptr);
    window.findChild<QPushButton *>(QStringLiteral("vehicleReserve_PILE-A-01"))->click();
    auto *cancel = child<QPushButton>(window, "vehicleCancelReservationButton");
    QTRY_VERIFY(cancel->isVisible());
    cancel->click();
    QTRY_VERIFY(!cancel->isVisible());
    QVERIFY(child<QPushButton>(window, "vehicleStartButton")->isHidden());
}

void VehicleUiTests::invalidSessionReturnsToLogin()
{
    client::MockChargingApi api;
    client::MockMapService map;
    client::VehicleMainWindow window(api, map);
    window.resize(1024, 600); window.show(); login(window);
    QSignalSpy logoutSpy(&api, &client::IChargingApi::logoutCompleted);
    const QString logoutRequest = api.logout();
    QVERIFY(!logoutRequest.isEmpty());
    QTRY_COMPARE(logoutSpy.count(), 1);
    child<QPushButton>(window, "vehicleRefreshButton")->click();
    QTRY_VERIFY(child<QLineEdit>(window, "phoneInput")->isVisible());
}

QTEST_MAIN(VehicleUiTests)
#include "vehicle_ui_tests.moc"
