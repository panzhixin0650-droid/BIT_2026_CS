#include "revenue_chart.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <utility>

namespace charging::server {

RevenueChart::RevenueChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(250);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void RevenueChart::setPoints(QList<RevenuePoint> points)
{
    points_ = std::move(points);
    update();
}

const QList<RevenuePoint> &RevenueChart::points() const noexcept
{
    return points_;
}

void RevenueChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    const QRectF plot = QRectF(rect()).adjusted(54.0, 24.0, -24.0, -42.0);
    painter.setPen(QPen(QColor(QStringLiteral("#e5eaf2")), 1.0));
    for (int line = 0; line <= 4; ++line) {
        const qreal y = plot.top() + plot.height() * line / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    if (points_.isEmpty()) {
        painter.setPen(QColor(QStringLiteral("#8793a7")));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("暂无营收数据"));
        return;
    }

    qint64 maximum = 100;
    for (const RevenuePoint &point : points_) {
        maximum = std::max(maximum, point.revenueCents);
    }

    QPainterPath linePath;
    QPainterPath fillPath;
    QList<QPointF> screenPoints;
    for (qsizetype index = 0; index < points_.size(); ++index) {
        const qreal x = points_.size() == 1
            ? plot.center().x()
            : plot.left() + plot.width() * index / (points_.size() - 1.0);
        const qreal y = plot.bottom()
            - plot.height() * points_.at(index).revenueCents / maximum;
        screenPoints.append(QPointF(x, y));
        if (index == 0) {
            linePath.moveTo(x, y);
            fillPath.moveTo(x, plot.bottom());
            fillPath.lineTo(x, y);
        } else {
            linePath.lineTo(x, y);
            fillPath.lineTo(x, y);
        }
    }
    fillPath.lineTo(screenPoints.constLast().x(), plot.bottom());
    fillPath.closeSubpath();

    QLinearGradient gradient(plot.topLeft(), plot.bottomLeft());
    gradient.setColorAt(0.0, QColor(47, 111, 237, 75));
    gradient.setColorAt(1.0, QColor(47, 111, 237, 5));
    painter.fillPath(fillPath, gradient);
    painter.setPen(QPen(QColor(QStringLiteral("#2f6fed")), 2.5));
    painter.drawPath(linePath);
    painter.setBrush(QColor(QStringLiteral("#2f6fed")));
    painter.setPen(Qt::white);
    for (const QPointF &point : screenPoints) {
        painter.drawEllipse(point, 3.5, 3.5);
    }

    if (hoveredIndex_ >= 0 && hoveredIndex_ < screenPoints.size()) {
        const QPointF highlight = screenPoints.at(hoveredIndex_);
        painter.setPen(QPen(QColor(47, 111, 237, 90), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(highlight.x(), plot.top()),
                         QPointF(highlight.x(), plot.bottom()));
        painter.setBrush(Qt::white);
        painter.setPen(QPen(QColor(QStringLiteral("#2f6fed")), 2.5));
        painter.drawEllipse(highlight, 5.5, 5.5);
    }

    painter.setPen(QColor(QStringLiteral("#8793a7")));
    const int labelStep = std::max(1, static_cast<int>(points_.size() / 6));
    for (qsizetype index = 0; index < points_.size(); index += labelStep) {
        const qreal x = screenPoints.at(index).x();
        painter.drawText(QRectF(x - 36.0, plot.bottom() + 10.0, 72.0, 20.0),
                         Qt::AlignHCenter, points_.at(index).date.mid(5));
    }
    painter.drawText(QRectF(0.0, plot.top() - 8.0, 48.0, 20.0),
                     Qt::AlignRight, QString::number(maximum / 100.0, 'f', 0));
    painter.drawText(QRectF(0.0, plot.bottom() - 10.0, 48.0, 20.0),
                     Qt::AlignRight, QStringLiteral("0"));
}

void RevenueChart::mouseMoveEvent(QMouseEvent *event)
{
    if (points_.isEmpty()) {
        QToolTip::hideText();
        return;
    }
    const QRectF plot = QRectF(rect()).adjusted(54.0, 24.0, -24.0, -42.0);
    const QPointF position = event->position();
    if (!plot.contains(position)) {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            update();
        }
        QToolTip::hideText();
        return;
    }

    const qreal normalized = points_.size() == 1
        ? 0.0
        : (position.x() - plot.left()) / plot.width() * (points_.size() - 1.0);
    const int index = qBound(0, qRound(normalized), points_.size() - 1);
    if (hoveredIndex_ != index) {
        hoveredIndex_ = index;
        update();
    }
    const RevenuePoint &point = points_.at(index);
    QToolTip::showText(mapToGlobal(event->position().toPoint()),
                       QStringLiteral("%1\n营收：¥%2")
                           .arg(point.date)
                           .arg(point.revenueCents / 100.0, 0, 'f', 2),
                       this);
}

void RevenueChart::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    hoveredIndex_ = -1;
    QToolTip::hideText();
    update();
}

}  // namespace charging::server
