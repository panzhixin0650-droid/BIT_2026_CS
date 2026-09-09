// 本文件测试 Qt 输入法环境变量的检测与回退逻辑
#include "local/input_method_setup.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace charging::client {

namespace {

// 在临时目录造一个假的输入法插件文件
void createPluginFile(const QString &pluginsPath, const QString &fileName)
{
    QDir root(pluginsPath);
    QVERIFY(root.mkpath(QStringLiteral("platforminputcontexts")));
    QFile plugin(root.filePath(QStringLiteral("platforminputcontexts/%1").arg(fileName)));
    QVERIFY(plugin.open(QIODevice::WriteOnly));
}

// EnvironmentGuard 析构时恢复原环境变量，避免影响其他用例
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
    // 缺少 fcitx 插件时回退到 ibus，非 Linux 不改动
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

    // 已存在 fcitx 的 Qt 插件则保持原设置
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

    // 用户显式选择的非 fcitx 输入法不被改写
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

    // 没有可用回退插件时保留原来的取值
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
