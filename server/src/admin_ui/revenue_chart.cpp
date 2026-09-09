#include "revenue_chart.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QKeyEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace charging::server {

RevenueChart::RevenueChart(QWidget *parent)
    : QWidget(parent)
{
    intro_ = new ChartIntro(this);
    setMinimumHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void RevenueChart::playIntro()
{
    if (!points_.isEmpty()) intro_->replay();
}


void RevenueChart::setPoints(QList<RevenuePoint> points)
{
    intro_->finish();
    setCursor(Qt::ArrowCursor);
    points_ = std::move(points);
    hoveredIndex_ = -1;
    setCursor(Qt::ArrowCursor);
    QToolTip::hideText();
    update();
}

void RevenueChart::setMetric(const QString &label, const QString &unit, qreal divisor, const QColor &color)
{
    label_ = label; unit_ = unit; divisor_ = qMax(1.0, divisor); color_ = color;
    setAccessibleName(label + QStringLiteral("趋势，单位") + unit); update();
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
    QFont textFont = font(); textFont.setPixelSize(12); painter.setFont(textFont);
    painter.fillRect(rect(), Qt::white);

    const QRectF plot = QRectF(rect()).adjusted(54.0, 24.0, -24.0, -42.0);
    painter.setPen(QPen(QColor(QStringLiteral("#e5eaf2")), 1.0));
    for (int line = 0; line <= 4; ++line) {
        const qreal y = plot.top() + plot.height() * line / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    if (points_.isEmpty()) {
        painter.setPen(QColor(QStringLiteral("#64748b")));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("当前范围暂无数据"));
        return;
    }

    qint64 maximum = static_cast<qint64>(divisor_);
    for (const RevenuePoint &point : points_) {
        maximum = std::max(maximum, point.revenueCents);
    }

    const double magnitude = std::pow(10.0, std::floor(std::log10(qMax(1.0, maximum / divisor_))));
    maximum = static_cast<qint64>(std::ceil(maximum / divisor_ / magnitude * 1.1) * magnitude * divisor_);
    maximum = qMax<qint64>(maximum, static_cast<qint64>(divisor_));
    if (divisor_ == 1) maximum = static_cast<qint64>(std::ceil(maximum / 4.0)) * 4;
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
    gradient.setColorAt(0.0, QColor(color_.red(), color_.green(), color_.blue(), 50));
    gradient.setColorAt(1.0, QColor(color_.red(), color_.green(), color_.blue(), 3));
    painter.save();
    painter.setClipRect(QRectF(plot.left()-6, plot.top()-6,
        (plot.width()+12)*intro_->progress(), plot.height()+12));
    painter.fillPath(fillPath, gradient);
    painter.setPen(QPen(color_, 2.5));
    painter.drawPath(linePath);
    painter.setBrush(color_);
    painter.setPen(Qt::white);
    if (screenPoints.size() <= 31) for (const QPointF &point : screenPoints) {
        painter.drawEllipse(point, 3.5, 3.5);
    }

    if (hoveredIndex_ >= 0 && hoveredIndex_ < screenPoints.size()) {
        const QPointF highlight = screenPoints.at(hoveredIndex_);
        painter.setPen(QPen(QColor(color_.red(), color_.green(), color_.blue(), 90), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(highlight.x(), plot.top()),
                         QPointF(highlight.x(), plot.bottom()));
        painter.setBrush(Qt::white);
        painter.setPen(QPen(color_, 2.5));
        painter.drawEllipse(highlight, 5.5, 5.5);
    }

    painter.restore();
    painter.setPen(QColor(QStringLiteral("#64748b")));
    const int labelStep = std::max(1, static_cast<int>(std::ceil(points_.size() / 6.0)));
    for (qsizetype index = 0; index < points_.size(); index += labelStep) {
        const qreal x = screenPoints.at(index).x();
        painter.drawText(QRectF(x - 36.0, plot.bottom() + 10.0, 72.0, 20.0),
                         Qt::AlignHCenter, points_.at(index).date.mid(5));
    }
    for (int tick=0; tick<=4; ++tick) {
        const qreal amount = maximum / divisor_ * (4-tick) / 4.0;
        painter.drawText(QRectF(0, plot.top()+plot.height()*tick/4.0-10,48,20), Qt::AlignRight,
            QString::number(amount,'f', divisor_ != 1 && maximum/divisor_ < 10 ? 1 : 0));
    }

}

void RevenueChart::mouseMoveEvent(QMouseEvent *event)
{
    if (points_.isEmpty()) {
        setCursor(Qt::ArrowCursor);
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
        setCursor(Qt::ArrowCursor);
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
    setCursor(Qt::PointingHandCursor);
    const RevenuePoint &point = points_.at(index);
    QToolTip::showText(mapToGlobal(event->position().toPoint()),
                       QStringLiteral("%1\n%2：%3 %4\n点击查看当日已支付订单").arg(point.date, label_)
                           .arg(point.revenueCents / divisor_, 0, 'f', divisor_ == 1 ? 0 : 2).arg(unit_),
                       this);
}

void RevenueChart::mouseReleaseEvent(QMouseEvent *event)
{
    const QRectF plot = QRectF(rect()).adjusted(54,24,-24,-42);
    if (event->button()==Qt::LeftButton && !points_.isEmpty() && plot.contains(event->position())) {
        const int index = points_.size()==1 ? 0 : qBound(0,
            qRound((event->position().x()-plot.left())/plot.width()*(points_.size()-1)), points_.size()-1);
        const auto date = points_[index].date;
        intro_->finish(); QToolTip::hideText(); emit dateClicked(date); return;
    }
    QWidget::mouseReleaseEvent(event);
}

void RevenueChart::keyPressEvent(QKeyEvent *event)
{
    if (!points_.isEmpty() && (event->key()==Qt::Key_Left || event->key()==Qt::Key_Right)) {
        hoveredIndex_ = hoveredIndex_ < 0 ? 0 : qBound(0,
            hoveredIndex_ + (event->key()==Qt::Key_Right ? 1 : -1), points_.size()-1);
        intro_->finish(); update(); event->accept(); return;
    }
    if (hoveredIndex_>=0 && hoveredIndex_<points_.size()
        && (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter || event->key()==Qt::Key_Space)) {
        emit dateClicked(points_[hoveredIndex_].date); event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}

void RevenueChart::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    hoveredIndex_ = -1;
    setCursor(Qt::ArrowCursor);
    QToolTip::hideText();
    update();
}

}  // namespace charging::server
