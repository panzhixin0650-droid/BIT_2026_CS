#pragma once

#include <QHash>
#include <QString>

#include <optional>

namespace charging::server {

// V1 sessions are deliberately process-local and expire when server-app exits.
class SessionStore final {
public:
    [[nodiscard]] QString create(qint64 userId);
    [[nodiscard]] std::optional<qint64> userIdForToken(const QString &token) const;
    [[nodiscard]] bool remove(const QString &token);

private:
    QHash<QString, qint64> sessions_;
};

}  // namespace charging::server
