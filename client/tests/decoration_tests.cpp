// 本文件测试手绘装饰素材与各页面在不同尺寸下的布局
#include "api/mock_charging_api.h"
#include "ui/main_window.h"
#include "ui/client_theme.h"
#include "ui/profile_page.h"

#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtTest>

using namespace charging::client;

// 装饰与布局测试集合
class DecorationTests final : public QObject {
    Q_OBJECT
private slots:
    void transparentAssetsPreserveWhite();
    void responsivePages_data();
    void responsivePages();
    void profileCardsKeepInteractions_data();
    void profileCardsKeepInteractions();
};

// 装饰图边缘必须透明，角色白色区域保持不透明
void DecorationTests::transparentAssetsPreserveWhite()
{
    MockChargingApi api;
    MainWindow window(api); // Registers the static resource bundle.
    for (const auto &name : {"clover", "avocado", "bow", "leaves", "rabbit", "drink", "frog", "plant"}) {
        const QImage image(QStringLiteral(":/decorations/%1.png").arg(name));
        QVERIFY2(!image.isNull(), name);
        QVERIFY2(image.hasAlphaChannel(), name);
        int solidWhite = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (x == 0 || y == 0 || x == image.width() - 1 || y == image.height() - 1)
                    QCOMPARE(color.alpha(), 0);
                if (color.alpha() == 255 && color.red() > 245
                    && color.green() > 245 && color.blue() > 245) ++solidWhite;
            }
        }
        if (QString::fromLatin1(name) == "rabbit" || QString::fromLatin1(name) == "frog")
            QVERIFY2(solidWhite > image.width() * image.height() / 10,
                     "White character interiors must remain opaque");
    }
}

// 手机、默认与桌面三种窗口尺寸数据集
void DecorationTests::responsivePages_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("mobile") << QSize(360, 640);
    QTest::newRow("default") << QSize(480, 860);
    QTest::newRow("desktop") << QSize(1100, 800);
}

// 装饰不接收鼠标、不越界重叠，页面不横向滚动
void DecorationTests::responsivePages()
{
    QFETCH(QSize, size);
    MockChargingApi api;
    MainWindow window(api);
    window.resize(size);
    window.show();
    QTest::qWait(150);
    QCOMPARE(window.size(), size);
    const auto inspect = [&](const QString &page) {
        for (auto *widget : window.findChildren<QWidget *>()) {
            if (!widget->objectName().startsWith("handdrawnAccent_") || !widget->isVisible()) continue;
            QVERIFY(widget->testAttribute(Qt::WA_TransparentForMouseEvents));
            QCOMPARE(widget->focusPolicy(), Qt::NoFocus);
            QVERIFY(widget->accessibleName().isEmpty());
            QVERIFY(widget->parentWidget()->rect().contains(widget->geometry()));
            for (auto *sibling : widget->parentWidget()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (sibling != widget && sibling->isVisible())
                    QVERIFY(!sibling->geometry().intersects(widget->geometry()));
            }
        }
        for (auto *scroll : window.findChildren<QScrollArea *>()) {
            if (scroll->isVisible()) QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        }
        const QString directory = qEnvironmentVariable("BIT_DECOR_SCREENSHOTS");
        if (!directory.isEmpty()) {
            QDir().mkpath(directory);
            QVERIFY(window.grab().save(directory + '/' + page + '-'
                                       + QString::fromLatin1(QTest::currentDataTag()) + ".png"));
        }
    };
    inspect("login");
    window.findChild<QLineEdit *>("phoneInput")->setText("13800000001");
    window.findChild<QLineEdit *>("verificationCodeInput")->setText("123456");
    window.findChild<QPushButton *>("loginButton")->click();
    auto *tabs = window.findChild<QTabWidget *>("mainNavigation");
    QTRY_VERIFY(tabs->isVisible());
    tabs->setCurrentIndex(3);
    QTest::qWait(150);
    QCOMPARE(window.findChild<QScrollArea *>("assistantScroll")->verticalScrollBar()->value(), 0);
    inspect("assistant");
    QVERIFY(window.findChild<QPushButton *>("assistantSuggestion0")->isEnabled());
    tabs->setCurrentIndex(4);
    QTest::qWait(150);
    inspect("profile");
    QVERIFY(window.findChild<QPushButton *>("rechargeButton")->isEnabled());
}

void DecorationTests::profileCardsKeepInteractions_data()
{
    responsivePages_data();
}

// 我的页卡片样式不影响按钮点击与钱包流程
void DecorationTests::profileCardsKeepInteractions()
{
    QFETCH(QSize, size);
    QWidget host;
    host.setStyleSheet(clientThemeStyleSheet());
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *profile = new ProfilePage(&host);
    layout->addWidget(profile);
    host.resize(size);
    host.show();

    auto *services = profile->findChild<QFrame *>("profileServicesCard");
    auto *wallet = profile->findChild<QFrame *>("profileWalletCard");
    auto *scroll = profile->findChild<QScrollArea *>();
    QVERIFY(services && wallet && scroll);
    QVERIFY(profile->styleSheet().isEmpty());
    QCOMPARE(services->styleSheet(), profileServicesStyleSheet());
    QCOMPARE(wallet->styleSheet(), profileWalletStyleSheet());
    QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);

    QSignalSpy orders(profile, &ProfilePage::ordersRequested);
    QSignalSpy repair(profile, &ProfilePage::repairRequested);
    QSignalSpy tickets(profile, &ProfilePage::ticketsRequested);
    const QStringList names = {"profileOrdersButton", "profileRepairButton", "profileTicketsButton"};
    const QStringList titles = {QStringLiteral("我的订单"), QStringLiteral("故障报修"), QStringLiteral("我的工单")};
    for (int index = 0; index < names.size(); ++index) {
        auto *button = services->findChild<QPushButton *>(names[index]);
        QVERIFY(button);
        QCOMPARE(profile->findChildren<QPushButton *>(names[index]).size(), 1);
        QCOMPARE(button->accessibleName(), titles[index]);
        QCOMPARE(button->height(), 52);
        QCOMPARE(button->findChildren<QLabel *>().size(), 3);
        for (auto *label : button->findChildren<QLabel *>()) {
            QVERIFY(label->testAttribute(Qt::WA_TransparentForMouseEvents));
            QVERIFY(button->rect().contains(label->geometry()));
        }
        scroll->ensureWidgetVisible(button);
        QTRY_VERIFY(button->visibleRegion().contains(button->rect().center()));
        QTest::mouseClick(button, Qt::LeftButton);
        button->setFocus();
        QTest::keyClick(button, Qt::Key_Space);
    }
    QCOMPARE(orders.count(), 2);
    QCOMPARE(repair.count(), 2);
    QCOMPARE(tickets.count(), 2);

    // 快捷金额只填写输入框，充值需点按钮，余额只随接口更新
    auto *amount = wallet->findChild<QLineEdit *>("rechargeAmountInput");
    auto *recharge = wallet->findChild<QPushButton *>("rechargeButton");
    auto *balance = wallet->findChild<QLabel *>("profileBalanceLabel");
    QVERIFY(amount && recharge && balance);
    profile->setBalance(12345);
    QCOMPARE(balance->text(), QStringLiteral("¥123.45"));
    QSignalSpy requested(profile, &ProfilePage::rechargeRequested);
    int quickAmountCount = 0;
    for (auto *button : wallet->findChildren<QPushButton *>()) {
        if (!button->property("rechargeAmount").isValid()) continue;
        ++quickAmountCount;
        scroll->ensureWidgetVisible(button);
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(amount->text(), button->property("rechargeAmount").toString());
        QVERIFY(button->isChecked());
        QCOMPARE(button->height(), 32);
        QCOMPARE(requested.count(), 0); // Selecting an amount never recharges automatically.
    }
    QCOMPARE(quickAmountCount, 4);
    amount->setText("12.34");
    for (auto *button : wallet->findChildren<QPushButton *>()) {
        if (button->property("rechargeAmount").isValid()) QVERIFY(!button->isChecked());
    }
    QVERIFY(amount->hasAcceptableInput());
    scroll->ensureWidgetVisible(recharge);
    QTRY_VERIFY(recharge->visibleRegion().contains(recharge->rect().center()));
    QTest::mouseClick(recharge, Qt::LeftButton);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("12.34"));
    QCOMPARE(balance->text(), QStringLiteral("¥123.45")); // Only API responses update balance.
    // 忙碌时禁用充值输入，金额超上限视为非法
    profile->setBusy(true);
    QVERIFY(!recharge->isEnabled());
    QVERIFY(!amount->isEnabled());
    QTest::mouseClick(recharge, Qt::LeftButton);
    QCOMPARE(requested.count(), 1);
    profile->setBusy(false);
    QVERIFY(recharge->isEnabled());
    QVERIFY(amount->isEnabled());
    amount->setText("10000.01");
    QVERIFY(!amount->hasAcceptableInput());

    // The local card styles must not change main's identity/detail/avatar flow.
    // 卡片样式不改变身份信息、详情与头像页的跳转
    QCOMPARE(profile->findChild<QLabel *>("profileAvatar")->size(), QSize(64, 64));
    QCOMPARE(profile->findChild<QLabel *>("profileNicknameLabel")->font().pointSize(), 15);
    QVERIFY(!profile->findChild<QPushButton *>("logoutButton")->isFlat());
    auto *details = profile->findChild<QPushButton *>("profileDetailsButton");
    details->click();
    QVERIFY(profile->findChild<QWidget *>("profileDetailPage")->isVisible());
    QVERIFY(!services->isVisible());
    QVERIFY(!wallet->isVisible());
    profile->findChild<QPushButton *>("profileAvatarButton")->click();
    QVERIFY(profile->findChild<QWidget *>("profileAvatarPage")->isVisible());
    profile->findChild<QPushButton *>("profileAvatarBack")->click();
    profile->findChild<QPushButton *>("profileDetailBack")->click();
    QVERIFY(services->isVisible());
    QVERIFY(wallet->isVisible());
    QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
}

QTEST_MAIN(DecorationTests)
#include "decoration_tests.moc"
