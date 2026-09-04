#include "local/input_method_setup.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace charging::client {

namespace {

void createPluginFile(const QString &pluginsPath, const QString &fileName)
{
    QDir root(pluginsPath);
    QVERIFY(root.mkpath(QStringLiteral("platforminputcontexts")));
    QFile plugin(root.filePath(QStringLiteral("platforminputcontexts/%1").arg(fileName)));
    QVERIFY(plugin.open(QIODevice::WriteOnly));
}

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(const char *name)
        : name_(name), wasSet_(qEnvironmentVariableIsSet(name)), value_(qgetenv(name))
    {
    }

    ~EnvironmentGuard()
    {
        if (wasSet_) {
            qputenv(name_, value_);
        } else {
            qunsetenv(name_);
        }
    }

private:
    const char *name_;
    bool wasSet_;
    QByteArray value_;
};

}  // namespace

class InputMethodSetupTests final : public QObject {
    Q_OBJECT

private slots:
    void fallsBackToIbusWhenFcitxPluginIsMissing()
    {
        EnvironmentGuard guard("QT_IM_MODULE");
        QTemporaryDir plugins;
        QVERIFY(plugins.isValid());
        createPluginFile(plugins.path(), QStringLiteral("libibusplatforminputcontextplugin.so"));
        QVERIFY(qputenv("QT_IM_MODULE", QByteArrayLiteral("fcitx")));

#ifdef Q_OS_LINUX
        QVERIFY(configureInputMethodForQt(plugins.path()));
        QCOMPARE(qgetenv("QT_IM_MODULE"), QByteArrayLiteral("ibus"));
#else
        QVERIFY(!configureInputMethodForQt(plugins.path()));
        QCOMPARE(qgetenv("QT_IM_MODULE"), QByteArrayLiteral("fcitx"));
#endif
    }

    void preservesFcitxWhenItsQtPluginExists()
    {
        EnvironmentGuard guard("QT_IM_MODULE");
        QTemporaryDir plugins;
        QVERIFY(plugins.isValid());
        createPluginFile(plugins.path(), QStringLiteral("libfcitx5platforminputcontextplugin.so"));
        createPluginFile(plugins.path(), QStringLiteral("libibusplatforminputcontextplugin.so"));
        QVERIFY(qputenv("QT_IM_MODULE", QByteArrayLiteral("fcitx")));

        QVERIFY(!configureInputMethodForQt(plugins.path()));
        QCOMPARE(qgetenv("QT_IM_MODULE"), QByteArrayLiteral("fcitx"));
    }

    void preservesAnExplicitNonFcitxChoice()
    {
        EnvironmentGuard guard("QT_IM_MODULE");
        QTemporaryDir plugins;
        QVERIFY(plugins.isValid());
        createPluginFile(plugins.path(), QStringLiteral("libibusplatforminputcontextplugin.so"));
        QVERIFY(qputenv("QT_IM_MODULE", QByteArrayLiteral("xim")));

        QVERIFY(!configureInputMethodForQt(plugins.path()));
        QCOMPARE(qgetenv("QT_IM_MODULE"), QByteArrayLiteral("xim"));
    }

    void keepsFcitxWhenNoCompatibleFallbackExists()
    {
        EnvironmentGuard guard("QT_IM_MODULE");
        QTemporaryDir plugins;
        QVERIFY(plugins.isValid());
        QVERIFY(qputenv("QT_IM_MODULE", QByteArrayLiteral("fcitx5")));

        QVERIFY(!configureInputMethodForQt(plugins.path()));
        QCOMPARE(qgetenv("QT_IM_MODULE"), QByteArrayLiteral("fcitx5"));
    }
};

}  // namespace charging::client

QTEST_APPLESS_MAIN(charging::client::InputMethodSetupTests)

#include "input_method_setup_tests.moc"
