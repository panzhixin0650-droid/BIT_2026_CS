// 头像绘制工具声明，提供基础头像与圆形裁剪
#pragma once

#include <QImage>
#include <QPixmap>
#include <QString>

#include <array>

namespace charging::client {

// 基础头像项：显示名称加对应图像
struct BasicAvatar {
    QString name;
    QImage image;
};

// Code-drawn artwork, sharing the client's palette without image plugins.
const std::array<BasicAvatar, 4> &basicAvatars();
QImage defaultAvatar();
// 把任意图像裁成指定尺寸的圆形头像
QPixmap circularAvatar(const QImage &image, int size, qreal devicePixelRatio);

}  // namespace charging::client
