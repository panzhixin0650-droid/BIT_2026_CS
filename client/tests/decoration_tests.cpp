#include "api/mock_charging_api.h"
#include "ui/main_window.h"

#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QtTest>

using namespace charging::client;

class DecorationTests final : public QObject {
    Q_OBJECT
private slots:
    void transparentAssetsPreserveWhite();
    void responsivePages_data();
    void responsivePages();
};

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

void DecorationTests::responsivePages_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("mobile") << QSize(360, 640);
    QTest::newRow("default") << QSize(480, 860);
    QTest::newRow("desktop") << QSize(1100, 800);
}

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

QTEST_MAIN(DecorationTests)
#include "decoration_tests.moc"
