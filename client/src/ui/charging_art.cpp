#include "ui/charging_art.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace charging::client {
namespace {

void drawStation(QPainter &p)
{
    // A small architectural scene, drawn in a 360 x 170 coordinate space.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#264c42"));
    p.drawEllipse(QRectF(38, 125, 304, 32));
    p.setPen(QPen(QColor("#507060"), 1));
    p.drawLine(QPointF(8, 148), QPointF(348, 148));
    p.drawLine(QPointF(40, 162), QPointF(308, 162));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#487461"));
    p.drawRoundedRect(QRectF(260, 31, 6, 92), 2, 2);
    p.drawRoundedRect(QRectF(67, 31, 5, 79), 2, 2);
    QPainterPath roof;
    roof.moveTo(42, 32); roof.lineTo(102, 9); roof.lineTo(299, 9);
    roof.lineTo(276, 32); roof.closeSubpath();
    p.setBrush(QColor("#74917b")); p.drawPath(roof);
    p.setPen(QPen(QColor("#b3d28a"), 2));
    p.drawLine(QPointF(43, 33), QPointF(276, 33));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#d9e5d2"));
    p.drawRoundedRect(QRectF(274, 51, 35, 79), 7, 7);
    p.setBrush(QColor("#122f29"));
    p.drawRoundedRect(QRectF(280, 61, 23, 30), 4, 4);
    p.setPen(QPen(QColor("#c3e88d"), 2.5, Qt::SolidLine, Qt::RoundCap));
    p.drawPolyline(QPolygonF{QPointF(294, 66), QPointF(287, 76),
                            QPointF(295, 76), QPointF(289, 85)});
    p.setPen(QPen(QColor("#96ae90"), 3, Qt::SolidLine, Qt::RoundCap));
    QPainterPath cable;
    cable.moveTo(308, 72); cable.cubicTo(336, 67, 327, 125, 299, 117);
    cable.cubicTo(280, 112, 278, 104, 260, 107);
    p.drawPath(cable);

    QPainterPath body;
    body.moveTo(42, 111); body.quadTo(41, 96, 61, 91);
    body.lineTo(90, 85); body.lineTo(117, 60);
    body.quadTo(121, 55, 135, 55); body.lineTo(186, 55);
    body.quadTo(201, 56, 218, 82); body.lineTo(246, 91);
    body.quadTo(259, 94, 259, 111); body.lineTo(252, 127);
    body.lineTo(48, 127); body.closeSubpath();
    QLinearGradient car(0, 60, 0, 132);
    car.setColorAt(0, QColor("#fcfbeb")); car.setColorAt(1, QColor("#bbcdb8"));
    p.setPen(Qt::NoPen); p.setBrush(car); p.drawPath(body);
    QPainterPath windows;
    windows.moveTo(102, 85); windows.lineTo(123, 63);
    windows.lineTo(184, 63); windows.quadTo(193, 64, 207, 85);
    windows.closeSubpath();
    p.setBrush(QColor("#234b43")); p.drawPath(windows);
    p.setPen(QPen(QColor("#d8e2cb"), 3));
    p.drawLine(QPointF(156, 62), QPointF(156, 86));
    p.setPen(QPen(QColor("#95ab95"), 1));
    p.drawLine(QPointF(155, 90), QPointF(155, 117));
    p.drawLine(QPointF(111, 97), QPointF(124, 97));
    p.drawLine(QPointF(171, 97), QPointF(184, 97));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#edfaac")); p.drawRoundedRect(QRectF(44, 99, 21, 6), 2, 2);
    p.setBrush(QColor("#7f9b76")); p.drawRoundedRect(QRectF(242, 98, 13, 5), 2, 2);
    for (qreal x : {88.0, 214.0}) {
        p.setBrush(QColor("#112e29")); p.drawEllipse(QPointF(x, 125), 18, 18);
        p.setBrush(QColor("#9eb3a2")); p.drawEllipse(QPointF(x, 125), 10, 10);
        p.setBrush(QColor("#d3dfcb")); p.drawEllipse(QPointF(x, 125), 4, 4);
    }
}

}  // namespace

ChargingArt::ChargingArt(Scene scene, QWidget *parent)
    : QWidget(parent), scene_(scene)
{
    setObjectName(QStringLiteral("chargingArtwork"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(scene == Scene::Journey ? 194 : scene == Scene::Scan ? 166 : 186);
    setAccessibleName(scene == Scene::Scan ? QStringLiteral("充电桩编号输入示意")
                                          : QStringLiteral("电动汽车在绿色充电站补能的插画"));
}

void ChargingArt::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 22, 22);
    p.setClipPath(clip);
    QLinearGradient background(0, height(), width(), 0);
    background.setColorAt(0, QColor("#133f35"));
    background.setColorAt(1, QColor("#254f40"));
    p.fillRect(rect(), background);
    p.setPen(QPen(QColor("#3a6150"), 1));
    p.setBrush(Qt::NoBrush);
    for (int radius : {95, 135, 180}) {
        p.drawEllipse(QPointF(width() - 30, 4), radius, radius);
    }

    if (scene_ == Scene::Scan) {
        const qreal x = width() / 2.0 - 45;
        p.setPen(QPen(QColor("#c8e99a"), 3, Qt::SolidLine, Qt::RoundCap));
        for (int corner = 0; corner < 4; ++corner) {
            p.save();
            p.translate(x + (corner % 2) * 90, 22 + (corner / 2) * 90);
            p.scale(corner % 2 ? -1 : 1, corner / 2 ? -1 : 1);
            p.drawPolyline(QPolygonF{QPointF(0, 20), QPointF(0, 0), QPointF(20, 0)});
            p.restore();
        }
        QPainterPath bolt;
        bolt.moveTo(x + 51, 37); bolt.lineTo(x + 27, 70);
        bolt.lineTo(x + 44, 70); bolt.lineTo(x + 37, 97);
        bolt.lineTo(x + 64, 62); bolt.lineTo(x + 48, 62); bolt.closeSubpath();
        p.setPen(Qt::NoPen); p.setBrush(QColor("#c8e99a")); p.drawPath(bolt);
        p.setPen(QColor("#d4e3d1"));
        QFont caption = font(); caption.setPixelSize(12); p.setFont(caption);
        p.drawText(QRect(0, 128, width(), 22), Qt::AlignCenter,
                   QStringLiteral("到达电桩后，输入编号开始充电"));
        return;
    }

    if (scene_ == Scene::Journey) {
        QFont title = font(); title.setPixelSize(27); title.setBold(true); p.setFont(title);
        p.setPen(QColor("#fbfbed"));
        p.drawText(QRect(22, 22, width() - 44, 72), Qt::AlignLeft,
                   QStringLiteral("为下一程\n蓄满能量。"));
        QFont caption = font(); caption.setPixelSize(10); caption.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(caption); p.setPen(QColor("#c8e99a"));
        p.drawText(23, 122, QStringLiteral("CHARGE & GO"));
        p.save();
        const qreal scale = qMin((width() - 95) / 360.0, 0.86);
        p.translate(width() - 350 * scale, height() - 163 * scale);
        p.scale(scale, scale); drawStation(p); p.restore();
    } else {
        p.save();
        const qreal scale = qMin((width() - 32) / 360.0, 1.0);
        p.translate((width() - 360 * scale) / 2, height() - 168 * scale - 10);
        p.scale(scale, scale); drawStation(p); p.restore();
    }
}

}  // namespace charging::client
