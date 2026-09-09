#include "common/charging_progress_ring.h"

#include <QPainter>

namespace charging::client {

ChargingProgressRing::ChargingProgressRing(QWidget *parent, const QString &objectName)
    : QWidget(parent)
{
    setObjectName(objectName);
    setAccessibleName(QStringLiteral("Demo 会话充电进度"));
    setMinimumSize(220, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ChargingProgressRing::setProgress(int percent, const QString &caption)
{
    percent_ = qBound(0, percent, 100);
    caption_ = caption;
    update();
}

void ChargingProgressRing::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin<qreal>(280, qMin(width() - 36, height() - 28));
    const QRectF arc((width() - side) / 2, (height() - side) / 2, side, side);

    painter.setPen(QPen(QColor(QStringLiteral("#eef2e8")), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(arc.adjusted(-12, -12, 12, 12));
    painter.setPen(QPen(QColor(QStringLiteral("#e7eddf")), 13,
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(arc);
    painter.setPen(QPen(QColor(QStringLiteral("#567b52")), 13,
                        Qt::SolidLine, Qt::RoundCap));
    if (percent_ > 0)
        painter.drawArc(arc, 90 * 16, -qRound(percent_ * 3.6 * 16));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#f5f8f0")));
    painter.drawEllipse(arc.adjusted(15, 15, -15, -15));

    QFont textFont = font();
    textFont.setPixelSize(11);
    painter.setFont(textFont);
    painter.setPen(QColor(QStringLiteral("#71826c")));
    painter.drawText(QRectF(arc.left(), arc.center().y() - 58, side, 24),
                     Qt::AlignCenter, QStringLiteral("本次充电进度"));
    textFont.setPixelSize(qRound(side * .22));
    textFont.setBold(true);
    painter.setFont(textFont);
    painter.setPen(QColor(QStringLiteral("#245c45")));
    painter.drawText(arc.adjusted(0, -4, 0, -4), Qt::AlignCenter,
                     QString::number(percent_) + QStringLiteral("%"));
    textFont.setPixelSize(11);
    textFont.setBold(false);
    painter.setFont(textFont);
    painter.setPen(QColor(QStringLiteral("#65796c")));
    painter.drawText(QRectF(arc.left(), arc.center().y() + 34, side, 26),
                     Qt::AlignCenter, caption_);
}

}  // namespace charging::client
