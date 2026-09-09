#include "api/mock_charging_api.h"
#include "common/charging_progress_ring.h"
#include "local/mock_map_service.h"
#include "ui/station_map_view.h"
#include "vehicle/vehicle_main_window.h"
#include "vehicle/vehicle_pages.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
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
    void loginUsesLandscapeColumns();
    void landscapeGeometry();
    void routeCanEnterAndExitFullscreen();
    void orderPageUsesLandscapeCards();
    void stationToChargingFlow();
    void chargingUsesRealSecondsAndAutomaticallyEnds();
    void reservationCanBeCancelled();
    void invalidSessionReturnsToLogin();
};

void VehicleUiTests::loginUsesLandscapeColumns()
{
    for (const QSize size : {QSize(1280, 720), QSize(1024, 600)}) {
        client::MockChargingApi api;
        client::MockMapService map;
        client::VehicleMainWindow window(api, map);
        window.resize(size);
        window.show();
        QTest::qWait(100);
        auto *intro = child<QWidget>(window, "loginIntro");
        auto *card = child<QWidget>(window, "loginCard");
        QVERIFY(intro->isVisible());
        QVERIFY(card->isVisible());
        QVERIFY(globalRect(intro).right() < globalRect(card).left());
        QVERIFY(globalRect(card).width() >= 360);
        auto *loginButton = child<QPushButton>(window, "loginButton");
        QVERIFY2(loginButton->height() >= 56,
                 qPrintable(QStringLiteral("height=%1 min=%2 max=%3")
                                .arg(loginButton->height()).arg(loginButton->minimumHeight())
                                .arg(loginButton->maximumHeight())));
    }
}

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
    QCOMPARE(child<QPushButton>(window, "vehicleRefreshButton")->text(), QStringLiteral("↻"));
    QCOMPARE(child<QLabel>(window, "vehicleSafetyNotice")->text(),
             QStringLiteral("请停车后操作，安全驾驶，文明出行"));
    const QString accountText = child<QPushButton>(window, "vehicleAccountButton")->text();
    QCOMPARE(accountText, child<QLabel>(window, "vehicleProfileNickname")->text());
    QVERIFY(!accountText.contains(QStringLiteral("138")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("vehicleSearchButton")) == nullptr);
    QCOMPARE(window.findChildren<QLineEdit *>(QStringLiteral("vehicleStationSearch")).size(), 1);
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
        navigation->setCurrentIndex(1);
        QTest::qWait(50);
        auto *chargingSession = child<QWidget>(window, "vehicleChargingSession");
        auto *chargingSide = child<QWidget>(window, "vehicleChargingSide");
        QVERIFY(!globalRect(chargingSession).intersects(globalRect(chargingSide)));
        QVERIFY(child<client::ChargingProgressRing>(window,
                    "vehicleChargingProgressRing")->isVisible());
        QVERIFY(child<QPushButton>(window, "vehicleChargingOrdersEntry")->isVisible());
        navigation->setCurrentIndex(2);
        QTest::qWait(50);
        auto *profileDetails = child<QWidget>(window, "vehicleProfileDetailsCard");
        auto *profileActions = child<QWidget>(window, "vehicleProfileActionsColumn");
        QVERIFY(!globalRect(profileDetails).intersects(globalRect(profileActions)));
        auto *avatar = child<QWidget>(window, "vehicleLocalAvatar");
        QVERIFY(qAbs(globalRect(avatar).center().x() - globalRect(profileDetails).center().x()) <= 2);
        QCOMPARE(avatar->size(), QSize(168, 168));
        auto *nickname = child<QLabel>(window, "vehicleProfileNickname");
        auto *phone = child<QLabel>(window, "vehicleProfilePhone");
        auto *balance = child<QLabel>(window, "vehicleProfileBalance");
        QCOMPARE(nickname->font().pointSize(), phone->font().pointSize());
        QCOMPARE(nickname->font().pointSize(), balance->font().pointSize());
        QVERIFY(nickname->font().pointSize() >= 20);
        QVERIFY(nickname->font().bold());
        QVERIFY(phone->font().bold());
        QVERIFY(balance->font().bold());
        QCOMPARE(nickname->styleSheet(), phone->styleSheet());
        QCOMPARE(nickname->styleSheet(), balance->styleSheet());
        QVERIFY(nickname->styleSheet().contains(QStringLiteral("#36583c")));
        auto *identityBlock = child<QWidget>(window, "vehicleProfileIdentityBlock");
        QVERIFY(globalRect(identityBlock).contains(globalRect(nickname)));
        QVERIFY(globalRect(identityBlock).contains(globalRect(phone)));
        QVERIFY(globalRect(identityBlock).contains(globalRect(balance)));
        QVERIFY(identityBlock->styleSheet().contains(QStringLiteral("background:transparent")));
        auto *editProfile = child<QPushButton>(window, "vehicleEditProfileButton");
        QVERIFY(editProfile->isVisible());
        auto *ordersButton = child<QPushButton>(window, "vehicleOrdersButton");
        auto *supportButton = child<QPushButton>(window, "vehicleSupportButton");
        auto *repairButton = child<QPushButton>(window, "vehicleRepairButton");
        auto *ticketsButton = child<QPushButton>(window, "vehicleTicketsButton");
        auto *serviceCard = child<QWidget>(window, "vehicleProfileServiceCard");
        auto *logoutButton = child<QPushButton>(window, "vehicleLogoutButton");
        auto *profileControls = child<QWidget>(window, "vehicleProfileControls");
        QVERIFY(profileControls->styleSheet().contains(QStringLiteral("background:transparent")));
        QVERIFY(globalRect(logoutButton).left() >= globalRect(profileDetails).left());
        QVERIFY(globalRect(logoutButton).right() <= globalRect(profileDetails).right());
        QVERIFY(globalRect(logoutButton).top() > globalRect(editProfile).bottom());
        QVERIFY(globalRect(logoutButton).top() - globalRect(editProfile).bottom() <= 12);
        QVERIFY(editProfile->height() >= 60);
        QVERIFY2(logoutButton->height() >= 56,
                 qPrintable(QStringLiteral("window=%1x%2 logout=%3 min=%4 max=%5")
                                .arg(size.width()).arg(size.height()).arg(logoutButton->height())
                                .arg(logoutButton->minimumHeight()).arg(logoutButton->maximumHeight())));
        QVERIFY(logoutButton->styleSheet().contains(QStringLiteral("#fff0ec")));
        QVERIFY(logoutButton->height() >= 60);
        const auto verticalGap = [](QWidget *upper, QWidget *lower) {
            return globalRect(lower).top() - globalRect(upper).bottom();
        };
        const int avatarIdentityGap = verticalGap(avatar, nickname);
        QVERIFY2(avatarIdentityGap >= 10 && avatarIdentityGap <= 18,
                 qPrintable(QStringLiteral("avatar identity gap=%1 at %2x%3")
                                .arg(avatarIdentityGap).arg(size.width()).arg(size.height())));
        QVERIFY(verticalGap(nickname, phone) >= 1);
        QVERIFY(verticalGap(nickname, phone) <= 6);
        QVERIFY(verticalGap(phone, balance) >= 1);
        QVERIFY(verticalGap(phone, balance) <= 6);
        const int identityControlsGap = verticalGap(balance, editProfile);
        QVERIFY2(identityControlsGap >= 10 && identityControlsGap <= 18,
                 qPrintable(QStringLiteral("identity controls gap=%1 at %2x%3")
                                .arg(identityControlsGap).arg(size.width()).arg(size.height())));
        QVERIFY(ordersButton->height() > 70);
        QVERIFY(supportButton->height() > 70);
        for (auto *service : {ordersButton, supportButton, repairButton, ticketsButton}) {
            QCOMPARE(service->iconSize(), QSize(40, 40));
            QVERIFY(service->font().pointSize() >= 19);
            QVERIFY(service->font().bold());
            QVERIFY(service->styleSheet().contains(QStringLiteral("font-weight:800")));
        }
        QVERIFY(globalRect(repairButton).bottom() >= globalRect(serviceCard).bottom() - 16);
        QVERIFY(globalRect(ticketsButton).bottom() >= globalRect(serviceCard).bottom() - 16);
        auto *profilePage = child<client::VehicleProfilePage>(window, "vehicleProfilePage");
        profilePage->showMessage(QStringLiteral("资料已刷新"));
        auto *profileMessage = child<QLabel>(window, "vehicleProfileMessage");
        QVERIFY(globalRect(profileMessage).top() > globalRect(editProfile).bottom());
        QVERIFY(globalRect(profileMessage).bottom() < globalRect(logoutButton).top());
        for (auto *label : profileDetails->findChildren<QLabel *>())
            QVERIFY(!label->text().contains(QStringLiteral("昵称、手机号与余额来自服务端")));
        int rechargeChoices = 0;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->property("rechargeAmount").isValid()) ++rechargeChoices;
        QCOMPARE(rechargeChoices, 4);
        editProfile->click();
        QVERIFY(child<QPushButton>(window, "vehicleAvatarChangeButton")->isVisible());
        child<QPushButton>(window, "vehicleProfileEditBackButton")->click();
        for (auto *button : window.findChildren<QPushButton *>()) {
            if (!button->isVisible()) continue;
            QVERIFY2(button->height() >= 48, qPrintable(button->objectName()));
        }
    }
}

void VehicleUiTests::routeCanEnterAndExitFullscreen()
{
    client::MockChargingApi api;
    client::MockMapService map;
    client::VehicleMainWindow window(api, map);
    window.resize(1024, 600);
    window.show();
    login(window);
    auto *page = child<client::VehicleHomePage>(window, "vehicleHomePage");
    client::StationDetailPayload detail;
    detail.station.stationId = 7;
    detail.station.name = QStringLiteral("悦充测试站");
    detail.station.address = QStringLiteral("青年大街 7 号");
    detail.station.longitude = 123.42;
    detail.station.latitude = 41.70;
    page->showStationDetail(detail);
    child<QPushButton>(window, "vehicleRouteButton")->click();
    auto *mapStack = child<QStackedWidget>(window, "vehicleMapStack");
    QCOMPARE(mapStack->currentWidget()->objectName(), QStringLiteral("vehicleStationMap"));
    QVERIFY(window.findChild<QObject *>(QStringLiteral("vehicleRouteMode")) == nullptr);
    QVERIFY(child<QPlainTextEdit>(window, "vehicleRoutePanelDetails")->toPlainText()
                .contains(QStringLiteral("所选充电站")));
    client::RouteResult route;
    route.success = true;
    route.summary = QStringLiteral("驾车约 2.0 公里 · 6 分钟");
    route.instructions = {QStringLiteral("向东行驶 800 米"),
                          QStringLiteral("右转进入青年大街")};
    QSignalSpy fullscreen(page, &client::VehicleHomePage::routeFullscreenChanged);
    page->showRoute(route);
    QTRY_VERIFY(child<QWidget>(window, "vehicleHomeSidePanel")->isHidden());
    QTRY_VERIFY(child<QWidget>(window, "vehicleHeader")->isHidden());
    QTRY_VERIFY(child<QTabWidget>(window, "vehicleNavigation")->tabBar()->isHidden());
    QTRY_VERIFY(child<QPushButton>(window, "vehicleExitRouteFullscreen")->isVisible());
    auto *exitButton = child<QPushButton>(window, "vehicleExitRouteFullscreen");
    QVERIFY(globalRect(exitButton).center().x() < globalRect(mapStack).center().x());
    auto *detailsButton = child<QPushButton>(window, "vehicleRouteDetailsButton");
    auto *details = child<QPlainTextEdit>(window, "vehicleRouteDetails");
    QVERIFY(detailsButton->isEnabled());
    QVERIFY(details->isHidden());
    detailsButton->click();
    QVERIFY(details->isVisible());
    QCOMPARE(detailsButton->text(), QStringLiteral("收起路线"));
    QVERIFY(details->toPlainText().contains(QStringLiteral("青年大街")));
    detailsButton->click();
    QVERIFY(details->isHidden());
    QCOMPARE(detailsButton->text(), QStringLiteral("展开路线"));
    QCOMPARE(fullscreen.count(), 1);
    exitButton->click();
    QVERIFY(child<QWidget>(window, "vehicleHomeSidePanel")->isVisible());
    QVERIFY(child<QWidget>(window, "vehicleHeader")->isVisible());
    QVERIFY(child<QTabWidget>(window, "vehicleNavigation")->tabBar()->isVisible());
    const QString panelRoute = child<QPlainTextEdit>(window, "vehicleRoutePanelDetails")->toPlainText();
    QVERIFY(panelRoute.contains(route.summary));
    QVERIFY(panelRoute.contains(QStringLiteral("青年大街")));
    QVERIFY(!panelRoute.contains(QStringLiteral("路线已绘制")));
}

void VehicleUiTests::orderPageUsesLandscapeCards()
{
    client::VehicleProfilePage page;
    page.resize(1000, 480);
    page.show();
    protocol::OrderDto first;
    first.orderId = 21;
    first.orderNo = QStringLiteral("ORD-20260908-21");
    first.createdAt = QStringLiteral("2026-09-08T02:00:00Z");
    first.stationName = QStringLiteral("悦充青年大街站");
    first.pileCode = QStringLiteral("PILE-A-01");
    first.status = protocol::OrderStatus::Completed;
    first.mode = protocol::OrderMode::Direct;
    first.startedAt = QStringLiteral("2026-09-08T02:01:00Z");
    first.endedAt = QStringLiteral("2026-09-08T02:04:00Z");
    first.durationSeconds = 180;
    first.energyWh = 2500;
    first.unitPriceCentsPerKwh = 120;
    first.amountCents = 300;
    protocol::OrderDto second = first;
    second.orderId = 22;
    second.orderNo = QStringLiteral("ORD-20260908-22");
    second.stationName = QStringLiteral("悦充奥体中心站");
    second.status = protocol::OrderStatus::PendingPayment;
    second.amountCents = 420;
    page.showOrders({first, second});
    QTest::qWait(50);

    auto *list = child<QListWidget>(page, "vehicleOrderList");
    auto *detail = child<QWidget>(page, "vehicleOrderDetailCard");
    QCOMPARE(list->count(), 2);
    QVERIFY(!globalRect(list).intersects(globalRect(detail)));
    QVERIFY(child<QPushButton>(page, "vehicleOrderCard_21")->isVisible());
    QVERIFY(child<QLabel>(page, "vehicleOrderDetailBody")->text().contains(QStringLiteral("2.500")));
    child<QPushButton>(page, "vehicleOrderCard_22")->click();
    QCOMPARE(child<QLabel>(page, "vehicleOrderDetailNumber")->text(),
             QStringLiteral("订单 ORD-20260908-22"));
    QCOMPARE(child<QLabel>(page, "vehicleOrderDetailStatus")->text(), QStringLiteral("待支付"));
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
    QVERIFY(child<QLabel>(window, "vehicleChargingPrice")->text()
                .contains(QStringLiteral("参考单价")));
    start->click();
    auto *stop = child<QPushButton>(window, "vehicleStopButton");
    QTRY_VERIFY(stop->isVisible());
    QTest::qWait(1100);
    const QString duration = child<QLabel>(window, "vehicleChargingDuration")->text();
    QVERIFY2(duration == QStringLiteral("00:00") || duration == QStringLiteral("00:01")
                 || duration == QStringLiteral("00:02") || duration == QStringLiteral("00:03"),
             qPrintable(duration));
    QTimer::singleShot(50, [] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (box && box->objectName() == QStringLiteral("vehicleStopConfirmation"))
                box->done(QMessageBox::Yes);
        }
    });
    stop->click();
    QTRY_VERIFY(!stop->isVisible());
    QVERIFY(child<client::ChargingProgressRing>(window, "vehicleChargingProgressRing")
                ->accessibleName().contains(QStringLiteral("Demo 会话")));
}

void VehicleUiTests::chargingUsesRealSecondsAndAutomaticallyEnds()
{
    QDateTime now = QDateTime::fromString(QStringLiteral("2026-09-09T02:00:00Z"), Qt::ISODate);
    client::MockChargingApi api(nullptr, [&now] { return now; });
    client::MockMapService map;
    client::VehicleMainWindow window(api, map);
    window.resize(1024, 600);
    window.show();
    login(window);
    auto *stationMap = child<client::StationMapView>(window, "vehicleStationMap");
    QVERIFY(QMetaObject::invokeMethod(stationMap, "stationSelected", Q_ARG(qint64, 1)));
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01")));
    window.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01"))->click();
    child<QPushButton>(window, "vehicleStartButton")->click();
    QTRY_VERIFY(child<QPushButton>(window, "vehicleStopButton")->isVisible());
    QSignalSpy progressCalls(&api, &client::IChargingApi::chargingProgressCompleted);

    now = now.addSecs(protocol::DemoChargingDurationSeconds + 1);
    QTRY_COMPARE_WITH_TIMEOUT(child<QLabel>(window, "vehicleChargingState")->text(),
                              QStringLiteral("已完成"), 3500);
    QVERIFY(child<QPushButton>(window, "vehicleStopButton")->isHidden());
    QCOMPARE(progressCalls.count(), 0);
    QCOMPARE(child<QLabel>(window, "vehicleChargingDuration")->text(), QStringLiteral("03:00"));
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
