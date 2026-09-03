#pragma once

#include <QJsonObject>
#include <QString>

namespace charging::server {

// Serializes a service-produced dashboard snapshot. It does not query SQLite
// directly; ApplicationService remains responsible for assembling the data.
class DashboardExporter final {
public:
    [[nodiscard]] bool exportSnapshot(const QString &path,
                                      const QJsonObject &snapshot,
                                      QString *error = nullptr) const;
};

}  // namespace charging::server
