#include "pile_status_chart.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <cmath>
#include <utility>

namespace charging::server {
namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

PileStatusChart::PileStatusChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void PileStatusChart::setSlices(QList<PileStatusSlice> slices)
{
    slices_ = std::move(slices);
    hoveredSlice_ = -1;
    update();
}

void PileStatusChart::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);
    legendRects_.clear();

    const qreal total = [&] {
        qreal value = 0.0;
        for (const PileStatusSlice &slice : slices_) value += qMax<qint64>(0, slice.count);
        return value;
    }();
    // The monitoring card intentionally contains only the chart.  Keep it
    // anchored to the left so the unused right side remains visually quiet
    // and available for a future monitoring module.
    const qreal side = qMax<qreal>(0.0, qMin(width() * 0.58, height() - 34.0));
    pieRect_ = QRectF(22.0, (height() - side) / 2.0, side, side);
    if (total <= 0.0) {
        painter.setPen(QColor(QStringLiteral("#8793a7")));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无电桩数据"));
    } else {
        int startAngle = 90 * 16;
        for (int index = 0; index < slices_.size(); ++index) {
            const PileStatusSlice &slice = slices_.at(index);
            const qreal count = qMax<qint64>(0, slice.count);
            if (count <= 0.0) continue;
            const int spanAngle = qRound(360.0 * 16.0 * count / total);
            painter.setBrush(slice.color.lighter(index == hoveredSlice_ ? 112 : 100));
            painter.setPen(Qt::white);
            painter.drawPie(pieRect_, startAngle, -spanAngle);
            startAngle -= spanAngle;
        }
        // Keep the original ring visual while leaving its center free of
        // permanent labels.  Hovering the ring supplies the contextual data.
        painter.setBrush(Qt::white);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(pieRect_.center(), pieRect_.width() * 0.22,
                            pieRect_.height() * 0.22);
    }

    // Keep the legend deliberately lightweight: color marker + status only.
    // Counts and percentages are transient hover information, so the chart
    // remains readable even when a category has a very small slice.
    const qreal legendX = qMax(pieRect_.right() + 28.0, width() * 0.60);
    const qreal rowHeight = 38.0;
    const qreal legendTop = (height() - slices_.size() * rowHeight) / 2.0;
    painter.setFont(QFont(font().family(), 13));
    for (int index = 0; index < slices_.size(); ++index) {
        const PileStatusSlice &slice = slices_.at(index);
        const QRectF row(legendX, legendTop + index * rowHeight,
                         qMax<qreal>(0.0, width() - legendX - 18.0), rowHeight);
        legendRects_.append(row);
        painter.setBrush(slice.color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(row.left() + 8.0, row.center().y()), 6.0, 6.0);
        painter.setPen(QColor(QStringLiteral("#344054")));
        painter.drawText(QRectF(row.left() + 24.0, row.top(),
                                qMax<qreal>(0.0, row.width() - 24.0), row.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, slice.label);
    }
}

int PileStatusChart::sliceAt(const QPointF &point) const
{
    for (int index = 0; index < legendRects_.size(); ++index) {
        if (legendRects_.at(index).contains(point)) return index;
    }
    if (!pieRect_.contains(point)) return -1;
    const QPointF delta = point - pieRect_.center();
    const qreal radius = std::hypot(delta.x(), delta.y());
    if (radius > pieRect_.width() / 2.0
        || radius < pieRect_.width() * 0.22) return -1;
    qreal total = 0.0;
    for (const PileStatusSlice &slice : slices_) total += qMax<qint64>(0, slice.count);
    if (total <= 0.0) return -1;
    qreal angle = std::atan2(-delta.y(), delta.x()) * 180.0 / kPi;
    angle = std::fmod(90.0 - angle + 360.0, 360.0);
    qreal cursor = 0.0;
    for (int index = 0; index < slices_.size(); ++index) {
        const qreal span = 360.0 * qMax<qint64>(0, slices_.at(index).count) / total;
        if (span > 0.0 && angle >= cursor && angle < cursor + span) return index;
        cursor += span;
    }
    return -1;
}

void PileStatusChart::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = sliceAt(event->position());
        if (index >= 0) {
            emit statusClicked(slices_.at(index).key);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void PileStatusChart::mouseMoveEvent(QMouseEvent *event)
{
    const int index = sliceAt(event->position());
    if (index < 0) {
        QToolTip::hideText();
        setCursor(Qt::ArrowCursor);
        if (hoveredSlice_ != -1) {
            hoveredSlice_ = -1;
            update();
        }
        return;
    }

    qreal total = 0.0;
    for (const PileStatusSlice &slice : slices_) total += qMax<qint64>(0, slice.count);
    const PileStatusSlice &slice = slices_.at(index);
    const qreal percentage = total <= 0.0 ? 0.0 : 100.0 * slice.count / total;
    const QString tip = QStringLiteral("%1\n数量：%2 / %3\n占比：%4%")
        .arg(slice.label)
        .arg(slice.count)
        .arg(static_cast<qint64>(total))
        .arg(QString::number(percentage, 'f', 1));
    setCursor(Qt::PointingHandCursor);
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
    if (hoveredSlice_ != index) {
        hoveredSlice_ = index;
        update();
    }
}

void PileStatusChart::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    QToolTip::hideText();
    hoveredSlice_ = -1;
    setCursor(Qt::PointingHandCursor);
    update();
}

}  // namespace charging::server
