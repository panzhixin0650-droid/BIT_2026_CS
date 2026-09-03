#include "session_store.h"

#include <QUuid>

namespace charging::server {

QString SessionStore::create(qint64 userId)
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sessions_.insert(token, userId);
    return token;
}

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
