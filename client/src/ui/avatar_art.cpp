// 用代码绘制头像图片，不依赖外部图片资源
#include "ui/avatar_art.h"

#include <QPainter>
#include <QPainterPath>

namespace charging::client {
namespace {

// 按变体号画出四种风格的头像底图
QImage drawAvatar(int variant)
{
    QImage image(512, 512, QImage::Format_ARGB32_Premultiplied);
    const char *backgrounds[] = {"#dce9cd", "#f1e4cf", "#dae8e2", "#e8e3ef"};
    image.fill(QColor(backgrounds[variant]));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(4, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffffff"));
    p.setOpacity(0.35);
    p.drawEllipse(QRectF(17, 14, 94, 94));
    p.setOpacity(1);

    // Shoulders and face give all four characters a common silhouette.
    p.setBrush(QColor(variant == 1 ? "#b78353" : "#527b64"));
    p.drawEllipse(QRectF(24, 89, 80, 60));
    const QColor face(variant == 2 ? "#c29b75" : "#fff5e3");
    p.setBrush(face);
    // 不同变体画耳朵或发饰，形成各自造型
    if (variant == 1) {
        p.drawPolygon(QPolygonF{QPointF(29, 54), QPointF(27, 22), QPointF(53, 40)});
        p.drawPolygon(QPolygonF{QPointF(75, 40), QPointF(101, 22), QPointF(99, 54)});
    } else if (variant == 2) {
        p.drawEllipse(QRectF(22, 26, 29, 30));
        p.drawEllipse(QRectF(77, 26, 29, 30));
    } else if (variant == 3) {
        p.drawRoundedRect(QRectF(38, 10, 19, 53), 10, 10);
        p.drawRoundedRect(QRectF(71, 10, 19, 53), 10, 10);
        p.setBrush(QColor("#dfbfc3"));
        p.drawRoundedRect(QRectF(44, 17, 7, 29), 4, 4);
        p.drawRoundedRect(QRectF(77, 17, 7, 29), 4, 4);
        p.setBrush(face);
    }
    p.drawRoundedRect(QRectF(28, 36, 72, 67), 31, 31);
    // 变体 0 额外画一株嫩芽装饰
    if (variant == 0) {
        p.setPen(QPen(QColor("#527b64"), 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(64, 38), QPointF(64, 23));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#527b64"));
        QPainterPath leaves;
        leaves.moveTo(64, 28);
        leaves.cubicTo(42, 29, 40, 12, 42, 12);
        leaves.cubicTo(56, 10, 65, 17, 64, 28);
        leaves.moveTo(64, 24);
        leaves.cubicTo(63, 11, 78, 8, 87, 12);
        leaves.cubicTo(83, 25, 75, 28, 64, 24);
        p.drawPath(leaves);
    }
    // 绘制腮红、眼睛和微笑等五官
    p.setBrush(QColor("#dfb5a2"));
    p.drawEllipse(QRectF(35, 74, 14, 8));
    p.drawEllipse(QRectF(79, 74, 14, 8));
    p.setBrush(QColor("#203d33"));
    p.drawEllipse(QRectF(46, 61, 6, 8));
    p.drawEllipse(QRectF(76, 61, 6, 8));
    p.setPen(QPen(QColor("#203d33"), 2.5, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(56, 73, 16, 13), 200 * 16, 140 * 16);
    if (variant == 1) {
        p.drawLine(QPointF(24, 71), QPointF(37, 74));
        p.drawLine(QPointF(24, 81), QPointF(37, 79));
        p.drawLine(QPointF(91, 74), QPointF(104, 71));
        p.drawLine(QPointF(91, 79), QPointF(104, 81));
    }
    return image;
}

}  // namespace

// 四个基础头像只绘制一次并静态缓存
const std::array<BasicAvatar, 4> &basicAvatars()
{
    static const std::array<BasicAvatar, 4> avatars{{
        {QStringLiteral("小芽"), drawAvatar(0)},
        {QStringLiteral("小猫"), drawAvatar(1)},
        {QStringLiteral("小熊"), drawAvatar(2)},
        {QStringLiteral("小兔"), drawAvatar(3)},
    }};
    return avatars;
}

// 未设置头像时的灰色占位图
QImage defaultAvatar()
{
    QImage image(128, 128, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#e4e6e2"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#a4aaa4"));
    p.drawEllipse(QRectF(43, 25, 42, 42));
    p.drawEllipse(QRectF(24, 76, 80, 70));
    return image;
}

// 居中裁成圆形头像，并按设备像素比适配高分屏
QPixmap circularAvatar(const QImage &image, int size, qreal devicePixelRatio)
{
    const int pixels = qRound(size * devicePixelRatio);
    QPixmap result(pixels, pixels);
    result.setDevicePixelRatio(devicePixelRatio);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(clip);
    const int side = qMin(image.width(), image.height());
    // PNG decoding can change the alpha format; normalize it so preview and
    // restored avatars use the same interpolation and edge blending.
    p.drawImage(QRectF(0, 0, size, size),
                image.convertToFormat(QImage::Format_ARGB32_Premultiplied),
                QRectF((image.width() - side) / 2.0,
                       (image.height() - side) / 2.0, side, side));
    return result;
}

}  // namespace charging::client
