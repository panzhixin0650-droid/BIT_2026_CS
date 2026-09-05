#include "api/i_charging_api.h"
#include "ui/main_window.h"
#include "ui/login_page.h"
#include "ui/order_page.h"
#include "ui/order_controller.h"
#include "ui/profile_page.h"
#include "ui/scan_page.h"
#include "ui/station_browser_page.h"
#include "ui/station_browser_controller.h"

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QtTest>

using namespace charging::client;
namespace protocol = charging::protocol;

// Completion is controlled by each test, without sockets or production API changes.
class DeferredApi final : public IChargingApi {
public:
    QHash<QString, QString> latest;
    QHash<QString, int> calls;
    int serial = 0;
    QString reservedPileCode;
    QString request(const char *type) {
        const QString key = QString::fromLatin1(type);
        ++calls[key];
        return latest[key] = QString::number(++serial);
    }
    QString loginUser(const QString &) override { return request("auth.user.login"); }
    QString logout() override { return request("auth.logout"); }
    QString getProfile() override { return request("user.profile.get"); }
    QString updateNickname(const QString &) override { return request("user.profile.update"); }
    QString recharge(qint64) override { return request("wallet.recharge"); }
    QString listStations(const StationQuery &) override { return request("station.list"); }
    QString getStation(qint64) override { return request("station.detail"); }
    QString getCurrentOrder() override { return request("order.current"); }
    QString listOrders() override { return request("order.list"); }
    QString reserve(const QString &code) override {
        reservedPileCode = code;
        return request("order.reserve");
    }
    QString cancel(qint64) override { return request("order.cancel"); }
    QString startCharging(const QString &, std::optional<qint64>) override { return request("order.start"); }
    QString getChargingProgress(qint64) override { return request("order.progress"); }
    QString stopCharging(qint64) override { return request("order.stop"); }
    QString payOrder(qint64) override { return request("order.pay"); }

    template<class T> T reply(const char *type, int code = protocol::ErrorCode::Ok) {
        T result;
        result.response = {latest[QString::fromLatin1(type)], QString::fromLatin1(type), code,
                           code == 0 ? QStringLiteral("OK") : QStringLiteral("SERVICE_UNAVAILABLE")};
        if (code == 0) result.payload.emplace();
        return result;
    }
};

class UiRecoveryTests : public QObject {
    Q_OBJECT
private:
    void login(MainWindow &window, DeferredApi &api, qint64 userId = 1) {
        window.findChild<LoginPage *>()->loginRequested(QStringLiteral("13800000001"));
        auto result = api.reply<LoginResult>("auth.user.login");
        result.payload->user.userId = userId;
        result.payload->user.nickname = QStringLiteral("用户%1").arg(userId);
        result.payload->user.phone = QStringLiteral("13800000001");
        emit api.loginCompleted(result);
    }
private slots:
    void sessionExpiryDiscardsOtherPagesPendingResults() {
        DeferredApi api;
        MainWindow window(api);
        login(window, api);
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
        tabs->setCurrentIndex(1);
        const auto staleOrders = api.reply<OrderListResult>("order.list");
        tabs->setCurrentIndex(4);
        auto staleProfile = api.reply<UserResult>("user.profile.get");
        staleProfile.payload->user.nickname = QStringLiteral("旧用户资料");
        tabs->setCurrentIndex(2);
        window.findChild<ScanPage *>()->scanRequested(QStringLiteral("PILE-A-01"));
        const auto staleScan = api.reply<CurrentOrderResult>("order.current");
        emit api.stationListCompleted(api.reply<StationListResult>("station.list", protocol::ErrorCode::InvalidSession));
        QCOMPARE(window.findChild<QStackedWidget *>(QStringLiteral("applicationPages"))->currentWidget(), window.findChild<LoginPage *>());
        login(window, api, 2);
        emit api.profileCompleted(staleProfile);
        emit api.orderListCompleted(staleOrders);
        emit api.currentOrderCompleted(staleScan);
        QCOMPARE(api.calls[QStringLiteral("order.start")], 0);
        QCOMPARE(window.findChild<QLabel *>(QStringLiteral("profileNicknameLabel"))->text(), QStringLiteral("用户2"));
        tabs->setCurrentIndex(1);
        QCOMPARE(api.calls[QStringLiteral("order.list")], 2);
        tabs->setCurrentIndex(4);
        QCOMPARE(api.calls[QStringLiteral("user.profile.get")], 2);
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("scanStartButton"))->isEnabled());
    }

    void logoutDiscardsPendingHomeResponse() {
        DeferredApi api;
        MainWindow window(api);
        login(window, api);
        auto stale = api.reply<StationListResult>("station.list", protocol::ErrorCode::InvalidSession);
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
        tabs->setCurrentIndex(4);
        emit api.profileCompleted(api.reply<UserResult>("user.profile.get"));
        window.findChild<ProfilePage *>()->logoutRequested();
        auto result = api.reply<LogoutResult>("auth.logout");
        result.payload->success = true;
        emit api.logoutCompleted(result);
        login(window, api, 2);
        emit api.stationListCompleted(stale);
        QCOMPARE(window.findChild<QStackedWidget *>(QStringLiteral("applicationPages"))->currentWidget(), tabs);
    }

    void timeoutRestoresControlsWithoutRepeatingWrites() {
        DeferredApi api;
        MainWindow window(api);
        login(window, api);
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
        tabs->setCurrentIndex(4);
        emit api.profileCompleted(api.reply<UserResult>("user.profile.get"));
        auto *profile = window.findChild<ProfilePage *>();
        profile->rechargeRequested(QStringLiteral("10"));
        profile->rechargeRequested(QStringLiteral("10"));
        QCOMPARE(api.calls[QStringLiteral("wallet.recharge")], 1);
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("rechargeButton"))->isEnabled());
        emit api.rechargeCompleted(api.reply<RechargeResult>("wallet.recharge", protocol::ErrorCode::ServiceUnavailable));
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("rechargeButton"))->isEnabled());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("profileMessageLabel"))->text().contains(QStringLiteral("核对")));
        QCOMPARE(api.calls[QStringLiteral("wallet.recharge")], 1);
        tabs->setCurrentIndex(1);
        emit api.orderListCompleted(api.reply<OrderListResult>("order.list", protocol::ErrorCode::ServiceUnavailable));
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("orderRefreshButton"))->isEnabled());
        window.findChild<OrderPage *>()->refreshRequested();
        QCOMPARE(api.calls[QStringLiteral("order.list")], 2);
        tabs->setCurrentIndex(2);
        auto *scan = window.findChild<ScanPage *>();
        scan->scanRequested(QStringLiteral("PILE-A-01"));
        scan->scanRequested(QStringLiteral("PILE-A-01"));
        emit api.currentOrderCompleted(api.reply<CurrentOrderResult>("order.current", protocol::ErrorCode::ServiceUnavailable));
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("scanStartButton"))->isEnabled());
        QCOMPARE(api.calls[QStringLiteral("order.start")], 0);
    }

    void lateProgressDoesNotReopenOrReplaceDetail() {
        DeferredApi api;
        OrderPage page;
        OrderController controller(page, api);
        protocol::OrderDto first;
        first.orderId = 1;
        first.orderNo = QStringLiteral("FIRST");
        first.status = protocol::OrderStatus::Charging;
        auto second = first;
        second.orderId = 2;
        second.orderNo = QStringLiteral("SECOND");
        page.showOrders({first, second});
        page.show();
        QTest::mouseClick(page.findChild<QWidget *>(QStringLiteral("orderCard_1")), Qt::LeftButton);
        page.progressRequested(1);
        auto result = api.reply<ChargingProgressResult>("order.progress");
        result.payload->order = first;
        page.showListPage();
        emit api.chargingProgressCompleted(result);
        QCOMPARE(page.findChild<QStackedWidget *>(QStringLiteral("orderPages"))->currentWidget()->objectName(), QStringLiteral("orderListPage"));
        QTest::mouseClick(page.findChild<QWidget *>(QStringLiteral("orderCard_1")), Qt::LeftButton);
        page.progressRequested(1);
        result = api.reply<ChargingProgressResult>("order.progress");
        result.payload->order = first;
        page.showListPage();
        QTest::mouseClick(page.findChild<QWidget *>(QStringLiteral("orderCard_2")), Qt::LeftButton);
        emit api.chargingProgressCompleted(result);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("orderDetailNumber"))->text(), QStringLiteral("订单 SECOND"));
    }

    void reservationCheckKeepsFirstSubmission() {
        DeferredApi api;
        StationBrowserPage page;
        StationBrowserController controller(page, api);
        page.reservationRequested(QStringLiteral("PILE-A-01"));
        page.reservationRequested(QStringLiteral("PILE-B-02"));
        QCOMPARE(api.calls[QStringLiteral("order.current")], 1);
        emit api.currentOrderCompleted(api.reply<CurrentOrderResult>("order.current"));
        QCOMPARE(api.calls[QStringLiteral("order.reserve")], 1);
        QCOMPARE(api.reservedPileCode, QStringLiteral("PILE-A-01"));
        emit api.reservationCompleted(api.reply<OrderResult>("order.reserve", protocol::ErrorCode::ServiceUnavailable));
        page.reservationRequested(QStringLiteral("PILE-B-02"));
        QCOMPARE(api.calls[QStringLiteral("order.current")], 2);
    }

    void leavingOrdersDiscardsNavigationIntent() {
        DeferredApi api;
        MainWindow window(api);
        login(window, api);
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
        tabs->setCurrentIndex(1);
        emit api.orderListCompleted(api.reply<OrderListResult>("order.list"));
        window.findChild<OrderPage *>()->navigationRequested(1);
        auto result = api.reply<StationDetailResult>("station.detail");
        tabs->setCurrentIndex(4);
        emit api.stationDetailCompleted(result);
        QCOMPARE(tabs->currentIndex(), 4);
    }
};

QTEST_MAIN(UiRecoveryTests)
#include "ui_recovery_tests.moc"
