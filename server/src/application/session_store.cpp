// 本文件实现内存会话表，管理令牌与用户ID的映射
#include "session_store.h"

#include <QUuid>

namespace charging::server {

// 登录成功后生成UUID令牌并记录归属用户
QString SessionStore::create(qint64 userId)
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sessions_.insert(token, userId);
    return token;
}

// 按令牌查用户ID，查不到返回空
std::optional<qint64> SessionStore::userIdForToken(const QString &token) const
{
    const auto found = sessions_.constFind(token);
    return found == sessions_.cend() ? std::nullopt
                                     : std::optional<qint64>(*found);
}

bool SessionStore::remove(const QString &token)
{
    return sessions_.remove(token) > 0;
}

}  // namespace charging::server
