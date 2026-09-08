#pragma once

#include <QImage>
#include <QPixmap>
#include <QString>

#include <array>

namespace charging::client {

struct BasicAvatar {
    QString name;
    QImage image;
};

// Code-drawn artwork, sharing the client's palette without image plugins.
const std::array<BasicAvatar, 4> &basicAvatars();
QImage defaultAvatar();
QPixmap circularAvatar(const QImage &image, int size, qreal devicePixelRatio);

}  // namespace charging::client
