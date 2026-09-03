#include "local/input_method_setup.h"

#include <QDir>
#include <QLibraryInfo>

namespace charging::client {

namespace {

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

bool configureInputMethodForQt(const QString &qtPluginsPath)
{
#ifdef Q_OS_LINUX
    const QByteArray configuredInputMethod = qgetenv("QT_IM_MODULE").trimmed();
    if (configuredInputMethod.compare("fcitx", Qt::CaseInsensitive) != 0
        && configuredInputMethod.compare("fcitx5", Qt::CaseInsensitive) != 0) {
        return false;
    }

    const QString pluginsPath = qtPluginsPath.isEmpty()
        ? QLibraryInfo::path(QLibraryInfo::PluginsPath)
        : qtPluginsPath;
    if (hasInputContextPlugin(pluginsPath, QStringLiteral("fcitx"))
        || !hasInputContextPlugin(pluginsPath, QStringLiteral("ibus"))) {
        return false;
    }

    return qputenv("QT_IM_MODULE", QByteArrayLiteral("ibus"));
#else
    Q_UNUSED(qtPluginsPath)
    return false;
#endif
}

}  // namespace charging::client
