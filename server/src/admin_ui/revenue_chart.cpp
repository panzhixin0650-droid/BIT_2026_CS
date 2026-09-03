#include "revenue_chart.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <utility>

namespace charging::server {

RevenueChart::RevenueChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(250);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

}  // namespace charging::server
