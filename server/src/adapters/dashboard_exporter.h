// 看板导出器声明：只负责序列化，不查询数据库
#pragma once

#include <QJsonObject>
#include <QString>

namespace charging::server {

// Serializes a service-produced dashboard snapshot. It does not query SQLite
// directly; ApplicationService remains responsible for assembling the data.
class DashboardExporter final {
public:
    // 输入服务层给的快照对象和输出路径，返回是否成功
    [[nodiscard]] bool exportSnapshot(const QString &path,
                                      const QJsonObject &snapshot,
                                      QString *error = nullptr) const;
};

}  // namespace charging::server
