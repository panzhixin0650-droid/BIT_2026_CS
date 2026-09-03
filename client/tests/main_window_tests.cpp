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
