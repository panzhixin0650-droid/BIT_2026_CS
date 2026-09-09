// 本文件把用户头像保存到本地目录并记录其路径
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

// 构造时确定数据目录与配置文件，未指定则用系统默认
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

// avatarPath 读配置里的相对路径，文件缺失则返回空
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

// saveAvatar 从文件读入图片并自动套用方向信息
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
    return saveImage(userKey, reader.read(), savedPath, error);
}

// saveImage 超过 512 像素时先等比缩放
bool AvatarStorage::saveImage(const QString &userKey,
                              const QImage &sourceImage,
                              QString *savedPath,
                              QString *error)
{
    if (userKey.isEmpty() || sourceImage.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("无法读取所选图片");
        }
        return false;
    }

    QImage image = sourceImage;
    if (image.width() > 512 || image.height() > 512) {
        image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // 用用户标识的哈希作文件名，不直接拼接手机号
    const QString relativePath =
        QStringLiteral("avatars/user-%1.png").arg(keyHash(userKey));
    const QString absolutePath = QDir(applicationDataDirectory_).filePath(relativePath);
    if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("无法创建头像保存目录");
        }
        return false;
    }

    // 用 QSaveFile 写 PNG，失败时不破坏原有头像
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

    // 写文件成功后再更新配置，并检查配置是否落盘
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

// keyHash 用 SHA-256 把用户键转成十六进制指纹
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
