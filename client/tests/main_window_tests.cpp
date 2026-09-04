#include "api/mock_charging_api.h"
#include "ui/main_window.h"
#include "ui/scan_controller.h"

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
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
    void simulatedScanStartsChargingAndRefreshesHome();
    void scannerAdapterCanSubmitDecodedPileCode();
    void chargingProgressCanRefreshAndStopWithConfirmation();
    void pendingOrderLinksRechargeAndCanBeSettled();
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
    auto *tabBar = navigation->tabBar();
    QVERIFY(tabBar->expanding());
    QVERIFY(!tabBar->usesScrollButtons());
    QTRY_VERIFY(tabBar->width() > 0);
    int occupiedWidth = 0;
    int minimumTabWidth = tabBar->tabRect(0).width();
    int maximumTabWidth = minimumTabWidth;
    for (int index = 0; index < tabBar->count(); ++index) {
        const int width = tabBar->tabRect(index).width();
        occupiedWidth += width;
        minimumTabWidth = qMin(minimumTabWidth, width);
        maximumTabWidth = qMax(maximumTabWidth, width);
    }
    QVERIFY(occupiedWidth >= tabBar->width() - 2);
    QVERIFY(maximumTabWidth - minimumTabWidth <= 1);
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

    auto *stationCard =
        window.findChild<QWidget *>(QStringLiteral("stationCard_1"));
    QVERIFY(stationCard != nullptr);
    QVERIFY(stationCard->toolTip().isEmpty());
    auto *homeScrollArea = window.findChild<QScrollArea *>(
        QStringLiteral("stationHomeScrollArea"));
    auto *queryCard =
        window.findChild<QWidget *>(QStringLiteral("stationQueryCard"));
    auto *currentOrderCard =
        window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    QVERIFY(homeScrollArea != nullptr);
    QVERIFY(homeScrollArea->widget()->isAncestorOf(queryCard));
    QVERIFY(homeScrollArea->widget()->isAncestorOf(currentOrderCard));
    QVERIFY(window.findChild<QScrollArea *>(
                QStringLiteral("stationListScrollArea")) == nullptr);
    QVERIFY(window.findChild<QPushButton *>(
                QStringLiteral("stationDetailButton_1")) == nullptr);
    QVERIFY(window.findChild<QLabel *>(
                QStringLiteral("stationDetailHint_1")) != nullptr);
    QTest::mouseClick(stationCard, Qt::LeftButton, Qt::NoModifier, QPoint(12, 12));

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
    auto *stationOneCard =
        window.findChild<QWidget *>(QStringLiteral("stationCard_1"));
    QTest::mouseClick(stationOneCard, Qt::LeftButton);
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
    auto *stationTwoCard =
        window.findChild<QWidget *>(QStringLiteral("stationCard_2"));
    QTest::mouseClick(stationTwoCard, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-B-02")) != nullptr);
    auto *secondReserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-B-02"));
    QTest::mouseClick(secondReserveButton, Qt::LeftButton);
    QTRY_VERIFY(currentOrderCard->isVisible());
    QCOMPARE(actionMessage->text(),
             QStringLiteral("您已有进行中的订单，请先处理当前订单"));

    auto *reservationScanButton = window.findChild<QPushButton *>(
        QStringLiteral("startReservedChargingButton"));
    QVERIFY(reservationScanButton->isVisible());
    QTest::mouseClick(reservationScanButton, Qt::LeftButton);
    auto *navigation =
        window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    QCOMPARE(navigation->currentIndex(), 2);
    auto *preparedPileCode =
        window.findChild<QLineEdit *>(QStringLiteral("scanPileCodeInput"));
    QCOMPARE(preparedPileCode->text(), QStringLiteral("PILE-A-01"));
    navigation->setCurrentIndex(0);
    QTRY_VERIFY(currentOrderCard->isVisible());

    auto *cancelButton =
        window.findChild<QPushButton *>(QStringLiteral("cancelReservationButton"));
    QVERIFY(cancelButton->isVisible());
    QTest::mouseClick(cancelButton, Qt::LeftButton);
    QTRY_COMPARE(actionMessage->text(), QStringLiteral("预约已取消"));
    QTRY_VERIFY(!currentOrderCard->isVisible());

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationCard_1")) != nullptr);
    stationOneCard =
        window.findChild<QWidget *>(QStringLiteral("stationCard_1"));
    QTest::mouseClick(stationOneCard, Qt::LeftButton);
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

    auto *historyCard =
        window.findChild<QWidget *>(QStringLiteral("orderCard_101"));
    QVERIFY(historyCard != nullptr);
    QVERIFY(historyCard->toolTip().isEmpty());
    QVERIFY(!historyCard->property("currentOrderHighlighted").toBool());
    QVERIFY(window.findChild<QPushButton *>(
                QStringLiteral("orderDetailButton_101")) == nullptr);
    QVERIFY(window.findChild<QLabel *>(
                QStringLiteral("orderDetailHint_101")) != nullptr);
    QTest::mouseClick(historyCard, Qt::LeftButton, Qt::NoModifier, QPoint(12, 12));
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
    auto *stationCard =
        window.findChild<QWidget *>(QStringLiteral("stationCard_1"));
    QTest::mouseClick(stationCard, Qt::LeftButton);
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
    auto *reservedCard =
        window.findChild<QWidget *>(QStringLiteral("orderCard_1001"));
    QVERIFY(reservedCard->property("currentOrderHighlighted").toBool());
    QTest::mouseClick(reservedCard, Qt::LeftButton);
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

void MainWindowTests::simulatedScanStartsChargingAndRefreshesHome()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    navigation->setCurrentIndex(2);
    auto *scanPage = window.findChild<QWidget *>(QStringLiteral("scanPage"));
    auto *pileCodeInput =
        window.findChild<QLineEdit *>(QStringLiteral("scanPileCodeInput"));
    auto *startButton =
        window.findChild<QPushButton *>(QStringLiteral("scanStartButton"));
    auto *scanMessage = window.findChild<QLabel *>(QStringLiteral("scanMessage"));
    auto *adapterHint =
        window.findChild<QLabel *>(QStringLiteral("scanAdapterHint"));
    QVERIFY(scanPage->isVisible());
    QVERIFY(adapterHint->text().contains(QStringLiteral("独立扫码适配器")));

    QTest::mouseClick(startButton, Qt::LeftButton);
    QCOMPARE(scanMessage->text(), QStringLiteral("请输入有效的充电桩编号"));
    pileCodeInput->setText(QStringLiteral("PILE-A-01"));
    QTest::mouseClick(startButton, Qt::LeftButton);

    auto *homePage =
        window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"));
    auto *currentOrderCard =
        window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    auto *currentOrderSummary =
        window.findChild<QLabel *>(QStringLiteral("currentOrderSummary"));
    auto *actionMessage =
        window.findChild<QLabel *>(QStringLiteral("stationActionMessage"));
    QTRY_VERIFY(homePage->isVisible());
    QTRY_VERIFY(currentOrderCard->isVisible());
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("充电中")));
    QCOMPARE(actionMessage->text(), QStringLiteral("充电已开始"));

    auto *stationRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"));
    QTRY_VERIFY(stationRefreshButton->isEnabled());
    auto *stationCard =
        window.findChild<QWidget *>(QStringLiteral("stationCard_1"));
    QTest::mouseClick(stationCard, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QLabel *>(
                    QStringLiteral("pileStatus_PILE-A-01")) != nullptr);
    auto *pileStatus =
        window.findChild<QLabel *>(QStringLiteral("pileStatus_PILE-A-01"));
    QCOMPARE(pileStatus->text(), QStringLiteral("使用中"));

    navigation->setCurrentIndex(2);
    pileCodeInput->setText(QStringLiteral("PILE-B-02"));
    QTest::mouseClick(startButton, Qt::LeftButton);
    QTRY_COMPARE(scanMessage->text(),
                 QStringLiteral("您已有充电中的订单，请先处理当前订单"));
    QTRY_VERIFY(homePage->isVisible());
}

void MainWindowTests::scannerAdapterCanSubmitDecodedPileCode()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *scanController = window.findChild<ScanController *>();
    QVERIFY(scanController != nullptr);
    scanController->submitPileCode(QStringLiteral("  PILE-A-01  "));

    auto *currentOrderCard =
        window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    auto *currentOrderSummary =
        window.findChild<QLabel *>(QStringLiteral("currentOrderSummary"));
    QTRY_VERIFY(currentOrderCard->isVisible());
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("PILE-A-01")));
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("充电中")));
}

void MainWindowTests::chargingProgressCanRefreshAndStopWithConfirmation()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    navigation->setCurrentIndex(2);
    auto *pileCodeInput =
        window.findChild<QLineEdit *>(QStringLiteral("scanPileCodeInput"));
    auto *startButton =
        window.findChild<QPushButton *>(QStringLiteral("scanStartButton"));
    pileCodeInput->setText(QStringLiteral("PILE-A-01"));
    QTest::mouseClick(startButton, Qt::LeftButton);

    auto *progressLabel =
        window.findChild<QLabel *>(QStringLiteral("currentOrderProgress"));
    auto *progressButton =
        window.findChild<QPushButton *>(QStringLiteral("chargingProgressButton"));
    auto *stopButton =
        window.findChild<QPushButton *>(QStringLiteral("chargingStopButton"));
    QTRY_VERIFY(progressButton->isVisible());
    QVERIFY(stopButton->isVisible());
    QTest::mouseClick(progressButton, Qt::LeftButton);
    QTRY_VERIFY(progressButton->isEnabled());
    QVERIFY(progressLabel->text().contains(QStringLiteral("1 分钟")));
    QVERIFY(progressLabel->text().contains(QStringLiteral("当前预估金额")));

    QString confirmButtonText;
    QString cancelButtonText;
    int confirmationWidth = 0;
    QTimer::singleShot(10, &window, [&]() {
        for (QWidget *topLevel : QApplication::topLevelWidgets()) {
            auto *confirmation = qobject_cast<QMessageBox *>(topLevel);
            if (confirmation != nullptr) {
                confirmButtonText =
                    confirmation->button(QMessageBox::Yes)->text();
                cancelButtonText =
                    confirmation->button(QMessageBox::No)->text();
                confirmationWidth = confirmation->width();
                confirmation->button(QMessageBox::Yes)->click();
                return;
            }
        }
    });
    QTest::mouseClick(stopButton, Qt::LeftButton);

    auto *currentOrderCard =
        window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    auto *actionMessage =
        window.findChild<QLabel *>(QStringLiteral("stationActionMessage"));
    QTRY_VERIFY(!currentOrderCard->isVisible());
    QVERIFY(actionMessage->text().contains(QStringLiteral("充电已结束并自动结算")));
    QCOMPARE(confirmButtonText, QStringLiteral("结束充电"));
    QCOMPARE(cancelButtonText, QStringLiteral("取消"));
    QVERIFY(confirmationWidth <= window.width());

    navigation->setCurrentIndex(1);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
    auto *status = window.findChild<QLabel *>(QStringLiteral("orderStatus_1001"));
    QCOMPARE(status->text(), QStringLiteral("已完成"));
}

void MainWindowTests::pendingOrderLinksRechargeAndCanBeSettled()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();

    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phoneInput->setText(QStringLiteral("13912345678"));
    QTest::mouseClick(loginButton, Qt::LeftButton);
    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    QTRY_VERIFY(navigation->isVisible());

    navigation->setCurrentIndex(2);
    auto *pileCodeInput =
        window.findChild<QLineEdit *>(QStringLiteral("scanPileCodeInput"));
    auto *startButton =
        window.findChild<QPushButton *>(QStringLiteral("scanStartButton"));
    pileCodeInput->setText(QStringLiteral("PILE-A-01"));
    QTest::mouseClick(startButton, Qt::LeftButton);
    auto *currentOrderCard =
        window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    QTRY_VERIFY(currentOrderCard->isVisible());

    navigation->setCurrentIndex(1);
    auto *orderRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("orderRefreshButton"));
    QTRY_VERIFY(orderRefreshButton->isEnabled());
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
    auto *orderCard =
        window.findChild<QWidget *>(QStringLiteral("orderCard_1001"));
    QTest::mouseClick(orderCard, Qt::LeftButton);
    auto *detailStopButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailStopButton"));
    auto *detailProgressButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailProgressButton"));
    auto *detailBody =
        window.findChild<QLabel *>(QStringLiteral("orderDetailBody"));
    auto *detailMessage =
        window.findChild<QLabel *>(QStringLiteral("orderDetailMessage"));
    QTRY_VERIFY(detailProgressButton->isVisible());
    QTest::mouseClick(detailProgressButton, Qt::LeftButton);
    QTRY_VERIFY(detailProgressButton->isEnabled());
    QVERIFY(detailBody->text().contains(QStringLiteral("充电时长：1分钟")));
    QCOMPARE(detailMessage->text(), QStringLiteral("充电进度已刷新"));
    QTRY_VERIFY(detailStopButton->isVisible());
    QTimer::singleShot(10, &window, []() {
        for (QWidget *topLevel : QApplication::topLevelWidgets()) {
            auto *confirmation = qobject_cast<QMessageBox *>(topLevel);
            if (confirmation != nullptr) {
                confirmation->button(QMessageBox::Yes)->click();
                return;
            }
        }
    });
    QTest::mouseClick(detailStopButton, Qt::LeftButton);

    auto *orderMessage =
        window.findChild<QLabel *>(QStringLiteral("orderListMessage"));
    QTRY_VERIFY(orderMessage->text().contains(QStringLiteral("余额不足")));

    navigation->setCurrentIndex(0);
    auto *stationRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"));
    QTRY_VERIFY(stationRefreshButton->isEnabled());
    QTRY_VERIFY(window.findChild<QWidget *>(
                    QStringLiteral("stationCard_1")) != nullptr);
    auto *stationCard =
        window.findChild<QWidget *>(QStringLiteral("stationCard_1"));
    QTest::mouseClick(stationCard, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-A-01")) != nullptr);
    auto *reserveButton = window.findChild<QPushButton *>(
        QStringLiteral("reserveButton_PILE-A-01"));
    QTest::mouseClick(reserveButton, Qt::LeftButton);
    QTRY_COMPARE(navigation->currentIndex(), 1);
    QTRY_VERIFY(orderRefreshButton->isEnabled());
    auto *pendingStatus =
        window.findChild<QLabel *>(QStringLiteral("orderStatus_1001"));
    QTRY_COMPARE(pendingStatus->text(), QStringLiteral("待支付"));

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
    orderCard = window.findChild<QWidget *>(QStringLiteral("orderCard_1001"));
    QTest::mouseClick(orderCard, Qt::LeftButton);
    auto *payButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailPayButton"));
    auto *rechargeLink =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailRechargeButton"));
    QTRY_VERIFY(payButton->isVisible());
    QVERIFY(rechargeLink->isVisible());
    QTest::mouseClick(payButton, Qt::LeftButton);
    QTRY_VERIFY(detailMessage->text().contains(QStringLiteral("余额不足")));

    QTest::mouseClick(rechargeLink, Qt::LeftButton);
    QCOMPARE(navigation->currentIndex(), 4);
    auto *rechargeInput =
        window.findChild<QLineEdit *>(QStringLiteral("rechargeAmountInput"));
    auto *rechargeButton =
        window.findChild<QPushButton *>(QStringLiteral("rechargeButton"));
    auto *profileMessage =
        window.findChild<QLabel *>(QStringLiteral("profileMessageLabel"));
    QTRY_VERIFY(rechargeButton->isEnabled());
    rechargeInput->setText(QStringLiteral("1.00"));
    QTest::mouseClick(rechargeButton, Qt::LeftButton);
    QTRY_VERIFY(profileMessage->text().contains(QStringLiteral("充值成功")));

    navigation->setCurrentIndex(1);
    orderRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("orderRefreshButton"));
    QTRY_VERIFY(orderRefreshButton->isEnabled());
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
    orderCard = window.findChild<QWidget *>(QStringLiteral("orderCard_1001"));
    QTest::mouseClick(orderCard, Qt::LeftButton);
    payButton = window.findChild<QPushButton *>(QStringLiteral("orderDetailPayButton"));
    QTRY_VERIFY(payButton->isVisible());
    QTest::mouseClick(payButton, Qt::LeftButton);

    QTRY_VERIFY(orderMessage->text().contains(QStringLiteral("订单结算成功")));
    auto *status = window.findChild<QLabel *>(QStringLiteral("orderStatus_1001"));
    QCOMPARE(status->text(), QStringLiteral("已完成"));
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
    auto *welcomeLabel = window.findChild<QLabel *>(QStringLiteral("welcomeLabel"));
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
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，新的昵称"));

    navigation->setCurrentIndex(0);
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，新的昵称"));
    navigation->setCurrentWidget(profilePage);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));

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
