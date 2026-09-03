#include "ui/main_window.h"

#include <QLabel>
#include <QtTest>

using charging::client::MainWindow;

class MainWindowTests : public QObject {
    Q_OBJECT

private slots:
    void constructsCodeOnlyApplicationShell();
};

void MainWindowTests::constructsCodeOnlyApplicationShell()
{
    MainWindow window;

    QCOMPARE(window.objectName(), QStringLiteral("mainWindow"));
    QCOMPARE(window.windowTitle(), QStringLiteral("新能源汽车充电服务"));

    auto *placeholder = window.findChild<QLabel *>(QStringLiteral("appPlaceholder"));
    QVERIFY(placeholder != nullptr);
    QCOMPARE(placeholder->text(), QStringLiteral("用户端正在建设中"));
}

QTEST_MAIN(MainWindowTests)

#include "main_window_tests.moc"
