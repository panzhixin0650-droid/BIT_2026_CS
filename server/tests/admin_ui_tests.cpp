#include "admin_ui/admin_facade.h"
#include "admin_ui/admin_window.h"
#include "application/application_service.h"
#include "application/session_store.h"
#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "persistence/in_memory_repository.h"

#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QtTest>

using namespace charging::server;

namespace {

struct LoginFixture {
    InMemoryRepository repository;
    SessionStore sessions;
    MockPile pileGateway;
    MockPredictionProvider predictions;
    ApplicationService service{&repository, &sessions, &pileGateway, &predictions};
    AdminFacade facade{&service};
    AdminWindow window{&facade, false, 0, false};
    QWidget *page = window.findChild<QWidget *>(QStringLiteral("loginPage"));
    QList<QLineEdit *> inputs = page->findChildren<QLineEdit *>();
    QLineEdit *username = inputs.at(0);
    QLineEdit *password = inputs.at(1);
    QLabel *error = page->findChild<QLabel *>(QStringLiteral("loginError"));
    QPushButton *submit = page->findChild<QPushButton *>(QStringLiteral("loginSubmit"));
};

// Read the pixels already presented by Qt. QWidget::grab()/render() forces
// a new paint and can hide corruption caused by an incremental update.
QImage screenImage(QWidget &window, const QString &name)
{
    QTest::qWait(100);
    const QPixmap snapshot = window.screen()->grabWindow(window.winId());
    const QString artifactDirectory = qEnvironmentVariable("CHARGING_UI_ARTIFACT_DIR");
    if (!artifactDirectory.isEmpty() && !snapshot.isNull()) {
        snapshot.save(QDir(artifactDirectory).filePath(name + QStringLiteral(".png")));
    }
    return snapshot.toImage().scaled(window.size(), Qt::IgnoreAspectRatio,
                                    Qt::FastTransformation);
}

QString unexpectedColor(const QImage &image, const QRect &area, const QColor &expected)
{
    if (area.isEmpty() || !image.rect().contains(area)) {
        return QStringLiteral("Invalid sample area");
    }
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor actual = image.pixelColor(x, y);
            if (actual != expected) {
                return QStringLiteral("Pixel (%1, %2): expected %3, got %4")
                    .arg(x).arg(y).arg(expected.name(), actual.name());
            }
        }
    }
    return {};
}

// Sample the right side of each row, away from label text and rounded edges.
QRect formGapArea(const QWidget *above, const QWidget *below, const QWidget &window)
{
    const QPoint top = above->mapTo(&window, QPoint());
    const QPoint bottom = below->mapTo(&window, QPoint());
    return QRect(top.x() + above->width() - 70, top.y() + above->height() + 2, 40,
                 bottom.y() - top.y() - above->height() - 4);
}

QRect errorArea(const LoginFixture &fixture)
{
    const QPoint error = fixture.error->mapTo(&fixture.window, QPoint());
    return QRect(error.x() + fixture.error->width() - 70, error.y() + 8,
                 40, fixture.error->height() - 16);
}

QString unexpectedSurfacePixels(const LoginFixture &fixture, const QImage &image,
                                const QColor &errorColor)
{
    // The warning's own fill can look correct while the gaps above and below
    // it still reveal the artwork. Check the entire reserved row as well.
    const QList<QRect> gaps{
        formGapArea(fixture.password, fixture.error, fixture.window),
        formGapArea(fixture.error, fixture.submit, fixture.window),
        formGapArea(fixture.username, fixture.password, fixture.window),
    };
    for (const QRect &area : gaps) {
        const QString mismatch = unexpectedColor(image, area, QColor("#edf5fb"));
        if (!mismatch.isEmpty()) return mismatch;
    }
    return unexpectedColor(image, errorArea(fixture), errorColor);
}

} // namespace

class AdminUiTests final : public QObject {
    Q_OBJECT

private slots:
    void loginFailurePreservesInputAndAllowsRetry();
    void loginSurfaceSurvivesPartialRepaints_data();
    void loginSurfaceSurvivesPartialRepaints();
};

void AdminUiTests::loginFailurePreservesInputAndAllowsRetry()
{
    LoginFixture fixture;
    fixture.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    QApplication::setActiveWindow(&fixture.window);
    const QRect passwordGeometry = fixture.password->geometry();
    const QRect buttonGeometry = fixture.submit->geometry();
    const QRect errorGeometry = fixture.error->geometry();

    QTest::mouseClick(fixture.username, Qt::LeftButton);
    QTest::keyClicks(fixture.username, "admin");
    QTest::keyClick(fixture.username, Qt::Key_Tab);
    QTRY_VERIFY(fixture.password->hasFocus());
    QTest::keyClicks(fixture.password, "wrong-password");
    QTest::mouseClick(fixture.submit, Qt::LeftButton);

    QVERIFY(fixture.page->isVisible());
    QCOMPARE(fixture.username->text(), QStringLiteral("admin"));
    QCOMPARE(fixture.password->text(), QStringLiteral("wrong-password"));
    QCOMPARE(fixture.error->text(), QStringLiteral("账号或密码错误，请重试"));
    QVERIFY(fixture.error->property("hasError").toBool());
    QCOMPARE(fixture.password->echoMode(), QLineEdit::Password);
    QVERIFY(fixture.password->hasFocus());
    QCOMPARE(fixture.password->selectedText(), fixture.password->text());
    QCOMPARE(fixture.password->geometry(), passwordGeometry);
    QCOMPARE(fixture.submit->geometry(), buttonGeometry);
    QCOMPARE(fixture.error->geometry(), errorGeometry);

    QTest::keyClicks(fixture.password, "123456");
    QTest::keyClick(fixture.password, Qt::Key_Return);
    QTRY_VERIFY(!fixture.page->isVisible());
    QVERIFY(fixture.error->text().isEmpty());
    QVERIFY(!fixture.error->property("hasError").toBool());
}

void AdminUiTests::loginSurfaceSurvivesPartialRepaints_data()
{
    QTest::addColumn<bool>("submitWrongPassword");
    QTest::newRow("password-focus") << false;
    QTest::newRow("wrong-password") << true;
}

void AdminUiTests::loginSurfaceSurvivesPartialRepaints()
{
    QFETCH(bool, submitWrongPassword);
    LoginFixture fixture;
    fixture.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&fixture.window));
    QApplication::setActiveWindow(&fixture.window);
    if (screenImage(fixture.window, QStringLiteral("initial")).isNull()) {
        QSKIP("This platform cannot capture presented pixels; run under X11/Xvfb with QT_QPA_PLATFORM=xcb.");
    }

    QTest::mouseClick(fixture.username, Qt::LeftButton);
    QTest::keyClicks(fixture.username, "admin");
    QTest::mouseClick(fixture.password, Qt::LeftButton);
    QTRY_VERIFY(fixture.password->hasFocus());
    if (submitWrongPassword) {
        QTest::keyClicks(fixture.password, "wrong-password");
        QTest::mouseClick(fixture.submit, Qt::LeftButton);
        QCOMPARE(fixture.error->text(), QStringLiteral("账号或密码错误，请重试"));
    }
    const QString state = QString::fromLatin1(QTest::currentDataTag());
    const QColor errorColor(submitWrongPassword ? "#fff3f1" : "#edf5fb");
    QImage image = screenImage(fixture.window, state);
    QString mismatch = unexpectedSurfacePixels(fixture, image, errorColor);
    QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));

    // Repeated focus/hover updates and resizing must not uncover either row.
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(fixture.username, Qt::LeftButton);
        QTest::mouseMove(fixture.submit);
        QTest::mouseClick(fixture.password, Qt::LeftButton);
        fixture.window.resize(i % 2 == 0 ? QSize(1080, 700) : QSize(1500, 950));
        image = screenImage(fixture.window, QStringLiteral("%1-resize-%2").arg(state).arg(i));
        mismatch = unexpectedSurfacePixels(fixture, image, errorColor);
        QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
    }
}

QTEST_MAIN(AdminUiTests)
#include "admin_ui_tests.moc"
