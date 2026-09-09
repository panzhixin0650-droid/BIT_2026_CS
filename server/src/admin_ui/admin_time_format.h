#pragma once

#include <QDateTime>
#include <QString>
#include <QTimeZone>

namespace charging::server {

// Transport/storage keep ISO 8601; operator-facing timestamps use Beijing time.
inline QString adminTimeText(const QString &timestamp,
                             const QString &fallback = QStringLiteral("—"))
{
    const auto instant = QDateTime::fromString(timestamp, Qt::ISODate);
    if (!instant.isValid()) return fallback;
    return instant.toTimeZone(QTimeZone("Asia/Shanghai"))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace charging::server
