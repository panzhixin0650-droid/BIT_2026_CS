#pragma once

#include <QString>
#include <QtGlobal>

#include <memory>

class QSettings;

namespace charging::client {

class AvatarStorage final {
public:
    explicit AvatarStorage(const QString &applicationDataDirectory = {},
                           const QString &settingsFile = {});
    ~AvatarStorage();

    AvatarStorage(const AvatarStorage &) = delete;
    AvatarStorage &operator=(const AvatarStorage &) = delete;

    [[nodiscard]] QString avatarPath(const QString &userKey) const;
    [[nodiscard]] bool saveAvatar(const QString &userKey,
                                  const QString &sourcePath,
                                  QString *savedPath = nullptr,
                                  QString *error = nullptr);

private:
    [[nodiscard]] QString keyHash(const QString &userKey) const;
    [[nodiscard]] QString settingsKey(const QString &userKey) const;

    QString applicationDataDirectory_;
    std::unique_ptr<QSettings> settings_;
};

}  // namespace charging::client
