#include "ui/busy_indicator.h"

#include <QPainter>
#include <QPen>

namespace charging::client {

BusyIndicator::BusyIndicator(QWidget *parent) : QWidget(parent)
{
    setFixedSize(20, 20);
    setAccessibleName(QStringLiteral("正在等待回复，可以停止"));
    animation_.setInterval(80);
    connect(&animation_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + 30) % 360;
        update();
    });
    hide();
}

void BusyIndicator::setRunning(bool running)
{
    if (running && !animation_.isActive()) {
        elapsed_.start();
        animation_.start();
    } else if (!running) {
        animation_.stop();
        elapsed_.invalidate();
    }
    setVisible(running);
}

void BusyIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF ring = QRectF(rect()).adjusted(3, 3, -3, -3);
    painter.setPen(QPen(QColor(QStringLiteral("#dce5d8")), 2.5));
    painter.drawEllipse(ring);
    painter.setPen(QPen(QColor(QStringLiteral("#245c45")), 2.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, -angle_ * 16, 110 * 16);
}

}  // namespace charging::client
