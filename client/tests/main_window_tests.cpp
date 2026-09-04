#include "api/mock_charging_api.h"
#include "ui/main_window.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QtTest>

using namespace charging::client;

class MainWindowTests : public QObject {
    Q_OBJECT

private slots:
    void constructsCodeOnlyLoginPage();
    void existingUserCanLogin();
    void newUserIsAutomaticallyRegistered();
    void invalidPhoneStaysOnLoginPage();
    void authenticatedShellHasFiveBottomEntries();
    void chargingHomeListsFiltersAndOpensStationDetail();
    void reservationAppearsOnHomeAndCanBeCancelled();
    void ordersPageShowsHistoryDetailAndReservationChanges();
    void profileCanRefreshUpdateNicknameAndRecharge();
    void profileRejectsInvalidRechargeAmount();
    void logoutReturnsToLoginPage();
};

namespace {

void loginFixtureUser(MainWindow &window)
{
    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phoneInput->setText(QStringLiteral("13800000001"));
    QTest::mouseClick(loginButton, Qt::LeftButton);
    auto *homePage = window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"));
    QTRY_VERIFY(homePage->isVisible());
}

}  // namespace

void MainWindowTests::constructsCodeOnlyLoginPage()
{
    MockChargingApi api;
    MainWindow window(api);

    QCOMPARE(window.objectName(), QStringLiteral("mainWindow"));
    QCOMPARE(window.windowTitle(), QStringLiteral("新能源汽车充电服务"));

    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("applicationPages"));
    auto *loginPage = window.findChild<QWidget *>(QStringLiteral("loginPage"));
    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));

    QVERIFY(pages != nullptr);
    QVERIFY(loginPage != nullptr);
    QVERIFY(phoneInput != nullptr);
    QVERIFY(loginButton != nullptr);
    QCOMPARE(pages->currentWidget(), loginPage);
    QCOMPARE(loginButton->text(), QStringLiteral("登录"));
}

void MainWindowTests::existingUserCanLogin()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();

    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    auto *homePage = window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"));
    auto *welcomeLabel = window.findChild<QLabel *>(QStringLiteral("welcomeLabel"));
    auto *noticeLabel = window.findChild<QLabel *>(QStringLiteral("loginNoticeLabel"));

    phoneInput->setText(QStringLiteral("13800000001"));
    QTest::mouseClick(loginButton, Qt::LeftButton);

    QTRY_VERIFY(homePage->isVisible());
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，演示用户0001"));
    QCOMPARE(noticeLabel->text(), QStringLiteral("登录成功"));
}

void MainWindowTests::newUserIsAutomaticallyRegistered()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();

    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    auto *homePage = window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"));
    auto *welcomeLabel = window.findChild<QLabel *>(QStringLiteral("welcomeLabel"));
    auto *noticeLabel = window.findChild<QLabel *>(QStringLiteral("loginNoticeLabel"));

    phoneInput->setText(QStringLiteral("13912345678"));
    QTest::mouseClick(loginButton, Qt::LeftButton);

    QTRY_VERIFY(homePage->isVisible());
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，用户5678"));
    QCOMPARE(noticeLabel->text(), QStringLiteral("账号已自动注册并登录"));
}

void MainWindowTests::invalidPhoneStaysOnLoginPage()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();

    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("applicationPages"));
    auto *loginPage = window.findChild<QWidget *>(QStringLiteral("loginPage"));
    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    auto *errorLabel = window.findChild<QLabel *>(QStringLiteral("loginErrorLabel"));

    phoneInput->setText(QStringLiteral("123"));
    QTest::mouseClick(loginButton, Qt::LeftButton);

    QCOMPARE(pages->currentWidget(), loginPage);
    QVERIFY(errorLabel->isVisible());
    QCOMPARE(errorLabel->text(), QStringLiteral("请输入11位数字手机号"));
    QVERIFY(loginButton->isEnabled());
}

void MainWindowTests::authenticatedShellHasFiveBottomEntries()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    QVERIFY(navigation != nullptr);
    QCOMPARE(navigation->tabPosition(), QTabWidget::South);
    QCOMPARE(navigation->count(), 5);
    QCOMPARE(navigation->tabText(0), QStringLiteral("充电"));
    QCOMPARE(navigation->tabText(1), QStringLiteral("订单"));
    QCOMPARE(navigation->tabText(2), QStringLiteral("扫一扫"));
    QCOMPARE(navigation->tabText(3), QStringLiteral("客服助理"));
    QCOMPARE(navigation->tabText(4), QStringLiteral("我的"));
}

void MainWindowTests::chargingHomeListsFiltersAndOpensStationDetail()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) != nullptr);
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_2")) != nullptr);
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("stationRecommended_1")) != nullptr);
    auto *locationTitle =
        window.findChild<QLabel *>(QStringLiteral("stationLocationTitle"));
    auto *locationSummary =
        window.findChild<QLabel *>(QStringLiteral("stationLocationSummary"));
    auto *filterTitle =
        window.findChild<QLabel *>(QStringLiteral("stationFilterTitle"));
    auto *filterHint =
        window.findChild<QLabel *>(QStringLiteral("stationFilterHint"));
    QVERIFY(locationTitle != nullptr);
    QVERIFY(locationSummary != nullptr);
    QVERIFY(filterTitle != nullptr);
    QVERIFY(filterHint != nullptr);
    QCOMPARE(locationTitle->text(), QStringLiteral("当前位置"));
    QVERIFY(locationSummary->text().contains(QStringLiteral("演示位置")));
    QCOMPARE(filterTitle->text(), QStringLiteral("查找充电站"));
    QVERIFY(filterHint->text().contains(QStringLiteral("模糊匹配")));

    auto *detailButton =
        window.findChild<QPushButton *>(QStringLiteral("stationDetailButton_1"));
    QVERIFY(detailButton != nullptr);
    QTest::mouseClick(detailButton, Qt::LeftButton);

    auto *detailPage =
        window.findChild<QWidget *>(QStringLiteral("stationDetailPage"));
    auto *detailName =
        window.findChild<QLabel *>(QStringLiteral("stationDetailName"));
    QTRY_VERIFY(detailPage->isVisible());
    QTRY_COMPARE(detailName->text(), QStringLiteral("浑南演示充电站"));
    auto *idleStatus =
        window.findChild<QLabel *>(QStringLiteral("pileStatus_PILE-A-01"));
    auto *chargingStatus =
        window.findChild<QLabel *>(QStringLiteral("pileStatus_PILE-A-02"));
    QVERIFY(idleStatus != nullptr);
    QVERIFY(chargingStatus != nullptr);
    QCOMPARE(idleStatus->text(), QStringLiteral("闲置 · 可预约"));
    QCOMPARE(chargingStatus->text(), QStringLiteral("使用中"));
    auto *idleReserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-A-01"));
    auto *chargingReserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-A-02"));
    QVERIFY(idleReserveButton->isEnabled());
    QVERIFY(!chargingReserveButton->isEnabled());

    auto *backButton =
        window.findChild<QPushButton *>(QStringLiteral("stationDetailBackButton"));
    QTest::mouseClick(backButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) != nullptr);

    auto *regionInput =
        window.findChild<QLineEdit *>(QStringLiteral("stationRegionInput"));
    auto *keywordInput =
        window.findChild<QLineEdit *>(QStringLiteral("stationKeywordInput"));
    auto *refreshButton =
        window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"));
    QVERIFY(keywordInput->placeholderText().contains(QStringLiteral("和平")));
    QVERIFY(regionInput->placeholderText().contains(QStringLiteral("完整区域名")));

    keywordInput->setText(QStringLiteral("和平"));
    QTest::mouseClick(refreshButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) == nullptr);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_2")) != nullptr);
    QTRY_VERIFY(refreshButton->isEnabled());

    keywordInput->clear();
    regionInput->setText(QStringLiteral("和平区"));
    QTest::mouseClick(refreshButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) == nullptr);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_2")) != nullptr);
    QTRY_VERIFY(refreshButton->isEnabled());

    auto *message =
        window.findChild<QLabel *>(QStringLiteral("stationListMessage"));
    regionInput->clear();
    keywordInput->setText(QStringLiteral("不存在的站点"));
    QTest::mouseClick(refreshButton, Qt::LeftButton);
    QTRY_COMPARE(message->text(), QStringLiteral("没有找到符合条件的充电站"));
    QVERIFY(message->isVisible());
}

void MainWindowTests::reservationAppearsOnHomeAndCanBeCancelled()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) != nullptr);
    auto *stationOneDetail =
        window.findChild<QPushButton *>(QStringLiteral("stationDetailButton_1"));
    QTest::mouseClick(stationOneDetail, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-A-01")) != nullptr);
    auto *reserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-A-01"));
    QVERIFY(reserveButton->isEnabled());
    QTest::mouseClick(reserveButton, Qt::LeftButton);

    auto *currentOrderCard =
        window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    auto *currentOrderSummary =
        window.findChild<QLabel *>(QStringLiteral("currentOrderSummary"));
    auto *actionMessage =
        window.findChild<QLabel *>(QStringLiteral("stationActionMessage"));
    QTRY_VERIFY(currentOrderCard->isVisible());
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("PILE-A-01")));
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("预约中")));
    QCOMPARE(actionMessage->text(), QStringLiteral("预约成功"));

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_2")) != nullptr);
    auto *stationTwoDetail =
        window.findChild<QPushButton *>(QStringLiteral("stationDetailButton_2"));
    QTest::mouseClick(stationTwoDetail, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-B-02")) != nullptr);
    auto *secondReserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-B-02"));
    QTest::mouseClick(secondReserveButton, Qt::LeftButton);
    QTRY_VERIFY(currentOrderCard->isVisible());
    QCOMPARE(actionMessage->text(),
             QStringLiteral("您已有进行中的订单，请先处理当前订单"));

    auto *cancelButton =
        window.findChild<QPushButton *>(QStringLiteral("cancelReservationButton"));
    QVERIFY(cancelButton->isVisible());
    QTest::mouseClick(cancelButton, Qt::LeftButton);
    QTRY_COMPARE(actionMessage->text(), QStringLiteral("预约已取消"));
    QTRY_VERIFY(!currentOrderCard->isVisible());

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) != nullptr);
    stationOneDetail =
        window.findChild<QPushButton *>(QStringLiteral("stationDetailButton_1"));
    QTest::mouseClick(stationOneDetail, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-A-01")) != nullptr);
    reserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-A-01"));
    QVERIFY(reserveButton->isEnabled());
}

void MainWindowTests::ordersPageShowsHistoryDetailAndReservationChanges()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    navigation->setCurrentIndex(1);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_101")) != nullptr);
    auto *completedStatus =
        window.findChild<QLabel *>(QStringLiteral("orderStatus_101"));
    QCOMPARE(completedStatus->text(), QStringLiteral("已完成"));

    auto *historyDetailButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailButton_101"));
    QTest::mouseClick(historyDetailButton, Qt::LeftButton);
    auto *detailPage =
        window.findChild<QWidget *>(QStringLiteral("orderDetailPage"));
    auto *detailNumber =
        window.findChild<QLabel *>(QStringLiteral("orderDetailNumber"));
    auto *detailBody =
        window.findChild<QLabel *>(QStringLiteral("orderDetailBody"));
    QTRY_VERIFY(detailPage->isVisible());
    QVERIFY(detailNumber->text().contains(QStringLiteral("DEMO-COMPLETED-101")));
    QVERIFY(detailBody->text().contains(QStringLiteral("订单金额：¥6.75")));
    QVERIFY(detailBody->text().contains(QStringLiteral("充电量：5.00 度")));

    auto *orderBackButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailBackButton"));
    QTest::mouseClick(orderBackButton, Qt::LeftButton);
    navigation->setCurrentIndex(0);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) != nullptr);
    auto *stationRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"));
    QTRY_VERIFY(stationRefreshButton->isEnabled());
    auto *stationDetailButton =
        window.findChild<QPushButton *>(QStringLiteral("stationDetailButton_1"));
    QTest::mouseClick(stationDetailButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-A-01")) != nullptr);
    auto *reserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-A-01"));
    QTest::mouseClick(reserveButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("currentOrderCard"))->isVisible());

    navigation->setCurrentIndex(1);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
    auto *reservedStatus =
        window.findChild<QLabel *>(QStringLiteral("orderStatus_1001"));
    QCOMPARE(reservedStatus->text(), QStringLiteral("预约中"));
    auto *reservedDetailButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailButton_1001"));
    QTest::mouseClick(reservedDetailButton, Qt::LeftButton);
    auto *detailStatus =
        window.findChild<QLabel *>(QStringLiteral("orderDetailStatus"));
    auto *cancelButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailCancelButton"));
    QTRY_COMPARE(detailStatus->text(), QStringLiteral("预约中"));
    QVERIFY(cancelButton->isVisible());
    QTest::mouseClick(cancelButton, Qt::LeftButton);

    auto *orderListPage =
        window.findChild<QWidget *>(QStringLiteral("orderListPage"));
    auto *orderMessage =
        window.findChild<QLabel *>(QStringLiteral("orderListMessage"));
    QTRY_VERIFY(orderListPage->isVisible());
    auto *orderRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("orderRefreshButton"));
    QTRY_VERIFY(orderRefreshButton->isEnabled());
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
    reservedStatus = window.findChild<QLabel *>(QStringLiteral("orderStatus_1001"));
    QTRY_COMPARE(reservedStatus->text(), QStringLiteral("已取消"));
    QCOMPARE(orderMessage->text(), QStringLiteral("预约已取消，订单状态已刷新"));
}

void MainWindowTests::profileCanRefreshUpdateNicknameAndRecharge()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    auto *profilePage = window.findChild<QWidget *>(QStringLiteral("profilePage"));
    auto *nicknameLabel =
        window.findChild<QLabel *>(QStringLiteral("profileNicknameLabel"));
    auto *phoneLabel = window.findChild<QLabel *>(QStringLiteral("profilePhoneLabel"));
    auto *balanceLabel = window.findChild<QLabel *>(QStringLiteral("profileBalanceLabel"));
    auto *messageLabel = window.findChild<QLabel *>(QStringLiteral("profileMessageLabel"));
    auto *nicknameInput = window.findChild<QLineEdit *>(QStringLiteral("nicknameInput"));
    auto *saveButton =
        window.findChild<QPushButton *>(QStringLiteral("saveNicknameButton"));
    auto *amountInput =
        window.findChild<QLineEdit *>(QStringLiteral("rechargeAmountInput"));
    auto *rechargeButton = window.findChild<QPushButton *>(QStringLiteral("rechargeButton"));

    navigation->setCurrentWidget(profilePage);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));
    QCOMPARE(nicknameLabel->text(), QStringLiteral("演示用户0001"));
    QCOMPARE(phoneLabel->text(), QStringLiteral("手机号：13800000001"));
    QCOMPARE(balanceLabel->text(), QStringLiteral("¥200.00"));

    nicknameInput->setText(QStringLiteral("新的昵称"));
    QTest::mouseClick(saveButton, Qt::LeftButton);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("昵称已更新"));
    QCOMPARE(nicknameLabel->text(), QStringLiteral("新的昵称"));

    amountInput->setText(QStringLiteral("10"));
    QTest::mouseClick(rechargeButton, Qt::LeftButton);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("充值成功，余额已刷新"));
    QCOMPARE(balanceLabel->text(), QStringLiteral("¥210.00"));
}

void MainWindowTests::profileRejectsInvalidRechargeAmount()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    auto *profilePage = window.findChild<QWidget *>(QStringLiteral("profilePage"));
    auto *messageLabel = window.findChild<QLabel *>(QStringLiteral("profileMessageLabel"));
    auto *amountInput =
        window.findChild<QLineEdit *>(QStringLiteral("rechargeAmountInput"));
    auto *rechargeButton = window.findChild<QPushButton *>(QStringLiteral("rechargeButton"));

    navigation->setCurrentWidget(profilePage);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));
    amountInput->setText(QStringLiteral("0"));
    QTest::mouseClick(rechargeButton, Qt::LeftButton);

    QCOMPARE(messageLabel->text(),
             QStringLiteral("请输入0.01元到10000元之间的有效金额"));
}

void MainWindowTests::logoutReturnsToLoginPage()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("applicationPages"));
    auto *loginPage = window.findChild<QWidget *>(QStringLiteral("loginPage"));
    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    auto *profilePage = window.findChild<QWidget *>(QStringLiteral("profilePage"));
    auto *messageLabel = window.findChild<QLabel *>(QStringLiteral("profileMessageLabel"));
    auto *logoutButton = window.findChild<QPushButton *>(QStringLiteral("logoutButton"));

    navigation->setCurrentWidget(profilePage);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));
    QTest::mouseClick(logoutButton, Qt::LeftButton);

    QTRY_COMPARE(pages->currentWidget(), loginPage);
}

QTEST_MAIN(MainWindowTests)

#include "main_window_tests.moc"
