// 管理端时间格式化：ISO 时间转北京时间展示
#pragma once

#include <QDateTime>
#include <QString>
#include <QTimeZone>

namespace charging::server {

// Transport/storage keep ISO 8601; operator-facing timestamps use Beijing time.
// 解析失败返回占位符，避免界面显示空白
inline QString adminTimeText(const QString &timestamp,
                             const QString &fallback = QStringLiteral("—"))
{
    const auto instant = QDateTime::fromString(timestamp, Qt::ISODate);
    if (!instant.isValid()) return fallback;
    return instant.toTimeZone(QTimeZone("Asia/Shanghai"))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace charging::server
