// 小型转圈等待控件，提示请求正在进行中
#include "ui/busy_indicator.h"

#include <QPainter>
#include <QPen>

namespace charging::client {

BusyIndicator::BusyIndicator(QWidget *parent) : QWidget(parent)
{
    // 固定20像素尺寸并设置无障碍名称，默认隐藏
    setFixedSize(20, 20);
    setAccessibleName(QStringLiteral("正在等待回复，可以停止"));
    // 每80毫秒转30度并重绘，形成旋转动画
    animation_.setInterval(80);
    connect(&animation_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + 30) % 360;
        update();
    });
    hide();
}

// 按运行状态启停定时器，同时控制自身显示隐藏
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

// 先画浅色底环，再画旋转的深色圆弧表示等待
void BusyIndicator::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF ring = QRectF(rect()).adjusted(3, 3, -3, -3);
    painter.setPen(QPen(QColor(QStringLiteral("#dce5d8")), 2.5));
    painter.drawEllipse(ring);
    painter.setPen(QPen(QColor(QStringLiteral("#245c45")), 2.5, Qt::SolidLine, Qt::RoundCap));
    // 角度乘16是Qt画弧的十六分之一度单位要求
    painter.drawArc(ring, -angle_ * 16, 110 * 16);
}

}  // namespace charging::client
