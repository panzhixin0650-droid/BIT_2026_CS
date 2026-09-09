// 本文件在 Linux 下修正 Qt 输入法插件的环境变量
#include "local/input_method_setup.h"

#include <QDir>
#include <QLibraryInfo>

namespace charging::client {

namespace {

// 检查插件目录里是否存在指定名字的输入法插件
bool hasInputContextPlugin(const QString &qtPluginsPath, const QString &namePart)
{
    const QDir inputContexts(QDir(qtPluginsPath).filePath(
        QStringLiteral("platforminputcontexts")));
    const QStringList pluginFiles = inputContexts.entryList(QDir::Files);
    for (const QString &fileName : pluginFiles) {
        if (fileName.contains(namePart, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

}  // namespace

// 仅当环境设为 fcitx 时才继续判断插件是否可用
bool configureInputMethodForQt(const QString &qtPluginsPath)
{
#ifdef Q_OS_LINUX
    const QByteArray configuredInputMethod = qgetenv("QT_IM_MODULE").trimmed();
    if (configuredInputMethod.compare("fcitx", Qt::CaseInsensitive) != 0
        && configuredInputMethod.compare("fcitx5", Qt::CaseInsensitive) != 0) {
        return false;
    }

    // 未传路径则使用 Qt 自带插件目录
    const QString pluginsPath = qtPluginsPath.isEmpty()
        ? QLibraryInfo::path(QLibraryInfo::PluginsPath)
        : qtPluginsPath;
    if (hasInputContextPlugin(pluginsPath, QStringLiteral("fcitx"))
        || !hasInputContextPlugin(pluginsPath, QStringLiteral("ibus"))) {
        return false;
    }

    return qputenv("QT_IM_MODULE", QByteArrayLiteral("ibus"));
// 非 Linux 平台不做任何改动，直接返回 false
#else
    Q_UNUSED(qtPluginsPath)
    return false;
#endif
}

}  // namespace charging::client
