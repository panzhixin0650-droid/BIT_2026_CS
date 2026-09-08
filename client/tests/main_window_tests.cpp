#include "api/mock_charging_api.h"
#include "navigation_paint_helpers.h"
#include "ui/main_window.h"
#include "ui/charging_controller.h"
#include "ui/charging_page.h"
#include "ui/scan_page.h"
#include "ui/station_browser_page.h"
#include "ui/support_page.h"
#include "ui/support_desk_page.h"
#include "ui/photo_album_page.h"
#include "ui/profile_page.h"
#include "ui/avatar_art.h"
#include "local/avatar_storage.h"
#include <QFile>
#include <QListWidget>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QPlainTextEdit>
#include <QSignalSpy>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QtTest>

#include <functional>

using namespace charging::client;

class MainWindowTests : public QObject {
    Q_OBJECT

private slots:
    void chargingLayoutFitsSmallWindow();
    void peakQuoteAndLockedPriceFitSmallWindow();
    void constructsCodeOnlyLoginPage();
    void existingUserCanLogin();
    void newUserIsAutomaticallyRegistered();
    void invalidPhoneStaysOnLoginPage();
    void verificationCodeRowFitsSmallWindow();
    void authenticatedShellHasFiveBottomEntries();
    void floatingNavigationResizesAndKeepsEntriesClickable();
    void floatingNavigationSurvivesPartialRepaints_data();
    void floatingNavigationSurvivesPartialRepaints();
    void clientUsesConsistentVisualTheme();
    void stationLocationCanChangeAndRestoreDefault();
    void chargingHomeMapFiltersAndOpensStationDetail();
    void stationDetailCanPrepareDirectCharging();
    void chargingStartLeavesNavigationForHomeOverview();
    void locationCanResolveAndOpenMockRoute();
    void reservationAppearsOnHomeAndCanBeCancelled();
    void reservationExpiryRefreshesHomeWithoutNavigation();
    void ordersPageShowsHistoryDetailAndReservationChanges();
    void leavingOrderDetailRefreshesChangedOrderState();
    void simulatedScanStartsChargingAndRefreshesHome();
    void scannerAdapterCanSubmitDecodedPileCode();
    void reservationStartsSameOrderOnChargingPage();
    void chargingProgressCanRefreshAndStopWithConfirmation();
    void stoppingFromOrderDetailRefreshesOpenStationDetail();
    void pendingOrderLinksRechargeAndCanBeSettled();
    void profileCanRefreshUpdateNicknameAndRecharge();
    void profileRejectsInvalidRechargeAmount();
    void logoutReturnsToLoginPage();
    void scanRepairSubmitsToSharedTickets();
    void supportDeskUsesInAppPageAndPreservesDraft();
    void profileAvatarUsesSharedAlbum();
};

namespace {

void loginFixtureUser(MainWindow &window)
{
    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phoneInput->setText(QStringLiteral("13800000001"));
    window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"))->setText(QStringLiteral("123456"));
    QTest::mouseClick(loginButton, Qt::LeftButton);
    auto *homePage = window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"));
    QTRY_VERIFY(homePage->isVisible());
}

void openPreviewDetails(MainWindow &window)
{
    auto *details = window.findChild<QPushButton *>(QStringLiteral("stationPreviewDetailsButton"));
    QTRY_VERIFY(details && details->isVisible() && details->isEnabled());
    QTest::mouseClick(details, Qt::LeftButton);
}

void expandCurrentOrder(MainWindow &window)
{
    auto *toggle = window.findChild<QPushButton *>(QStringLiteral("currentOrderToggle"));
    QTRY_VERIFY(toggle && toggle->isVisible());
    if (!toggle->isChecked()) QTest::mouseClick(toggle, Qt::LeftButton);
}

void handleDialogWhenShown(MainWindow &window,
                           const QString &objectName,
                           std::function<void(QMessageBox *)> handler)
{
    auto *poller = new QTimer(&window);
    poller->setInterval(5);
    QObject::connect(poller, &QTimer::timeout, &window,
                     [&window, objectName, handler = std::move(handler), poller]() {
        auto *dialog = window.findChild<QMessageBox *>(objectName);
        if (dialog == nullptr) {
            return;
        }
        poller->stop();
        poller->deleteLater();
        handler(dialog);
    });
    poller->start();
}

}  // namespace

void MainWindowTests::supportDeskUsesInAppPageAndPreservesDraft()
{
    MockChargingApi api; MainWindow window(api);
    window.resize(360,640); window.show(); loginFixtureUser(window);
    auto *tabs = window.findChild<QTabWidget *>("mainNavigation");
    auto *pages = window.findChild<QStackedWidget *>("applicationPages");
    QSignalSpy created(&api, &IChargingApi::supportTicketCreated);
    tabs->setCurrentIndex(3);
    window.findChild<QPushButton *>("supportDeskEntry")->click();
    auto *desk = window.findChild<SupportDeskPage *>("supportDeskPage");
    QVERIFY(desk && !desk->isWindow());
    QCOMPARE(pages->currentWidget(), desk);
    QCOMPARE(desk->findChild<QTabWidget *>("deskTabs")->currentIndex(), 0);
    QVERIFY(!desk->findChild<QPushButton *>("ticketGenerate")->isVisible());
    QVERIFY(!desk->findChild<QListWidget *>("myTickets")->isVisible());
    desk->findChild<QPlainTextEdit *>("deskInput")->setPlainText(QStringLiteral("尚未发送的问题"));
    desk->findChild<QPushButton *>("deskBackButton")->click();
    tabs->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileRepairButton")->click();
    auto *repair = window.findChild<SupportDeskPage *>("repairPage");
    QVERIFY(repair && repair != desk);
    QCOMPARE(pages->currentWidget(), repair);
    repair->findChild<QLineEdit *>("repairPileCode")->setText("PILE-A-01");
    repair->findChild<QPlainTextEdit *>("ticketSummary")->setPlainText(QStringLiteral("返回后保留报修内容"));
    auto *submit = repair->findChild<QPushButton *>("ticketSubmit");
    repair->findChild<QScrollArea *>("ticketDraftScroll")->ensureWidgetVisible(submit);
    QTRY_VERIFY(submit->visibleRegion().contains(submit->rect().center()));
    repair->findChild<QPushButton *>("deskBackButton")->click();
    window.findChild<QPushButton *>("profileTicketsButton")->click();
    auto *tracking = window.findChild<SupportDeskPage *>("ticketsPage");
    QVERIFY(tracking && tracking != repair && tracking != desk);
    QCOMPARE(pages->currentWidget(), tracking);
    QVERIFY(tracking->findChild<QListWidget *>("myTickets")->isVisible());
    QVERIFY(!tracking->findChild<QPlainTextEdit *>("deskInput")->isVisible());
    tracking->findChild<QPushButton *>("deskBackButton")->click();
    tabs->setCurrentIndex(3);
    window.findChild<QPushButton *>("supportDeskEntry")->click();
    QCOMPARE(pages->currentWidget(), desk);
    QCOMPARE(desk->findChild<QTabWidget *>("deskTabs")->currentIndex(), 0);
    QCOMPARE(desk->findChild<QPlainTextEdit *>("deskInput")->toPlainText(), QStringLiteral("尚未发送的问题"));
    desk->findChild<QPushButton *>("deskBackButton")->click();
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    window.findChild<QPushButton *>("chargingRepairButton")->click();
    QCOMPARE(pages->currentWidget(), repair);
    QCOMPARE(repair->findChild<QPlainTextEdit *>("ticketSummary")->toPlainText(), QStringLiteral("返回后保留报修内容"));
    repair->findChild<QPushButton *>("deskBackButton")->click();
    QCOMPARE(pages->currentWidget(), tabs); QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(created.count(), 0);
}

void MainWindowTests::profileAvatarUsesSharedAlbum()
{
    const auto originalName = QCoreApplication::applicationName();
    const auto originalOrganization = QCoreApplication::organizationName();
    QCoreApplication::setOrganizationName("BITAlbumTests");
    QCoreApplication::setApplicationName("avatar-album-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto cleanup = qScopeGuard([&] {
        QSettings settings;
        settings.clear(); settings.sync();
        QFile::remove(settings.fileName());
        if (!dataDirectory.isEmpty()) QDir(dataDirectory).removeRecursively();
        QCoreApplication::setApplicationName(originalName);
        QCoreApplication::setOrganizationName(originalOrganization);
    });
    MockChargingApi api;
    MainWindow window(api); window.resize(360,640); window.show();
    loginFixtureUser(window);
    auto *tabs = window.findChild<QTabWidget *>("mainNavigation");
    auto *pages = window.findChild<QStackedWidget *>("applicationPages");
    tabs->setCurrentIndex(4);
    auto *profile = window.findChild<ProfilePage *>();
    auto *avatar = profile->findChild<QLabel *>("profileAvatar");
    auto *change = profile->findChild<QPushButton *>("changeAvatarButton");
    QSignalSpy selected(profile, &ProfilePage::avatarSelected);
    profile->findChild<QPushButton *>("profileDetailsButton")->click();
    QVERIFY(profile->findChild<QWidget *>("profileDetailPage")->isVisible());
    profile->findChild<QPushButton *>("profileAvatarButton")->click();
    QVERIFY(profile->findChild<QWidget *>("profileAvatarPage")->isVisible());
    change->click();
    auto *album = window.findChild<PhotoAlbumPage *>();
    QVERIFY(album && !album->isWindow());
    QCOMPARE(pages->currentWidget(), album);
    auto *photos = album->findChild<QListWidget *>("albumPhotos");
    auto *confirm = album->findChild<QPushButton *>("albumConfirm");
    auto *back = album->findChild<QPushButton *>("albumBack");
    QCOMPARE(photos->count(), 9);
    QVERIFY(!confirm->isEnabled());
    const auto coffee = photos->findItems("sample-coffee", Qt::MatchExactly);
    QCOMPARE(coffee.size(), 1);
    photos->setCurrentItem(coffee.first());
    const auto sourcePath = coffee.first()->data(Qt::UserRole).toString();
    QCOMPARE(confirm->text(), QStringLiteral("设为头像"));
    album->findChild<QPushButton *>("albumPreview")->click();
    QVERIFY(!album->findChild<QLabel *>("albumPreviewImage")->pixmap(Qt::ReturnByValue).isNull());
    QCOMPARE(selected.count(), 0);
    back->click(); // Return from preview without applying it.
    QCOMPARE(pages->currentWidget(), album);
    confirm->click();
    QCOMPARE(pages->currentWidget(), tabs);
    QCOMPARE(tabs->currentIndex(), 4);
    QCOMPARE(selected.count(), 1);
    QVERIFY(profile->findChild<QWidget *>("profileAvatarPage")->isVisible());
    QVERIFY(!profile->findChild<QLabel *>("profileFullAvatar")->pixmap(Qt::ReturnByValue).isNull());
    QCOMPARE(selected.first().first().toString(), sourcePath);
    QVERIFY2(!avatar->pixmap(Qt::ReturnByValue).isNull(),
             qPrintable(profile->findChild<QLabel *>("profileMessageLabel")->text()));
    AvatarStorage storage;
    const auto savedPath = storage.avatarPath("1:13800000001");
    const QImage saved(savedPath);
    QVERIFY(!saved.isNull());
    QVERIFY(saved.width() <= 512 && saved.height() <= 512);
    change->click();
    QVERIFY(!confirm->isEnabled());
    photos->setCurrentRow(0);
    back->click();
    QCOMPARE(pages->currentWidget(), tabs);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(QImage(savedPath), saved);

    // Built-in images from main must follow the same embedded album return path
    // and refresh both the identity thumbnail and the complete avatar preview.
    QSignalSpy imageSelected(profile, &ProfilePage::avatarImageSelected);
    for (const auto &basic : basicAvatars()) {
        change->click();
        QCOMPARE(pages->currentWidget(), album);
        QPushButton *basicButton = nullptr;
        for (auto *button : album->findChildren<QPushButton *>()) {
            if (button->text() == basic.name) basicButton = button;
        }
        QVERIFY(basicButton);
        basicButton->click();
        QCOMPARE(pages->currentWidget(), tabs);
        QVERIFY(profile->findChild<QWidget *>("profileAvatarPage")->isVisible());
        const QImage updated(savedPath);
        QCOMPARE(updated.convertToFormat(QImage::Format_ARGB32),
                 basic.image.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(avatar->pixmap().toImage(),
                 circularAvatar(updated, avatar->width(), profile->devicePixelRatioF()).toImage());
        auto *full = profile->findChild<QLabel *>("profileFullAvatar");
        QCOMPARE(full->pixmap().toImage(), QPixmap::fromImage(updated).scaled(
                     full->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage());
    }
    QCOMPARE(imageSelected.count(), 4);
    QCOMPARE(selected.count(), 1); // Image selection does not emit a bogus path.
}

void MainWindowTests::scanRepairSubmitsToSharedTickets()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);
    auto *navigation = window.findChild<QTabWidget *>("mainNavigation");
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    QSignalSpy created(&api, &IChargingApi::supportTicketCreated);
    QSignalSpy started(&api, &IChargingApi::chargingStartCompleted);
    window.findChild<QPushButton *>("chargingRepairButton")->click();
    auto *tabs = window.findChild<QTabWidget *>("deskTabs");
    QVERIFY(tabs); QCOMPARE(tabs->currentIndex(), 1);
    QCOMPARE(window.findChild<QLineEdit *>("repairPileCode")->text(), QStringLiteral("PILE-A-01"));
    window.findChild<QPlainTextEdit *>("ticketSummary")->setPlainText(QStringLiteral("充电枪损坏"));
    window.findChild<QPushButton *>("ticketSubmit")->click();
    QTRY_COMPARE(created.size(), 1);
    const auto result = qvariant_cast<TicketResult>(created.takeFirst().first());
    QVERIFY(result.ok() && result.payload);
    QCOMPARE(result.payload->ticket.pileCode, QStringLiteral("PILE-A-01"));
    QCOMPARE(started.size(), 0);
    auto invalid = static_cast<const charging::protocol::SupportTicketDraft &>(result.payload->ticket);
    invalid.submissionId = "3aafde11-535e-4018-a752-ab7e686fa334";
    invalid.pileCode = "MISSING";
    QVERIFY(!api.createSupportTicket(invalid).isEmpty());
    QTRY_COMPARE(created.size(), 1);
    QCOMPARE(qvariant_cast<TicketResult>(created.takeFirst().first()).response.code,
             charging::protocol::ErrorCode::NotFound);
}

void MainWindowTests::chargingLayoutFitsSmallWindow()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    window.findChild<ChargingController *>()->reset();
    auto *tabs=window.findChild<QTabWidget *>("mainNavigation");
    tabs->setCurrentIndex(1);
    charging::protocol::OrderDto order;
    order.pileCode="PILE-A-01"; order.stationName=QStringLiteral("浑南演示充电站");
    order.status=charging::protocol::OrderStatus::Charging;
    order.durationSeconds=122; order.energyWh=244; order.amountCents=44;
    window.findChild<ChargingPage *>()->showOrder(order);
    for (const QSize size : {QSize(480,860),QSize(360,640)}) {
        window.resize(size); QTest::qWait(80);
        auto *ring=window.findChild<QWidget *>("chargingProgressRing");
        QVERIFY(ring->width() >= 220);
        auto path=qEnvironmentVariable("CHARGING_FLOW_SCREENSHOTS");
        if(!path.isEmpty()) window.grab().save(QDir(path).filePath(QStringLiteral("charging-%1x%2.png").arg(size.width()).arg(size.height())));
        auto *stop=window.findChild<QPushButton *>("chargingEndButton");
        window.findChild<ChargingPage *>()->findChild<QScrollArea *>()->ensureWidgetVisible(stop);
        QTRY_VERIFY(stop->visibleRegion().contains(stop->rect().center()));
    }
}

void MainWindowTests::peakQuoteAndLockedPriceFitSmallWindow()
{
    auto now = QDateTime::fromString(QStringLiteral("2026-09-08T02:59:00Z"), Qt::ISODate);
    MockChargingApi api(nullptr, [&now] { return now; });
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);
    QTRY_VERIFY(window.findChild<QAbstractButton *>("stationMarker_1") != nullptr);
    auto *marker = window.findChild<QAbstractButton *>("stationMarker_1");
    marker->click();
    QTRY_VERIFY(window.findChild<QLabel *>("stationPreviewMetrics")->text().contains("1.62"));
    openPreviewDetails(window);
    auto *detailPrice = window.findChild<QLabel *>("stationDetailPrice");
    QTRY_COMPARE(detailPrice->text(), QStringLiteral("当前参考单价：¥1.62/度"));
    auto *detailHelp = window.findChild<QToolButton *>("stationPricingInfoButton");
    QVERIFY(detailHelp && detailHelp->isVisible());
    QTest::mouseClick(detailHelp, Qt::LeftButton);
    auto *detailDialog = window.findChild<QDialog *>("pricingRulesDialog");
    QTRY_VERIFY(detailDialog && detailDialog->isVisible());
    QVERIFY(detailDialog->findChild<QLabel *>("pricingRulesText")->text().contains(QStringLiteral("高峰 +20%")));
    detailDialog->findChild<QPushButton *>("pricingRulesCloseButton")->click();
    QTRY_VERIFY(!window.findChild<QDialog *>("pricingRulesDialog"));
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    auto *page = window.findChild<ChargingPage *>();
    auto *start = page->findChild<QPushButton *>("chargingStartButton");
    auto *price = page->findChild<QLabel *>("chargingPrice");
    auto *help = page->findChild<QToolButton *>("chargingPricingInfoButton");
    auto *scroll = page->findChild<QScrollArea *>();
    QTRY_VERIFY(start->isEnabled());
    QCOMPARE(price->text(), QStringLiteral("当前参考单价：¥1.62/度"));
    QVERIFY(help && help->isVisible());
    QVERIFY(!page->findChild<QLabel *>("chargingPricingRule"));
    QVERIFY(!page->findChild<QPushButton *>("chargingRefreshPriceButton"));
    for (const QSize size : {QSize(480, 860), QSize(360, 640)}) {
        window.resize(size);
        QTest::qWait(80);
        scroll->ensureWidgetVisible(start);
        QTRY_VERIFY(start->visibleRegion().contains(start->rect().center()));
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(help->visibleRegion().contains(help->rect().center()));
        QVERIFY(help->geometry().left() >= price->geometry().right());
        const auto directory = qEnvironmentVariable("CHARGING_FLOW_SCREENSHOTS");
        if (!directory.isEmpty()) {
            QVERIFY(window.grab().save(QDir(directory).filePath(
                QStringLiteral("peak-price-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        help->setFocus();
        QTest::keyClick(help, Qt::Key_Space);
        auto *dialog = page->findChild<QDialog *>("pricingRulesDialog");
        QTRY_VERIFY(dialog && dialog->isVisible());
        QVERIFY(dialog->width() < window.width());
        QVERIFY(dialog->height() < window.height());
        auto *rules = dialog->findChild<QLabel *>("pricingRulesText");
        QVERIFY(rules->wordWrap());
        QVERIFY(rules->height() >= rules->heightForWidth(rules->width()));
        QVERIFY(rules->text().contains(QStringLiteral("高峰 +20%")));
        QVERIFY(rules->text().contains(QStringLiteral("08:00–11:00、18:00–21:00")));
        QVERIFY(rules->text().contains(QStringLiteral("开始充电时单价锁定全单")));
        if (!directory.isEmpty()) {
            QVERIFY(dialog->grab().save(QDir(directory).filePath(
                QStringLiteral("pricing-rules-%1x%2.png").arg(size.width()).arg(size.height()))));
        }
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!page->findChild<QDialog *>("pricingRulesDialog"));
    }
    QSignalSpy started(&api, &IChargingApi::chargingStartCompleted);
    start->click();
    QTRY_COMPARE(started.count(), 1);
    QCOMPARE(price->text(), QStringLiteral("本单锁定单价：¥1.62/度"));
    now = now.addSecs(120);
    window.findChild<ChargingController *>()->refresh();
    QTRY_VERIFY(page->findChild<QLabel *>("chargingDuration")->text() != "00:00");
    QCOMPARE(price->text(), QStringLiteral("本单锁定单价：¥1.62/度"));
}

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
    auto *codeInput = window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"));
    auto *sendCode = window.findChild<QPushButton *>(QStringLiteral("sendVerificationCodeButton"));
    QVERIFY(codeInput != nullptr);
    QVERIFY(sendCode != nullptr);
    QVERIFY(codeInput->text().isEmpty());
    QCOMPARE(codeInput->maxLength(), 6);
    QCOMPARE(sendCode->text(), QStringLiteral("发送验证码"));
    QCOMPARE(pages->currentWidget(), loginPage);
    QCOMPARE(loginButton->text(), QStringLiteral("登录"));
}

void MainWindowTests::existingUserCanLogin()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();

    auto *phoneInput = window.findChild<QLineEdit *>(QStringLiteral("phoneInput"));
    auto *homePage = window.findChild<QWidget *>(QStringLiteral("authenticatedHomePage"));
    auto *welcomeLabel = window.findChild<QLabel *>(QStringLiteral("welcomeLabel"));
    auto *noticeLabel = window.findChild<QLabel *>(QStringLiteral("loginNoticeLabel"));

    phoneInput->setText(QStringLiteral("13800000001"));
    auto *codeInput = window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"));
    codeInput->setFocus();
    QTest::keyClicks(codeInput, "a1234567");
    QCOMPARE(codeInput->text(), QStringLiteral("123456"));
    QTest::keyClick(codeInput, Qt::Key_Return);

    QTRY_VERIFY(homePage->isVisible());
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，演示用户0001"));
    QCOMPARE(noticeLabel->text(), QStringLiteral("登录成功"));
    QVERIFY(codeInput->text().isEmpty());
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
    auto *codeInput = window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"));
    codeInput->setText(QStringLiteral("123456"));
    QSignalSpy loginSpy(&api, &IChargingApi::loginCompleted);
    QTest::mouseClick(loginButton, Qt::LeftButton);

    QTRY_VERIFY(homePage->isVisible());
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，用户5678"));
    QCOMPARE(noticeLabel->text(), QStringLiteral("账号已自动注册并登录"));
    QCOMPARE(loginSpy.count(), 1);
    const auto firstLogin = qvariant_cast<LoginResult>(loginSpy.first().at(0));
    QVERIFY(firstLogin.payload.has_value());
    QVERIFY(firstLogin.payload->isNewUser);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    navigation->setCurrentIndex(4);
    auto *logoutButton = window.findChild<QPushButton *>(QStringLiteral("logoutButton"));
    QTRY_VERIFY(logoutButton->isEnabled());
    QTest::mouseClick(logoutButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("loginPage"))->isVisible());
    QVERIFY(codeInput->text().isEmpty());
    QTest::mouseClick(loginButton, Qt::LeftButton);
    QCOMPARE(loginSpy.count(), 1);
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("loginErrorLabel"))->text(),
             QStringLiteral("请输入6位数字验证码"));

    codeInput->setText(QStringLiteral("123456"));
    QTest::mouseClick(loginButton, Qt::LeftButton);
    QTRY_VERIFY(homePage->isVisible());
    QCOMPARE(loginSpy.count(), 2);
    const auto secondLogin = qvariant_cast<LoginResult>(loginSpy.last().at(0));
    QVERIFY(secondLogin.payload.has_value());
    QVERIFY(!secondLogin.payload->isNewUser);
    QCOMPARE(secondLogin.payload->user.userId, firstLogin.payload->user.userId);
    QCOMPARE(noticeLabel->text(), QStringLiteral("登录成功"));
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

void MainWindowTests::verificationCodeRowFitsSmallWindow()
{
    MockChargingApi api;
    MainWindow window(api);
    window.resize(360, 640);
    window.show();
    auto *scroll = window.findChild<QScrollArea *>(QStringLiteral("loginScrollArea"));
    auto *codeInput = window.findChild<QLineEdit *>(QStringLiteral("verificationCodeInput"));
    auto *sendCode = window.findChild<QPushButton *>(QStringLiteral("sendVerificationCodeButton"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    QVERIFY(scroll && codeInput && sendCode && loginButton);
    QTRY_VERIFY(scroll->widget()->width() <= scroll->viewport()->width());
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    scroll->ensureWidgetVisible(codeInput);
    QTRY_VERIFY(codeInput->visibleRegion().contains(codeInput->rect().center()));
    const QRect inputRect(codeInput->mapTo(scroll->viewport(), QPoint()), codeInput->size());
    const QRect buttonRect(sendCode->mapTo(scroll->viewport(), QPoint()), sendCode->size());
    QVERIFY(inputRect.right() < buttonRect.left());
    QVERIFY(scroll->viewport()->rect().contains(inputRect));
    QVERIFY(scroll->viewport()->rect().contains(buttonRect));
    scroll->ensureWidgetVisible(loginButton);
    QTRY_VERIFY(loginButton->visibleRegion().contains(loginButton->rect().center()));
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
    QCOMPARE(navigation->tabText(0), QStringLiteral("首页"));
    QCOMPARE(navigation->tabText(1), QStringLiteral("充电"));
    QCOMPARE(navigation->tabText(2), QStringLiteral("扫一扫"));
    QCOMPARE(navigation->tabText(3), QStringLiteral("客服助理"));
    QCOMPARE(navigation->tabText(4), QStringLiteral("我的"));
    for (int index = 0; index < navigation->count(); ++index) {
        QVERIFY(!navigation->tabIcon(index).isNull());
    }
    auto *support = qobject_cast<SupportPage *>(navigation->widget(3));
    QVERIFY(support != nullptr);
    navigation->setCurrentWidget(support);
    QVERIFY(support->isVisible());
    QVERIFY(support->findChild<QPushButton *>(QStringLiteral("assistantSend")) != nullptr);
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

void MainWindowTests::floatingNavigationResizesAndKeepsEntriesClickable()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    auto *container = window.findChild<QFrame *>(QStringLiteral("navigationContainer"));
    QVERIFY(navigation && container);
    auto *bar = navigation->tabBar();
    QVERIFY(container->testAttribute(Qt::WA_TransparentForMouseEvents));
    QVERIFY(container->graphicsEffect() == nullptr);

    // Resize the same shell back to narrow width to catch stale frame geometry.
    for (const QSize size : {QSize(360, 640), QSize(480, 860),
                             QSize(900, 760), QSize(360, 640)}) {
        window.resize(size);
        QTRY_COMPARE(window.size(), size);
        QTRY_VERIFY(container->isVisible());
        QTRY_VERIFY(navigation->rect().contains(container->geometry()));
        QTRY_VERIFY(navigation->rect().contains(bar->geometry()));
        QTRY_VERIFY(qAbs(container->geometry().center().x()
                         - navigation->rect().center().x()) <= 1);
        QVERIFY(container->x() > 0);
        QVERIFY(container->geometry().right() < navigation->width() - 1);
        QCOMPARE(container->y(), bar->y());
        QCOMPARE(bar->x() - container->x(),
                 container->geometry().right() - bar->geometry().right());
        QVERIFY(container->geometry().bottom() < navigation->height() - 1);

        int occupiedWidth = 0;
        const int firstWidth = bar->tabRect(0).width();
        for (int index = 0; index < bar->count(); ++index) {
            const QRect hitArea = bar->tabRect(index);
            occupiedWidth += hitArea.width();
            QVERIFY(qAbs(hitArea.width() - firstWidth) <= 1);
            QVERIFY(bar->rect().contains(hitArea));
            // The full equal-width area remains clickable, not just the painted tile.
            const QPoint clickPoint(hitArea.left() + 1, hitArea.center().y());
            QCOMPARE(navigation->childAt(bar->mapTo(navigation, clickPoint)), bar);
            QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, clickPoint);
            QTRY_COMPARE(navigation->currentIndex(), index);
            if (index == 2) {
                if (auto *scanner = window.findChild<QDialog *>("qrScanDialog")) {
                    QVERIFY(!scanner->isWindow());
                    QCOMPARE(scanner->parentWidget(), navigation->currentWidget());
                    QTRY_COMPARE(scanner->size(), navigation->currentWidget()->size());
                }
            }
            QTRY_COMPARE(container->height(), 92);
            QTRY_COMPARE(bar->height(), 120);
            QTRY_COMPARE(navigation->height() - container->geometry().bottom() - 1, 28);
            QCOMPARE(window.findChild<QWidget *>("applicationHeader")->height(), 64);
            auto *page = navigation->currentWidget();
            QVERIFY(page->isVisible());
            const QRect pageRect(page->mapTo(navigation, QPoint()), page->size());
            if (index == 0) {
                QTRY_COMPARE(page->mapTo(navigation, QPoint()).y() + page->height() - 1, navigation->rect().bottom());
                QVERIFY(QRect(page->mapTo(navigation, QPoint()), page->size()).intersects(container->geometry()));
            } else {
                QVERIFY(pageRect.bottom() < container->y());
            }
        }
        QVERIFY(occupiedWidth >= bar->width() - 2);
        bar->setFocus();
        QTest::keyClick(bar, Qt::Key_Left);
        QCOMPARE(navigation->currentIndex(), 3);
        QTest::keyClick(bar, Qt::Key_Right);
        QCOMPARE(navigation->currentIndex(), 4);

        const auto image = navigation_test::presentedNavigation(
            *bar, QStringLiteral("floating-nav"));
        if (!image.isNull()) {
            const auto missing = navigation_test::missingNavigationContent(*bar, image);
            QVERIFY2(missing.isEmpty(), qPrintable(missing));
        }
    }
    auto *logout = window.findChild<QPushButton *>(QStringLiteral("logoutButton"));
    QTRY_VERIFY(logout->isEnabled());
    QTest::mouseClick(logout, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("loginPage"))->isVisible());
    QVERIFY(!container->isVisible());
}

void MainWindowTests::floatingNavigationSurvivesPartialRepaints_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("narrow") << QSize(360, 640);
    QTest::newRow("default-width") << QSize(480, 760);
    QTest::newRow("wide") << QSize(900, 760);
}

void MainWindowTests::floatingNavigationSurvivesPartialRepaints()
{
    QFETCH(QSize, size);
    MockChargingApi api;
    MainWindow window(api);
    window.resize(size);
    window.show();
    loginFixtureUser(window);
    auto *navigation = window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    QVERIFY(navigation);
    auto *bar = navigation->tabBar();
    auto image = navigation_test::presentedNavigation(*bar, QStringLiteral("initial"));
    if (image.isNull()) {
        QSKIP("No window capture support; run with QT_QPA_PLATFORM=xcb under X11/Xvfb");
    }
    auto missing = navigation_test::missingNavigationContent(*bar, image);
    QVERIFY2(missing.isEmpty(), qPrintable(missing));

    // Repainting unchanged tabs must not accumulate translucent shadow layers.
    for (int repetition = 0; repetition < 3; ++repetition) {
        bar->update(QRegion(bar->tabRect(0)) | QRegion(bar->tabRect(4)));
        const auto repainted = navigation_test::presentedNavigation(
            *bar, QStringLiteral("unchanged-%1").arg(repetition));
        QCOMPARE(repainted, image);
    }

    // Jump over intermediate tabs. Adjacent-only hover/click tests miss the bug.
    for (int index : {0, 4, 1, 3, 0, 2, 4}) {
        QTest::mouseMove(bar, bar->tabRect(index).center());
        image = navigation_test::presentedNavigation(
            *bar, QStringLiteral("hover-%1").arg(index));
        missing = navigation_test::missingNavigationContent(*bar, image);
        QVERIFY2(missing.isEmpty(), qPrintable(missing));

        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                          bar->tabRect(index).center());
        QTRY_COMPARE(navigation->currentIndex(), index);
        // Explicitly coalesce two distant updates, regardless of platform mouse
        // event timing. A live sibling effect repaints the clean middle as well.
        QTest::qWait(60);
        // Scanning intentionally covers the shell; return before sampling the navigation pixels.
        if (auto *scanner = window.findChild<QDialog *>(QStringLiteral("qrScanDialog"))) {
            if (scanner->isVisible()) scanner->reject();
        }
        QTRY_VERIFY(navigation->isVisible());
        bar->update(QRegion(bar->tabRect(0)) | QRegion(bar->tabRect(4)));
        image = navigation_test::presentedNavigation(
            *bar, QStringLiteral("disjoint-%1").arg(index));
        missing = navigation_test::missingNavigationContent(*bar, image);
        QVERIFY2(missing.isEmpty(), qPrintable(missing));
    }

    window.hide();
    window.show();
    image = navigation_test::presentedNavigation(*bar, QStringLiteral("reshown"));
    missing = navigation_test::missingNavigationContent(*bar, image);
    QVERIFY2(missing.isEmpty(), qPrintable(missing));

    window.showMinimized();
    QTest::qWait(60);
    window.showNormal();
    image = navigation_test::presentedNavigation(*bar, QStringLiteral("restored"));
    missing = navigation_test::missingNavigationContent(*bar, image);
    QVERIFY2(missing.isEmpty(), qPrintable(missing));

    auto *logout = window.findChild<QPushButton *>(QStringLiteral("logoutButton"));
    QTRY_VERIFY(logout->isEnabled());
    QTest::mouseClick(logout, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("loginPage"))->isVisible());
    loginFixtureUser(window);
    image = navigation_test::presentedNavigation(*bar, QStringLiteral("relogin"));
    missing = navigation_test::missingNavigationContent(*bar, image);
    QVERIFY2(missing.isEmpty(), qPrintable(missing));
}

void MainWindowTests::clientUsesConsistentVisualTheme()
{
    MockChargingApi api;
    MainWindow window(api);

    const QString theme = window.styleSheet();
    QVERIFY(theme.contains(QStringLiteral("QTabWidget#mainNavigation")));
    QVERIFY(theme.contains(QStringLiteral("QPushButton#loginButton")));
    QVERIFY(theme.contains(QStringLiteral("QLineEdit:focus")));

    auto *brandBadge =
        window.findChild<QLabel *>(QStringLiteral("loginBrandBadge"));
    QVERIFY(brandBadge != nullptr);
    QCOMPARE(brandBadge->text(), QStringLiteral("EV CHARGE · DEMO"));

    auto *supportCard =
        window.findChild<QWidget *>(QStringLiteral("supportCard"));
    auto *supportTitle =
        window.findChild<QLabel *>(QStringLiteral("supportTitle"));
    auto *orderHeading =
        window.findChild<QLabel *>(QStringLiteral("orderListHeading"));
    auto *scanHeading =
        window.findChild<QLabel *>(QStringLiteral("scanHeading"));
    auto *profileHeading =
        window.findChild<QLabel *>(QStringLiteral("profileHeading"));
    auto *balance =
        window.findChild<QLabel *>(QStringLiteral("profileBalanceLabel"));
    QVERIFY(supportCard != nullptr);
    QVERIFY(supportTitle != nullptr);
    QVERIFY(orderHeading != nullptr);
    QVERIFY(scanHeading != nullptr);
    QVERIFY(profileHeading != nullptr);
    QVERIFY(balance != nullptr);
    QCOMPARE(supportTitle->text(), QStringLiteral("你好，有什么\n可以帮你？"));
    QCOMPARE(orderHeading->font().pointSize(), 24);
    QVERIFY(scanHeading->font().bold());
    QCOMPARE(profileHeading->font().pointSize(), 24);
    QCOMPARE(balance->font().pointSize(), 30);
}

void MainWindowTests::stationLocationCanChangeAndRestoreDefault()
{
    MockChargingApi api; MainWindow window(api);
    window.resize(360,640); window.show(); loginFixtureUser(window);
    auto *page = window.findChild<StationBrowserPage *>();
    auto *entry = window.findChild<QPushButton *>("stationHomeLocationButton");
    QVERIFY(!entry->icon().isNull());
    QVERIFY(!window.findChild<QLineEdit *>("stationRegionInput"));
    const auto initial = page->currentLocation();
    QVERIFY(page->stationQuery().longitude.has_value());
    entry->click();
    auto *address = window.findChild<QLineEdit *>("locationAddressInput");
    address->setText(QStringLiteral("沈阳市和平区"));
    window.findChild<QPushButton *>("stationLocationBack")->click();
    QCOMPARE(page->currentLocation().longitude, initial.longitude);
    entry->click();
    QCOMPARE(address->text(), initial.address);
    address->setText(QStringLiteral("沈阳市和平区"));
    window.findChild<QPushButton *>("resolveLocationButton")->click();
    QTRY_COMPARE(page->currentLocation().address, QStringLiteral("沈阳市和平区"));
    QCOMPARE(*page->stationQuery().longitude, 123.40);
    window.findChild<QPushButton *>("stationLocationDefault")->click();
    QTRY_COMPARE(page->currentLocation().longitude, initial.longitude);
    QCOMPARE(page->currentLocation().latitude, initial.latitude);
    QVERIFY(page->stationQuery().longitude.has_value());
}

void MainWindowTests::chargingHomeMapFiltersAndOpensStationDetail()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_1")) != nullptr);
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_2")) != nullptr);
    auto *locationSummary = window.findChild<QLabel *>(QStringLiteral("stationLocationSummary"));
    QVERIFY(locationSummary->text().contains(QStringLiteral("演示位置")));
    QVERIFY(!window.findChild<QScrollArea *>(QStringLiteral("stationHomeScrollArea")));
    QVERIFY(!window.findChild<QWidget *>(QStringLiteral("stationListContent")));
    auto *marker = window.findChild<QAbstractButton *>(QStringLiteral("stationMarker_1"));
    QVERIFY(marker);
    QTest::mouseClick(marker, Qt::LeftButton);
    auto *preview = window.findChild<QWidget *>(QStringLiteral("stationPreviewCard"));
    QTRY_VERIFY(preview->isVisible());
    QVERIFY(marker->isChecked());
    QVERIFY(!window.findChild<QWidget *>(QStringLiteral("stationDetailPage"))->isVisible());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("stationPreviewPrediction"))->text().contains(QStringLiteral("推荐")));
    openPreviewDetails(window);

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

    auto *detailNavigate = window.findChild<QPushButton *>(
        QStringLiteral("stationDetailNavigationButton"));
    QTest::mouseClick(detailNavigate, Qt::LeftButton);
    auto *navigationPage =
        window.findChild<QWidget *>(QStringLiteral("stationNavigationPage"));
    QVERIFY(navigationPage->isVisible());
    auto *navigationBack =
        window.findChild<QPushButton *>(QStringLiteral("navigationBackButton"));
    QTest::mouseClick(navigationBack, Qt::LeftButton);
    QVERIFY(detailPage->isVisible());

    auto *backButton =
        window.findChild<QPushButton *>(QStringLiteral("stationDetailBackButton"));
    QTest::mouseClick(backButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_1")) != nullptr);

    auto *keywordInput =
        window.findChild<QLineEdit *>(QStringLiteral("stationKeywordInput"));
    auto *refreshButton =
        window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"));
    window.findChild<QPushButton *>("stationSearchEntry")->click();
    QVERIFY(window.findChild<QWidget *>("stationSearchPage")->isVisible());

    keywordInput->setText(QStringLiteral("和平"));
    QTest::mouseClick(refreshButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_1")) == nullptr);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_2")) != nullptr);
    QTRY_VERIFY(refreshButton->isEnabled());

    auto *message =
        window.findChild<QLabel *>(QStringLiteral("stationListMessage"));
    keywordInput->setText(QStringLiteral("不存在的站点"));
    QTest::mouseClick(refreshButton, Qt::LeftButton);
    QTRY_COMPARE(message->text(), QStringLiteral("没有找到符合条件的充电站"));
    QVERIFY(window.findChild<QLabel *>("stationSearchMessage")->text().contains(QStringLiteral("0")));
}

void MainWindowTests::stationDetailCanPrepareDirectCharging()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<StationBrowserPage *>()->directChargingRequested("PILE-A-01");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QCOMPARE(window.findChild<ChargingPage *>()->pileCode(),QStringLiteral("PILE-A-01"));
    QCOMPARE(started.count(),0);
    QVERIFY(!window.findChild<ScanPage *>()->isVisible());
}

void MainWindowTests::chargingStartLeavesNavigationForHomeOverview()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingStartButton")->isVisible()
                && window.findChild<QPushButton *>("chargingStartButton")->isEnabled());
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<QPushButton *>("chargingStartButton")->click();
    QTRY_COMPARE(started.count(),1);
    QVERIFY(qvariant_cast<OrderResult>(started.first().first()).ok());
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("正在充电")));
    QCOMPARE(navigation->currentIndex(),1);
    QVERIFY(window.findChild<QWidget *>("chargingProgressRing")->isVisible());
}

void MainWindowTests::locationCanResolveAndOpenMockRoute()
{
    MockChargingApi api;
    MainWindow window(api);
    window.resize(360, 640);
    window.show();
    loginFixtureUser(window);
    window.findChild<QPushButton *>(QStringLiteral("stationLocationEntry"))->click();

    auto *preset =
        window.findChild<QComboBox *>(QStringLiteral("locationPresetCombo"));
    auto *address =
        window.findChild<QLineEdit *>(QStringLiteral("locationAddressInput"));
    auto *resolve =
        window.findChild<QPushButton *>(QStringLiteral("resolveLocationButton"));
    auto *summary =
        window.findChild<QLabel *>(QStringLiteral("stationLocationSummary"));
    auto *locationMessage =
        window.findChild<QLabel *>(QStringLiteral("locationMessage"));
    auto *locationHint =
        window.findChild<QLabel *>(QStringLiteral("locationInputHint"));
    QVERIFY(preset != nullptr);
    QCOMPARE(preset->count(), 4);
    QVERIFY(address->placeholderText().contains(QStringLiteral("城市")));
    QVERIFY(locationHint->text().contains(QStringLiteral("城市和地址")));

    preset->setCurrentIndex(1);
    QCOMPARE(address->text(), QStringLiteral("沈阳市和平区"));
    address->setFocus();
    QTest::keyClicks(address, "1");
    QCOMPARE(preset->currentText(), QStringLiteral("手动输入地址"));
    QCOMPARE(address->text(), QStringLiteral("沈阳市和平区1"));
    preset->setCurrentIndex(2);
    preset->setCurrentIndex(1);
    QCOMPARE(address->text(), QStringLiteral("沈阳市和平区"));
    QTest::mouseClick(resolve, Qt::LeftButton);
    QTRY_COMPARE(locationMessage->text(),
                 QStringLiteral("位置已更新，充电站距离已重新计算"));
    QVERIFY(summary->text().contains(QStringLiteral("沈阳市和平区")));
    QCOMPARE(window.findChild<StationBrowserPage *>()->currentLocation().longitude, 123.4);

    address->setText(QStringLiteral("无法解析的位置"));
    QTest::mouseClick(resolve, Qt::LeftButton);
    QTRY_VERIFY(locationMessage->text().contains(QStringLiteral("未能解析")));
    QVERIFY(summary->text().contains(QStringLiteral("沈阳市和平区")));

    window.findChild<QPushButton *>(QStringLiteral("stationLocationBack"))->click();
    QTRY_VERIFY(window.findChild<QAbstractButton *>(QStringLiteral("stationMarker_2")));
    QTest::mouseClick(window.findChild<QAbstractButton *>(QStringLiteral("stationMarker_2")), Qt::LeftButton);
    auto *navigate = window.findChild<QPushButton *>(QStringLiteral("stationPreviewNavigationButton"));
    QTest::mouseClick(navigate, Qt::LeftButton);
    auto *navigationPage =
        window.findChild<QWidget *>(QStringLiteral("stationNavigationPage"));
    auto *routeStart =
        window.findChild<QLineEdit *>(QStringLiteral("routeStartInput"));
    auto *destination =
        window.findChild<QLabel *>(QStringLiteral("routeDestination"));
    auto *routeMode =
        window.findChild<QComboBox *>(QStringLiteral("routeModeCombo"));
    auto *routeButton =
        window.findChild<QPushButton *>(QStringLiteral("routePlanButton"));
    auto *routeDisplay =
        window.findChild<QLabel *>(QStringLiteral("routeDisplay"));
    auto *routeDisplayStack =
        window.findChild<QStackedWidget *>(QStringLiteral("routeDisplayStack"));
    auto *routeControlsCard =
        window.findChild<QWidget *>(QStringLiteral("routeControlsCard"));
    auto *routeMessage =
        window.findChild<QLabel *>(QStringLiteral("routeMessage"));
    QVERIFY(navigationPage->isVisible());
    QVERIFY(routeDisplayStack != nullptr);
    QVERIFY(routeControlsCard != nullptr);
    QVERIFY(routeDisplayStack->minimumHeight() <= 160);
    QCOMPARE(routeDisplayStack->sizePolicy().verticalPolicy(),
             QSizePolicy::Expanding);
    QTRY_VERIFY(routeDisplayStack->height() >= 120);
    QVERIFY(routeDisplayStack->geometry().bottom()
            <= navigationPage->contentsRect().bottom());
    QCOMPARE(routeStart->text(), QStringLiteral("沈阳市和平区"));
    QVERIFY(destination->text().contains(QStringLiteral("和平演示充电站")));
    QVERIFY(!destination->text().startsWith(QStringLiteral("终点")));
    QVERIFY(!destination->text().contains(QStringLiteral("123.4000")));
    QCOMPARE(routeMode->count(), 4);

    routeMode->setCurrentIndex(1);
    routeStart->setText(QStringLiteral("沈阳市浑南区"));
    QTest::mouseClick(routeButton, Qt::LeftButton);
    QTRY_VERIFY(routeDisplay->text().contains(QStringLiteral("步行路线")));
    QVERIFY(routeDisplay->text().contains(QStringLiteral("沈阳市浑南区")));
    QCOMPARE(routeMessage->text(), QStringLiteral("Mock 路线已生成"));

    routeMode->setCurrentIndex(2);
    QCOMPARE(routeMode->currentText(), QStringLiteral("公共交通"));
    QTest::mouseClick(routeButton, Qt::LeftButton);
    QTRY_VERIFY(routeDisplay->text().contains(QStringLiteral("公共交通路线")));
    QVERIFY(routeDisplay->text().contains(QStringLiteral("离线 Mock")));
    QVERIFY(routeButton->isEnabled());

    routeMode->setCurrentIndex(3);
    QCOMPARE(routeMode->currentText(), QStringLiteral("骑行"));
    QTest::mouseClick(routeButton, Qt::LeftButton);
    QTRY_VERIFY(routeDisplay->text().contains(QStringLiteral("骑行路线")));
    QVERIFY(routeButton->isEnabled());

    routeStart->setText(QStringLiteral("无法解析的位置"));
    QTest::mouseClick(routeButton, Qt::LeftButton);
    QTRY_VERIFY(routeMessage->text().contains(QStringLiteral("未能解析")));
    QVERIFY(routeButton->isEnabled());

    auto *navigationBack =
        window.findChild<QPushButton *>(QStringLiteral("navigationBackButton"));
    QTest::mouseClick(navigationBack, Qt::LeftButton);
    auto *stationListPage =
        window.findChild<QWidget *>(QStringLiteral("stationListPage"));
    QVERIFY(stationListPage->isVisible());
}

void MainWindowTests::reservationAppearsOnHomeAndCanBeCancelled()
{
    MockChargingApi api;
    MainWindow window(api);
    window.show();
    loginFixtureUser(window);

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_1")) != nullptr);
    auto *stationOneCard =
        window.findChild<QWidget *>(QStringLiteral("stationMarker_1"));
    QTest::mouseClick(stationOneCard, Qt::LeftButton);
    openPreviewDetails(window);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-A-01")) != nullptr);
    auto *reserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-A-01"));
    QVERIFY(reserveButton->isEnabled());
    bool reservationDialogSeen = false;
    handleDialogWhenShown(
        window,
        QStringLiteral("reservationSuccessDialog"),
        [&window, &reservationDialogSeen](QMessageBox *dialog) {
        QVERIFY(window.findChild<QWidget *>(
                    QStringLiteral("stationDetailPage"))->isVisible());
        QCOMPARE(dialog->windowTitle(), QStringLiteral("预约成功"));
        QCOMPARE(dialog->button(QMessageBox::Ok)->text(), QStringLiteral("知道了"));
        QVERIFY(dialog->text().contains(QStringLiteral("30 分钟")));
        QVERIFY(dialog->text().contains(QStringLiteral("前开始充电")));
        reservationDialogSeen = true;
        dialog->button(QMessageBox::Ok)->click();
    });
    QTest::mouseClick(reserveButton, Qt::LeftButton);
    QTRY_VERIFY(reservationDialogSeen);

    auto *currentOrderCard =
        window.findChild<QWidget *>(QStringLiteral("currentOrderCard"));
    auto *currentOrderSummary =
        window.findChild<QLabel *>(QStringLiteral("currentOrderSummary"));
    auto *actionMessage =
        window.findChild<QLabel *>(QStringLiteral("stationActionMessage"));
    QTRY_VERIFY(currentOrderCard->isVisible());
    expandCurrentOrder(window);
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("PILE-A-01")));
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("预约中")));
    QVERIFY(currentOrderSummary->text().contains(QStringLiteral("北京时间")));
    QCOMPARE(actionMessage->text(), QStringLiteral("预约成功"));

    QTRY_VERIFY(window.findChild<QWidget *>(
                    QStringLiteral("stationMarker_1")) != nullptr);
    QTest::mouseClick(
        window.findChild<QWidget *>(QStringLiteral("stationMarker_1")),
        Qt::LeftButton,
        Qt::NoModifier,
        QPoint(12, 12));
    openPreviewDetails(window);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("directChargeButton_PILE-A-01")) != nullptr);
    QTRY_COMPARE(window.findChild<QLabel *>(
                     QStringLiteral("pileStatus_PILE-A-01"))->text(),
                 QStringLiteral("已预约"));
    auto *reservedDirectButton = window.findChild<QPushButton *>(
        QStringLiteral("directChargeButton_PILE-A-01"));
    auto *reservedReserveButton = window.findChild<QPushButton *>(
        QStringLiteral("reserveButton_PILE-A-01"));
    QCOMPARE(reservedDirectButton->text(), QStringLiteral("开始充电"));
    QVERIFY(reservedDirectButton->isEnabled());
    QCOMPARE(reservedReserveButton->text(), QStringLiteral("不可预约"));
    QVERIFY(!reservedReserveButton->isEnabled());
    QTest::mouseClick(
        window.findChild<QPushButton *>(QStringLiteral("stationDetailBackButton")),
        Qt::LeftButton);
    QTRY_VERIFY(currentOrderCard->isVisible());
    expandCurrentOrder(window);

    auto *currentOrderNavigate = window.findChild<QPushButton *>(
        QStringLiteral("currentOrderNavigationButton"));
    QVERIFY(currentOrderNavigate->isVisible());
    QTest::mouseClick(currentOrderNavigate, Qt::LeftButton);
    auto *navigationPage =
        window.findChild<QWidget *>(QStringLiteral("stationNavigationPage"));
    auto *routeDestination =
        window.findChild<QLabel *>(QStringLiteral("routeDestination"));
    QTRY_VERIFY(navigationPage->isVisible());
    QVERIFY(routeDestination->text().contains(QStringLiteral("浑南演示充电站")));
    auto *navigationBack =
        window.findChild<QPushButton *>(QStringLiteral("navigationBackButton"));
    QTest::mouseClick(navigationBack, Qt::LeftButton);
    QTRY_VERIFY(currentOrderCard->isVisible());
    expandCurrentOrder(window);

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_2")) != nullptr);
    auto *stationTwoCard =
        window.findChild<QWidget *>(QStringLiteral("stationMarker_2"));
    QTest::mouseClick(stationTwoCard, Qt::LeftButton);
    openPreviewDetails(window);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-B-02")) != nullptr);
    auto *secondReserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-B-02"));
    QTest::mouseClick(secondReserveButton, Qt::LeftButton);
    QTRY_COMPARE(window.findChild<QTabWidget *>("mainNavigation")->currentIndex(), 1);
    window.findChild<QTabWidget *>("mainNavigation")->setCurrentIndex(0);
    QTRY_VERIFY(currentOrderCard->isVisible());
    expandCurrentOrder(window);
    QCOMPARE(actionMessage->text(),
             QStringLiteral("您已有进行中的订单，请先处理当前订单"));

    auto *reservationScanButton = window.findChild<QPushButton *>(
        QStringLiteral("startReservedChargingButton"));
    QVERIFY(reservationScanButton->isVisible());
    QTest::mouseClick(reservationScanButton, Qt::LeftButton);
    auto *navigation =
        window.findChild<QTabWidget *>(QStringLiteral("mainNavigation"));
    QCOMPARE(navigation->currentIndex(), 1);
    QCOMPARE(window.findChild<ChargingPage *>()->pileCode(), QStringLiteral("PILE-A-01"));
    navigation->setCurrentIndex(0);
    QTRY_VERIFY(currentOrderCard->isVisible());
    expandCurrentOrder(window);

    auto *cancelButton =
        window.findChild<QPushButton *>(QStringLiteral("cancelReservationButton"));
    QVERIFY(cancelButton->isVisible());
    QTest::mouseClick(cancelButton, Qt::LeftButton);
    QTRY_COMPARE(actionMessage->text(), QStringLiteral("预约已取消"));
    QTRY_VERIFY(!currentOrderCard->isVisible());

    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_1")) != nullptr);
    stationOneCard =
        window.findChild<QWidget *>(QStringLiteral("stationMarker_1"));
    QTest::mouseClick(stationOneCard, Qt::LeftButton);
    openPreviewDetails(window);
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
    navigation->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileOrdersButton")->click();
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
    auto *detailHeading =
        window.findChild<QLabel *>(QStringLiteral("orderDetailHeading"));
    auto *detailCard =
        window.findChild<QWidget *>(QStringLiteral("orderDetailCard"));
    auto *detailScrollArea =
        window.findChild<QScrollArea *>(QStringLiteral("orderDetailScrollArea"));
    QTRY_VERIFY(detailPage->isVisible());
    QVERIFY(detailHeading != nullptr);
    QVERIFY(detailCard != nullptr);
    QVERIFY(detailScrollArea != nullptr);
    QCOMPARE(detailHeading->font().pointSize(), 24);
    QVERIFY(detailNumber->text().contains(QStringLiteral("DEMO-COMPLETED-101")));
    QVERIFY(detailBody->text().contains(QStringLiteral("<table")));
    QVERIFY(detailBody->text().contains(QStringLiteral("width=\"92\"")));
    QVERIFY(detailBody->accessibleDescription().contains(
        QStringLiteral("订单金额：¥6.75")));
    QVERIFY(detailBody->accessibleDescription().contains(
        QStringLiteral("充电量：5.00 度")));
    auto *detailNavigation = window.findChild<QPushButton *>(
        QStringLiteral("orderDetailNavigationButton"));
    QVERIFY(!detailNavigation->isVisible());

    auto *orderBackButton =
        window.findChild<QPushButton *>(QStringLiteral("orderDetailBackButton"));
    QTest::mouseClick(orderBackButton, Qt::LeftButton);
    navigation->setCurrentIndex(0);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("stationMarker_1")) != nullptr);
    auto *stationRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("stationRefreshButton"));
    QTRY_VERIFY(stationRefreshButton->isEnabled());
    auto *stationCard =
        window.findChild<QWidget *>(QStringLiteral("stationMarker_1"));
    QTest::mouseClick(stationCard, Qt::LeftButton);
    openPreviewDetails(window);
    QTRY_VERIFY(window.findChild<QPushButton *>(
                    QStringLiteral("reserveButton_PILE-A-01")) != nullptr);
    auto *reserveButton =
        window.findChild<QPushButton *>(QStringLiteral("reserveButton_PILE-A-01"));
    handleDialogWhenShown(
        window,
        QStringLiteral("reservationSuccessDialog"),
        [](QMessageBox *dialog) { dialog->button(QMessageBox::Ok)->click(); });
    QTest::mouseClick(reserveButton, Qt::LeftButton);
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("currentOrderCard"))->isVisible());

    navigation->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileOrdersButton")->click();
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
    QVERIFY(detailNavigation->isVisible());
    QTest::mouseClick(detailNavigation, Qt::LeftButton);
    auto *stationNavigationPage =
        window.findChild<QWidget *>(QStringLiteral("stationNavigationPage"));
    QTRY_COMPARE(navigation->currentIndex(), 0);
    QTRY_VERIFY(stationNavigationPage->isVisible());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("routeDestination"))
                ->text()
                .contains(QStringLiteral("浑南演示充电站")));
    QTest::mouseClick(
        window.findChild<QPushButton *>(QStringLiteral("navigationBackButton")),
        Qt::LeftButton);
    navigation->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileOrdersButton")->click();
    auto *orderListPage =
        window.findChild<QWidget *>(QStringLiteral("orderListPage"));
    auto *orderRefreshButton =
        window.findChild<QPushButton *>(QStringLiteral("orderRefreshButton"));
    QTRY_VERIFY(orderListPage->isVisible());
    QTRY_VERIFY(orderRefreshButton->isEnabled());
    QTRY_VERIFY(window.findChild<QWidget *>(
                    QStringLiteral("orderCard_1001")) != nullptr);
    reservedCard =
        window.findChild<QWidget *>(QStringLiteral("orderCard_1001"));
    QTest::mouseClick(reservedCard, Qt::LeftButton);
    QTRY_VERIFY(detailPage->isVisible());
    QTRY_VERIFY(cancelButton->isEnabled());
    QTest::mouseClick(cancelButton, Qt::LeftButton);

    auto *orderMessage =
        window.findChild<QLabel *>(QStringLiteral("orderListMessage"));
    QTRY_VERIFY(orderListPage->isVisible());
    QTRY_VERIFY(orderRefreshButton->isEnabled());
    QTRY_VERIFY(window.findChild<QWidget *>(QStringLiteral("orderCard_1001")) != nullptr);
    reservedStatus = window.findChild<QLabel *>(QStringLiteral("orderStatus_1001"));
    QTRY_COMPARE(reservedStatus->text(), QStringLiteral("已取消"));
    QCOMPARE(orderMessage->text(), QStringLiteral("预约已取消，订单状态已刷新"));
}

void MainWindowTests::leavingOrderDetailRefreshesChangedOrderState()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    navigation->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileOrdersButton")->click();
    QTRY_VERIFY(window.findChild<QWidget *>("orderListPage")->isVisible());
    window.findChild<QPushButton *>("ordersBackButton")->click();
    QVERIFY(window.findChild<QPushButton *>("profileOrdersButton")->isVisible());
    QCOMPARE(navigation->currentIndex(),4);
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingStartButton")->isVisible()
                && window.findChild<QPushButton *>("chargingStartButton")->isEnabled());
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<QPushButton *>("chargingStartButton")->click();
    QTRY_COMPARE(started.count(),1);
    QVERIFY(qvariant_cast<OrderResult>(started.first().first()).ok());
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("正在充电")));
    navigation->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileOrdersButton")->click();
    QTRY_VERIFY(window.findChild<QWidget *>("orderListPage")->isVisible());
    QTRY_VERIFY(window.findChild<QLabel *>("orderStatus_1001"));
    QCOMPARE(window.findChild<QLabel *>("orderStatus_1001")->text(),QStringLiteral("充电中"));
}

void MainWindowTests::simulatedScanStartsChargingAndRefreshesHome()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingStartButton")->isVisible()
                && window.findChild<QPushButton *>("chargingStartButton")->isEnabled());
    QSignalSpy requests(&api,&IChargingApi::chargingStartCompleted);
    QTest::qWait(20); QCOMPARE(requests.count(),0);
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<QPushButton *>("chargingStartButton")->click();
    QTRY_COMPARE(started.count(),1);
    QVERIFY(qvariant_cast<OrderResult>(started.first().first()).ok());
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("正在充电")));
    window.findChild<ScanPage *>()->submitPileCode("PILE-B-02");
    QCOMPARE(window.findChild<ChargingPage *>()->pileCode(),QStringLiteral("PILE-A-01"));
    QCOMPARE(requests.count(),1);
}

void MainWindowTests::reservationStartsSameOrderOnChargingPage()
{
    MockChargingApi api; MainWindow window(api); window.show(); loginFixtureUser(window);
    QSignalSpy reserved(&api,&IChargingApi::reservationCompleted);
    (void)api.reserve("PILE-A-01"); QTRY_COMPARE(reserved.count(),1);
    const auto reservation=qvariant_cast<OrderResult>(reserved.first().first());
    QVERIFY(reservation.ok());
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<StationBrowserPage *>()->reservationScanRequested("PILE-A-01");
    QCOMPARE(window.findChild<QTabWidget *>("mainNavigation")->currentIndex(),1);
    QCOMPARE(started.count(),0);
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingStartButton")->isEnabled());
    window.findChild<QPushButton *>("chargingStartButton")->click();
    QTRY_COMPARE(started.count(),1);
    const auto result=qvariant_cast<OrderResult>(started.first().first());
    QVERIFY(result.ok());
    QCOMPARE(result.payload->order.orderId,reservation.payload->order.orderId);
    QVERIFY(result.payload->order.status==charging::protocol::OrderStatus::Charging);
}

void MainWindowTests::reservationExpiryRefreshesHomeWithoutNavigation()
{
    auto now = QDateTime::fromString(QStringLiteral("2026-09-08T15:45:00Z"), Qt::ISODate);
    MockChargingApi api(nullptr, [&now] { return now; });
    MainWindow window(api); window.show(); loginFixtureUser(window);
    QSignalSpy reserved(&api, &IChargingApi::reservationCompleted);
    QSignalSpy started(&api, &IChargingApi::chargingStartCompleted);
    (void)api.reserve("PILE-A-01"); QTRY_COMPARE(reserved.count(), 1);
    QVERIFY(qvariant_cast<OrderResult>(reserved.first().first()).ok());
    auto *card = window.findChild<QWidget *>("currentOrderCard");
    auto *summary = window.findChild<QLabel *>("currentOrderSummary");
    QTRY_VERIFY(card->isVisible());
    QVERIFY(summary->text().contains(QStringLiteral("09-09 00:15:00")));
    auto *navigation = window.findChild<QTabWidget *>("mainNavigation");
    QCOMPARE(navigation->currentIndex(), 0);
    now = now.addSecs(charging::protocol::DemoReservationDurationSeconds);
    QTRY_VERIFY(!card->isVisible());
    QCOMPARE(navigation->currentIndex(), 0); // background refresh does not steal the tab
    QCOMPARE(started.count(), 0);
    QCOMPARE(window.findChild<QLabel *>("chargingState")->text(), QStringLiteral("预约已取消"));
    QTRY_VERIFY(window.findChild<QWidget *>("stationMarker_1") != nullptr);
    QTest::mouseClick(window.findChild<QWidget *>("stationMarker_1"), Qt::LeftButton);
    openPreviewDetails(window);
    QTRY_VERIFY(window.findChild<QPushButton *>("reserveButton_PILE-A-01") != nullptr);
    QVERIFY(window.findChild<QPushButton *>("reserveButton_PILE-A-01")->isEnabled());
}

void MainWindowTests::scannerAdapterCanSubmitDecodedPileCode()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    auto *scan=window.findChild<ScanPage *>();
    scan->submitPileCode("https://example.com/PILE-A-01");
    QCOMPARE(navigation->currentIndex(),0);
    scan->submitPileCode("  PILE-A-01  ");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QCOMPARE(window.findChild<ChargingPage *>()->pileCode(),QStringLiteral("PILE-A-01"));
}

void MainWindowTests::chargingProgressCanRefreshAndStopWithConfirmation()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingStartButton")->isVisible()
                && window.findChild<QPushButton *>("chargingStartButton")->isEnabled());
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<QPushButton *>("chargingStartButton")->click();
    QTRY_COMPARE(started.count(),1);
    QVERIFY(qvariant_cast<OrderResult>(started.first().first()).ok());
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("正在充电")));
    auto order=qvariant_cast<OrderResult>(started.first().first()).payload->order;
    QSignalSpy progress(&api,&IChargingApi::chargingProgressCompleted);
    (void)api.getChargingProgress(order.orderId);QTRY_COMPARE(progress.count(),1);
    window.findChild<ChargingController *>()->refresh();
    QTRY_VERIFY(window.findChild<QLabel *>("chargingDuration")->text()!="00:00");
    QTimer::singleShot(10,&window,[]{for(auto*w:QApplication::topLevelWidgets())if(auto*d=qobject_cast<QMessageBox*>(w))if(d->button(QMessageBox::Yes))d->done(QMessageBox::Yes);});
    window.findChild<QPushButton *>("chargingEndButton")->click();
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("已结束")));
    navigation->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileOrdersButton")->click();
    QTRY_VERIFY(window.findChild<QWidget *>("orderListPage")->isVisible());
    QTRY_VERIFY(window.findChild<QLabel *>("orderStatus_1001"));
    QCOMPARE(window.findChild<QLabel *>("orderStatus_1001")->text(),QStringLiteral("已完成"));
}

void MainWindowTests::stoppingFromOrderDetailRefreshesOpenStationDetail()
{
    MockChargingApi api;
    MainWindow window(api); window.show(); loginFixtureUser(window);
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingStartButton")->isVisible()
                && window.findChild<QPushButton *>("chargingStartButton")->isEnabled());
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<QPushButton *>("chargingStartButton")->click();
    QTRY_COMPARE(started.count(),1);
    QVERIFY(qvariant_cast<OrderResult>(started.first().first()).ok());
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("正在充电")));
    navigation->setCurrentIndex(4);
    window.findChild<QPushButton *>("profileOrdersButton")->click();
    QTRY_VERIFY(window.findChild<QWidget *>("orderListPage")->isVisible());
    QTRY_VERIFY(window.findChild<QWidget *>("orderCard_1001"));
    window.findChild<QWidget *>("orderCard_1001")->setFocus();
    QTest::mouseClick(window.findChild<QWidget *>("orderCard_1001"),Qt::LeftButton);
    auto *view=window.findChild<QPushButton *>("orderDetailProgressButton");
    QTRY_VERIFY(view->isVisible());view->click();
    QTRY_COMPARE(navigation->currentIndex(),1);
    QTimer::singleShot(10,&window,[]{for(auto*w:QApplication::topLevelWidgets())if(auto*d=qobject_cast<QMessageBox*>(w))if(d->button(QMessageBox::Yes))d->done(QMessageBox::Yes);});
    window.findChild<QPushButton *>("chargingEndButton")->click();
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("已结束")));

}

void MainWindowTests::pendingOrderLinksRechargeAndCanBeSettled()
{
    MockChargingApi api; MainWindow window(api); window.show();
    window.findChild<QLineEdit *>("phoneInput")->setText("13912345678");
    window.findChild<QLineEdit *>("verificationCodeInput")->setText("123456");
    window.findChild<QPushButton *>("loginButton")->click();
    auto *navigation=window.findChild<QTabWidget *>("mainNavigation");
    QTRY_VERIFY(navigation->isVisible());
    window.findChild<ScanPage *>()->submitPileCode("PILE-A-01");
    QTRY_COMPARE(navigation->currentIndex(),1);
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingStartButton")->isVisible()
                && window.findChild<QPushButton *>("chargingStartButton")->isEnabled());
    QSignalSpy started(&api,&IChargingApi::chargingStartCompleted);
    window.findChild<QPushButton *>("chargingStartButton")->click();
    QTRY_COMPARE(started.count(),1);
    QVERIFY(qvariant_cast<OrderResult>(started.first().first()).ok());
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("正在充电")));
    auto order=qvariant_cast<OrderResult>(started.first().first()).payload->order;
    QSignalSpy progress(&api,&IChargingApi::chargingProgressCompleted);
    (void)api.getChargingProgress(order.orderId);QTRY_COMPARE(progress.count(),1);
    QTimer::singleShot(10,&window,[]{for(auto*w:QApplication::topLevelWidgets())if(auto*d=qobject_cast<QMessageBox*>(w))if(d->button(QMessageBox::Yes))d->done(QMessageBox::Yes);});
    window.findChild<QPushButton *>("chargingEndButton")->click();
    QTRY_VERIFY(window.findChild<QLabel *>("chargingState")->text().contains(QStringLiteral("已结束")));
    QTRY_VERIFY(window.findChild<QPushButton *>("chargingRechargeButton")->isVisible());
    window.findChild<QPushButton *>("chargingRechargeButton")->click();
    QCOMPARE(navigation->currentIndex(),4);
    QSignalSpy recharged(&api,&IChargingApi::rechargeCompleted);
    (void)api.recharge(1000);QTRY_COMPARE(recharged.count(),1);
    QSignalSpy paid(&api,&IChargingApi::paymentCompleted);(void)api.payOrder(order.orderId);
    QTRY_COMPARE(paid.count(),1);QVERIFY(qvariant_cast<PaymentResult>(paid.first().first()).ok());
    navigation->setCurrentIndex(1);window.findChild<ChargingController *>()->refresh();
    QTRY_VERIFY(!window.findChild<QPushButton *>("chargingRechargeButton")->isVisible());
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

    navigation->setCurrentIndex(4);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));
    QCOMPARE(nicknameLabel->text(), QStringLiteral("演示用户0001"));
    QTRY_VERIFY(nicknameLabel->visibleRegion().contains(nicknameLabel->rect().center()));
    QTRY_VERIFY(phoneLabel->visibleRegion().contains(phoneLabel->rect().center()));
    QCOMPARE(phoneLabel->text(), QStringLiteral("手机号：13800000001"));
    QCOMPARE(balanceLabel->text(), QStringLiteral("¥200.00"));

    window.findChild<QPushButton *>("profileDetailsButton")->click();
    QVERIFY(window.findChild<QWidget *>("profileDetailPage")->isVisible());
    nicknameInput->setText(QStringLiteral("新的昵称"));
    QTest::mouseClick(saveButton, Qt::LeftButton);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("昵称已更新"));
    QCOMPARE(nicknameLabel->text(), QStringLiteral("新的昵称"));
    QVERIFY(window.findChild<QPushButton *>("headerAccountButton")->text().contains(QStringLiteral("新的昵称")));
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，新的昵称"));

    nicknameInput->setText(QStringLiteral("尚未保存的昵称"));
    window.findChild<QPushButton *>("profileDetailBack")->click();
    window.findChild<QPushButton *>("profileDetailsButton")->click();
    QCOMPARE(nicknameInput->text(), QStringLiteral("新的昵称"));
    QVERIFY(window.findChild<QPushButton *>("headerAccountButton")->text().contains("138****0001"));

    navigation->setCurrentIndex(0);
    QCOMPARE(welcomeLabel->text(), QStringLiteral("你好，新的昵称"));
    navigation->setCurrentIndex(4);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));

    amountInput->setText(QStringLiteral("10"));
    bool rechargeSuccessDialogSeen = false;
    handleDialogWhenShown(
        window,
        QStringLiteral("rechargeSuccessDialog"),
        [&rechargeSuccessDialogSeen](QMessageBox *dialog) {
        QCOMPARE(dialog->windowTitle(), QStringLiteral("充值成功"));
        QVERIFY(dialog->text().contains(QStringLiteral("当前余额 ¥210.00")));
        QCOMPARE(dialog->button(QMessageBox::Ok)->text(),
                 QStringLiteral("知道了"));
        rechargeSuccessDialogSeen = true;
        dialog->button(QMessageBox::Ok)->click();
    });
    QTest::mouseClick(rechargeButton, Qt::LeftButton);
    QTRY_VERIFY(rechargeSuccessDialogSeen);
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

    navigation->setCurrentIndex(4);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));

    amountInput->clear();
    amountInput->setFocus();
    QTest::keyClicks(amountInput, QStringLiteral("1e2"));
    QCOMPARE(amountInput->text(), QStringLiteral("12"));
    amountInput->clear();
    QTest::keyClicks(amountInput, QStringLiteral("1E2"));
    QCOMPARE(amountInput->text(), QStringLiteral("12"));

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

    navigation->setCurrentIndex(4);
    QTRY_COMPARE(messageLabel->text(), QStringLiteral("资料已刷新"));
    QTest::mouseClick(logoutButton, Qt::LeftButton);

    QTRY_COMPARE(pages->currentWidget(), loginPage);
}

QTEST_MAIN(MainWindowTests)

#include "main_window_tests.moc"
