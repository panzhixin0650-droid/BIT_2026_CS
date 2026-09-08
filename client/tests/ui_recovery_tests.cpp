#include "api/i_charging_api.h"
#include "ui/main_window.h"
#include "ui/charging_controller.h"
#include "ui/charging_page.h"
#include "ui/login_page.h"
#include "ui/order_page.h"
#include "ui/order_controller.h"
#include "ui/profile_page.h"
#include "ui/scan_page.h"
#include "ui/station_browser_page.h"
#include "ui/station_browser_controller.h"

#include <QLabel>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QSignalSpy>
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
    void verifyPricingRules(ChargingPage &page, bool hasPeak) {
        auto *help = page.findChild<QToolButton *>("chargingPricingInfoButton");
        QVERIFY(help && help->isVisible());
        QVERIFY(!page.findChild<QLabel *>("chargingPricingRule"));
        QVERIFY(!page.findChild<QPushButton *>("chargingRefreshPriceButton"));
        QVERIFY(!page.findChild<QDialog *>("pricingRulesDialog"));
        help->click();
        auto *dialog = page.findChild<QDialog *>("pricingRulesDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto *text = dialog->findChild<QLabel *>("pricingRulesText");
        QVERIFY(text && text->wordWrap());
        QCOMPARE(text->text().contains(QStringLiteral("高峰 +20%")), hasPeak);
        dialog->findChild<QPushButton *>("pricingRulesCloseButton")->click();
        QTRY_VERIFY(!page.findChild<QDialog *>("pricingRulesDialog"));
    }

    void login(MainWindow &window, DeferredApi &api, qint64 userId = 1) {
        window.findChild<LoginPage *>()->loginRequested(QStringLiteral("13800000001"),
                                                       QStringLiteral("123456"));
        auto result = api.reply<LoginResult>("auth.user.login");
        result.payload->user.userId = userId;
        result.payload->user.nickname = QStringLiteral("用户%1").arg(userId);
        result.payload->user.phone = QStringLiteral("13800000001");
        emit api.loginCompleted(result);
    }
private slots:
    void chargingQuoteRejectsStaleResponsesAndUsesLockedApiPrice() {
        DeferredApi api;
        ChargingPage page;
        page.show();
        ChargingController controller(page, api);
        controller.activate();
        controller.prepare("PILE-A-01");
        const auto stale = api.reply<StationListResult>("station.list");
        controller.prepare("PILE-B-02");
        emit api.stationListCompleted(stale);
        QCOMPARE(api.calls["station.detail"], 0);
        auto stations = api.reply<StationListResult>("station.list");
        protocol::StationDto a, b;
        a.stationId = 1; a.priceCentsPerKwh = 162;
        b.stationId = 2; b.priceCentsPerKwh = 144; b.name = QStringLiteral("和平演示站");
        b.pricingRule = QString::fromLatin1(protocol::DemoPeakPricingRule);
        stations.payload->items = {a, b};
        emit api.stationListCompleted(stations);
        auto wrongStation = api.reply<StationDetailResult>("station.detail");
        wrongStation.payload->station = a;
        protocol::PileDto pileA, pileB;
        pileA.stationId = 1; pileA.pileCode = "PILE-A-01";
        pileB.stationId = 2; pileB.pileCode = "PILE-B-02";
        wrongStation.payload->piles = {pileA};
        emit api.stationDetailCompleted(wrongStation);
        QCOMPARE(api.calls["station.detail"], 2);
        auto quote = api.reply<StationDetailResult>("station.detail");
        quote.payload->station = b; quote.payload->piles = {pileB};
        emit api.stationDetailCompleted(quote);
        auto *start = page.findChild<QPushButton *>("chargingStartButton");
        auto *price = page.findChild<QLabel *>("chargingPrice");
        QVERIFY(start->isEnabled());
        QCOMPARE(price->text(), QStringLiteral("当前参考单价：¥1.44/度"));
        QVERIFY(!price->text().contains(QStringLiteral("高峰")));
        verifyPricingRules(page, true);
        QCOMPARE(api.calls["order.start"], 0);
        emit api.currentOrderCompleted(api.reply<CurrentOrderResult>("order.current"));
        start->click();
        emit api.currentOrderCompleted(api.reply<CurrentOrderResult>("order.current"));
        QCOMPARE(api.calls["order.start"], 1);
        auto started = api.reply<OrderResult>("order.start");
        started.payload->order.orderId = 7;
        started.payload->order.stationId = 2;
        started.payload->order.pileCode = "PILE-B-02";
        started.payload->order.status = protocol::OrderStatus::Charging;
        // Deliberately not the quote: the UI must display the start response verbatim.
        started.payload->order.unitPriceCentsPerKwh = 149;
        emit api.chargingStartCompleted(started);
        QCOMPARE(price->text(), QStringLiteral("本单锁定单价：¥1.49/度"));
        verifyPricingRules(page, false);
        emit api.stationDetailCompleted(quote);
        QCOMPARE(price->text(), QStringLiteral("本单锁定单价：¥1.49/度"));
        controller.reset();
        emit api.stationDetailCompleted(quote);
        QVERIFY(page.pileCode().isEmpty());
        QVERIFY(!start->isEnabled());
    }

    void chargingQuoteFailureCanRetryAndSessionFailureResets() {
        DeferredApi api;
        ChargingPage page;
        ChargingController controller(page, api);
        QSignalSpy auth(&controller, &ChargingController::authenticationRequired);
        controller.activate();
        controller.prepare("PILE-A-01");
        auto *start = page.findChild<QPushButton *>("chargingStartButton");
        QVERIFY(!page.findChild<QPushButton *>("chargingRefreshPriceButton"));
        QVERIFY(!start->isEnabled());
        emit api.stationListCompleted(api.reply<StationListResult>(
            "station.list", protocol::ErrorCode::ServiceUnavailable));
        QVERIFY(start->isEnabled());
        QCOMPARE(start->text(), QStringLiteral("重试加载"));
        QVERIFY(!page.findChild<QLabel *>("chargingPrice")->text().isEmpty());
        QCOMPARE(api.calls["order.start"], 0);
        start->click();
        QVERIFY(!start->isEnabled());
        QCOMPARE(api.calls["station.list"], 2);
        emit api.stationListCompleted(api.reply<StationListResult>("station.list")); // no matching station
        QVERIFY(start->isEnabled());
        QCOMPARE(start->text(), QStringLiteral("重试加载"));
        start->click();
        auto incomplete = api.reply<StationListResult>("station.list");
        incomplete.payload.reset();
        emit api.stationListCompleted(incomplete);
        QVERIFY(start->isEnabled());
        QCOMPARE(start->text(), QStringLiteral("重试加载"));
        start->click();
        emit api.stationListCompleted(api.reply<StationListResult>(
            "station.list", protocol::ErrorCode::InvalidSession));
        QCOMPARE(auth.count(), 1);
        QVERIFY(page.pileCode().isEmpty());
        QCOMPARE(api.calls["order.start"], 0);
    }

    void quoteRetryOnlyLoadsPriceUntilUserStartsAgain() {
        DeferredApi api;
        ChargingPage page;
        ChargingController controller(page, api);
        controller.activate();
        controller.prepare("PILE-A-01");
        emit api.currentOrderCompleted(api.reply<CurrentOrderResult>("order.current"));
        emit api.stationListCompleted(api.reply<StationListResult>(
            "station.list", protocol::ErrorCode::ServiceUnavailable));
        auto *start = page.findChild<QPushButton *>("chargingStartButton");
        QCOMPARE(start->text(), QStringLiteral("重试加载"));
        start->click();
        start->click(); // Disabled while loading; no duplicate requests or starts.
        QCOMPARE(api.calls["station.list"], 2);
        auto stations = api.reply<StationListResult>("station.list");
        protocol::StationDto station;
        station.stationId = 1;
        station.priceCentsPerKwh = 162;
        stations.payload->items = {station};
        emit api.stationListCompleted(stations);
        auto quote = api.reply<StationDetailResult>("station.detail");
        quote.payload->station = station;
        protocol::PileDto pile;
        pile.stationId = 1;
        pile.pileCode = "PILE-A-01";
        quote.payload->piles = {pile};
        emit api.stationDetailCompleted(quote);
        QVERIFY(start->isEnabled());
        QCOMPARE(start->text(), QStringLiteral("开始充电"));
        QCOMPARE(api.calls["order.start"], 0);
        start->click();
        emit api.currentOrderCompleted(api.reply<CurrentOrderResult>("order.current"));
        QCOMPARE(api.calls["order.start"], 1);
    }

    void oldOrUnknownPricingRuleDoesNotInventPeakPrice() {
        ChargingPage page;
        page.show();
        page.prepare("PILE-A-01");
        protocol::StationDto quote;
        quote.stationId = 1; quote.priceCentsPerKwh = 137;
        for (const auto &rule : {QString(), QStringLiteral("FUTURE_RULE")}) {
            quote.pricingRule = rule;
            page.showQuote(quote);
            QCOMPARE(page.findChild<QLabel *>("chargingPrice")->text(),
                     QStringLiteral("当前参考单价：¥1.37/度"));
            verifyPricingRules(page, false);
        }
    }

    void pricingRulesCloseWhenQuoteOrSessionChanges() {
        ChargingPage page;
        page.show();
        page.prepare("PILE-A-01");
        protocol::StationDto quote;
        quote.priceCentsPerKwh = 162;
        quote.pricingRule = QString::fromLatin1(protocol::DemoPeakPricingRule);
        page.showQuote(quote);
        auto *help = page.findChild<QToolButton *>("chargingPricingInfoButton");
        help->click();
        QVERIFY(page.findChild<QDialog *>("pricingRulesDialog")->isVisible());
        page.prepare("PILE-B-02");
        QTRY_VERIFY(!page.findChild<QDialog *>("pricingRulesDialog"));
        QVERIFY(!help->isVisible());
        page.showQuote(quote);
        help->click();
        QVERIFY(page.findChild<QDialog *>("pricingRulesDialog")->isVisible());
        page.hide();
        QTRY_VERIFY(!page.findChild<QDialog *>("pricingRulesDialog"));
        page.show();
        help->click();
        QVERIFY(page.findChild<QDialog *>("pricingRulesDialog")->isVisible());
        page.reset();
        QTRY_VERIFY(!page.findChild<QDialog *>("pricingRulesDialog"));
        QVERIFY(!help->isVisible());
    }

    void invalidVerificationCodeDoesNotCallApi_data() {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("message");
        QTest::newRow("empty") << QString() << QStringLiteral("请输入6位数字验证码");
        QTest::newRow("short") << QStringLiteral("12345") << QStringLiteral("请输入6位数字验证码");
        QTest::newRow("letters") << QStringLiteral("abcdef") << QStringLiteral("请输入6位数字验证码");
        QTest::newRow("wrong") << QStringLiteral("654321") << QStringLiteral("验证码不正确，请重试");
    }

    void invalidVerificationCodeDoesNotCallApi() {
        QFETCH(QString, code);
        QFETCH(QString, message);
        DeferredApi api;
        MainWindow window(api);
        window.show();
        window.findChild<QLineEdit *>(QStringLiteral("phoneInput"))->setText(QStringLiteral("13800000001"));
        auto *input = window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"));
        input->setText(code);
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        QCOMPARE(api.serial, 0);
        QCOMPARE(window.findChild<QLabel *>(QStringLiteral("loginErrorLabel"))->text(), message);
        QVERIFY(window.findChild<LoginPage *>()->isVisible());
        QVERIFY(input->isEnabled());
    }

    void sendCodeIsOnlyADemoHint() {
        DeferredApi api;
        MainWindow window(api);
        window.show();
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
        auto *input = window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"));
        auto *send = window.findChild<QPushButton *>(QStringLiteral("sendVerificationCodeButton"));
        auto *error = window.findChild<QLabel *>(QStringLiteral("loginErrorLabel"));
        auto *hint = window.findChild<QLabel *>(QStringLiteral("verificationCodeHint"));
        send->click();
        QCOMPARE(error->text(), QStringLiteral("请输入11位数字手机号"));
        QVERIFY(!hint->text().contains(QStringLiteral("123456")));
        phone->setText(QStringLiteral("13800000001"));
        send->click();
        QVERIFY(error->isHidden());
        QVERIFY(hint->text().contains(QStringLiteral("123456")));
        QVERIFY(hint->text().contains(QStringLiteral("暂不发送短信")));
        QVERIFY(input->text().isEmpty());
        QCOMPARE(api.serial, 0);
        QVERIFY(send->isEnabled());
    }

    void verificationLoginFailureRestoresAllControls_data() {
        QTest::addColumn<int>("errorCode");
        QTest::newRow("network") << protocol::ErrorCode::ServiceUnavailable;
        QTest::newRow("frozen") << protocol::ErrorCode::Forbidden;
    }

    void verificationLoginFailureRestoresAllControls() {
        QFETCH(int, errorCode);
        DeferredApi api;
        MainWindow window(api);
        auto *page = window.findChild<LoginPage *>();
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
        auto *input = window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"));
        auto *send = window.findChild<QPushButton *>(QStringLiteral("sendVerificationCodeButton"));
        auto *submit = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
        phone->setText(QStringLiteral("13800000001"));
        input->setText(QStringLiteral("123456"));
        submit->click();
        QCOMPARE(api.calls[QStringLiteral("auth.user.login")], 1);
        QVERIFY(!phone->isEnabled());
        QVERIFY(!input->isEnabled());
        QVERIFY(!send->isEnabled());
        QVERIFY(!submit->isEnabled());
        // Even direct repeated signals must not start another request while pending.
        page->loginRequested(phone->text(), input->text());
        QCOMPARE(api.calls[QStringLiteral("auth.user.login")], 1);
        emit api.loginCompleted(api.reply<LoginResult>("auth.user.login", errorCode));
        QVERIFY(phone->isEnabled());
        QVERIFY(input->isEnabled());
        QVERIFY(send->isEnabled());
        QVERIFY(submit->isEnabled());
        QVERIFY(!window.findChild<QLabel *>(QStringLiteral("loginErrorLabel"))->text().isEmpty());
        QCOMPARE(input->text(), QStringLiteral("123456"));
        submit->click();
        QCOMPARE(api.calls[QStringLiteral("auth.user.login")], 2);
    }

    void sessionExpiryDiscardsOtherPagesPendingResults() {
        DeferredApi api;
        MainWindow window(api);
        login(window, api);
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
        tabs->setCurrentIndex(4);
        window.findChild<QPushButton *>("profileOrdersButton")->click();
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
        tabs->setCurrentIndex(4);
        window.findChild<QPushButton *>("profileOrdersButton")->click();
        QCOMPARE(api.calls[QStringLiteral("order.list")], 4); // Home history + orders for each session.
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
        tabs->setCurrentIndex(4);
        window.findChild<QPushButton *>("profileOrdersButton")->click();
        emit api.orderListCompleted(api.reply<OrderListResult>("order.list", protocol::ErrorCode::ServiceUnavailable));
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("orderRefreshButton"))->isEnabled());
        window.findChild<OrderPage *>()->refreshRequested();
        QCOMPARE(api.calls[QStringLiteral("order.list")], 3); // Home history + orders + explicit retry.
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
        tabs->setCurrentIndex(4);
        window.findChild<QPushButton *>("profileOrdersButton")->click();
        emit api.orderListCompleted(api.reply<OrderListResult>("order.list"));
        window.findChild<OrderPage *>()->navigationRequested(1);
        auto result = api.reply<StationDetailResult>("station.detail");
        window.findChild<QPushButton *>("ordersBackButton")->click();
        emit api.stationDetailCompleted(result);
        QCOMPARE(tabs->currentIndex(), 4);
    }
};

QTEST_MAIN(UiRecoveryTests)
#include "ui_recovery_tests.moc"
