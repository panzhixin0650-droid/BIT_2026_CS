// 本文件声明本地头像存储类
#pragma once

#include <QString>
#include <QtGlobal>

#include <memory>

class QSettings;
class QImage;

namespace charging::client {

// AvatarStorage 管理头像文件与配置记录，禁止拷贝
class AvatarStorage final {
public:
    explicit AvatarStorage(const QString &applicationDataDirectory = {},
                           const QString &settingsFile = {});
    ~AvatarStorage();

    AvatarStorage(const AvatarStorage &) = delete;
    AvatarStorage &operator=(const AvatarStorage &) = delete;

    // 提供查询路径、按文件保存、按图片保存三个入口
    [[nodiscard]] QString avatarPath(const QString &userKey) const;
    [[nodiscard]] bool saveAvatar(const QString &userKey,
                                  const QString &sourcePath,
                                  QString *savedPath = nullptr,
                                  QString *error = nullptr);
    [[nodiscard]] bool saveImage(const QString &userKey,
                                 const QImage &image,
                                 QString *savedPath = nullptr,
                                 QString *error = nullptr);

private:
    [[nodiscard]] QString keyHash(const QString &userKey) const;
    [[nodiscard]] QString settingsKey(const QString &userKey) const;

    // 目录与配置对象由构造注入，方便测试替换
    QString applicationDataDirectory_;
    std::unique_ptr<QSettings> settings_;
};

}  // namespace charging::client
