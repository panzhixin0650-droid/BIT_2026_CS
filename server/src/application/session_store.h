// 本文件声明会话存储接口，令牌只保存在进程内存
#pragma once

#include <QHash>
#include <QString>

#include <optional>

namespace charging::server {

// V1 sessions are deliberately process-local and expire when server-app exits.
// 提供创建、查询与注销令牌三个操作
class SessionStore final {
public:
    [[nodiscard]] QString create(qint64 userId);
    [[nodiscard]] std::optional<qint64> userIdForToken(const QString &token) const;
    [[nodiscard]] bool remove(const QString &token);

private:
    // 哈希表保存令牌到用户ID的映射
    QHash<QString, qint64> sessions_;
};

}  // namespace charging::server
