#include "dashboard_exporter.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace charging::server {

bool DashboardExporter::exportSnapshot(const QString &path,
                                       const QJsonObject &snapshot,
                                       QString *error) const
{
    if (path.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("dashboard output path is empty");
        }
        return false;
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }

    const QByteArray json = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
    if (output.write(json) != json.size() || !output.commit()) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace charging::server
