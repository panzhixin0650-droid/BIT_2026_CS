#include "local/avatar_storage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace charging::client {

AvatarStorage::AvatarStorage(const QString &applicationDataDirectory,
                             const QString &settingsFile)
    : applicationDataDirectory_(applicationDataDirectory.isEmpty()
                                    ? QStandardPaths::writableLocation(
                                          QStandardPaths::AppDataLocation)
                                    : applicationDataDirectory)
    , settings_(settingsFile.isEmpty()
                    ? std::make_unique<QSettings>()
                    : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat))
{
}

AvatarStorage::~AvatarStorage() = default;

QString AvatarStorage::avatarPath(const QString &userKey) const
{
    if (userKey.isEmpty()) {
        return {};
    }

    const QString relativePath = settings_->value(settingsKey(userKey)).toString();
    if (relativePath.isEmpty()) {
        return {};
    }

    const QString absolutePath = QDir(applicationDataDirectory_).filePath(relativePath);
    return QFileInfo::exists(absolutePath) ? absolutePath : QString{};
}

bool AvatarStorage::saveAvatar(const QString &userKey,
                               const QString &sourcePath,
                               QString *savedPath,
                               QString *error)
{
    if (userKey.isEmpty() || sourcePath.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("头像文件无效");
        }
        return false;
    }

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("无法读取所选图片");
        }
        return false;
    }

    if (image.width() > 512 || image.height() > 512) {
        image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    const QString relativePath =
        QStringLiteral("avatars/user-%1.png").arg(keyHash(userKey));
    const QString absolutePath = QDir(applicationDataDirectory_).filePath(relativePath);
    if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("无法创建头像保存目录");
        }
        return false;
    }

    QSaveFile destination(absolutePath);
    if (!destination.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("无法保存头像文件");
        }
        return false;
    }

    QImageWriter writer(&destination, "png");
    if (!writer.write(image) || !destination.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("头像保存失败");
        }
        return false;
    }

    settings_->setValue(settingsKey(userKey), relativePath);
    settings_->sync();
    if (settings_->status() != QSettings::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("头像配置保存失败");
        }
        return false;
    }

    if (savedPath != nullptr) {
        *savedPath = absolutePath;
    }
    return true;
}

QString AvatarStorage::keyHash(const QString &userKey) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(userKey.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString AvatarStorage::settingsKey(const QString &userKey) const
{
    return QStringLiteral("avatars/%1").arg(keyHash(userKey));
}

}  // namespace charging::client
