// 环形占比图控件，用于电桩状态、订单状态等分类构成展示
#include "pile_status_chart.h"
#include <algorithm>

#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
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
    intro_ = new ChartIntro(this);
    setMinimumHeight(280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(interactive_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

// 全部分类为零时不播放动画
void PileStatusChart::playIntro()
{
    if (std::any_of(slices_.cbegin(), slices_.cend(), [](const auto &slice) { return slice.count > 0; })) intro_->replay();
}


// 更新扇区数据并生成无障碍描述文本
void PileStatusChart::setSlices(QList<PileStatusSlice> slices)
{
    intro_->finish();
    slices_ = std::move(slices);
    hoveredSlice_ = -1;
    QStringList labels;
    for (const auto &slice : slices_)
        labels << slice.label + QStringLiteral("：")
            + QString::number(slice.count / valueDivisor_, 'f', valueDivisor_ == 1 ? 0 : 2) + valueUnit_;
    setAccessibleDescription(labels.join(QStringLiteral("；")));
    QToolTip::hideText(); update();
}

void PileStatusChart::setCaption(const QString &caption, bool interactive)
{
    caption_ = caption; interactive_ = interactive;
    setFocusPolicy(interactive ? Qt::StrongFocus : Qt::NoFocus);
    setAccessibleName(caption); setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

// 设置数值换算倍数与单位，如分转元、Wh转kWh
void PileStatusChart::setValueFormat(qreal divisor, const QString &unit)
{
    valueDivisor_ = qMax(1.0,divisor); valueUnit_ = unit; update();
}

// 绘制环形图：按占比画扇区，进度用于入场动画
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
    const qreal side = qMax<qreal>(0.0, qMin(width() * 0.46, height() - 48.0));
    pieRect_ = QRectF(8, (height() - side) / 2.0, side, side);
    if (total <= 0.0) {
        painter.setPen(QColor(QStringLiteral("#64748b")));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("当前范围暂无数据"));
    } else {
        painter.setBrush(QColor("#edf2f9"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(pieRect_);
        int startAngle = 90 * 16;
        int remaining = qRound(360 * 16 * intro_->progress());
        for (int index = 0; index < slices_.size(); ++index) {
            const PileStatusSlice &slice = slices_.at(index);
            const qreal count = qMax<qint64>(0, slice.count);
            if (count <= 0.0) continue;
            const int spanAngle = qRound(360.0 * 16.0 * count / total);
            painter.setBrush(slice.color.lighter(index == hoveredSlice_ ? 112 : 100));
            painter.setPen(Qt::white);
            const int visibleSpan = qBound(0, remaining, spanAngle);
            if (visibleSpan > 0) painter.drawPie(pieRect_, startAngle, -visibleSpan);
            remaining -= spanAngle;
            startAngle -= spanAngle;
        }
        // The hollow center holds the total; the legend retains every category.
        painter.setBrush(Qt::white);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(pieRect_.center(), pieRect_.width() * 0.34,
                            pieRect_.height() * 0.34);
    }

    // 圆心显示合计值，字号按可用宽度自动缩小
    if (total > 0) {
        QFont number = font(); number.setPixelSize(24); number.setWeight(QFont::DemiBold);
        const QString totalText = QString::number(total/valueDivisor_, 'f', valueDivisor_==1 ? 0 : 2);
        const qreal textWidth = pieRect_.width() * 0.62;
        while (number.pixelSize() > 12 && QFontMetrics(number).horizontalAdvance(totalText) > textWidth)
            number.setPixelSize(number.pixelSize() - 1);
        painter.setFont(number); painter.setPen(QColor("#243044"));
        painter.drawText(pieRect_.adjusted(0,-10,0,-10),Qt::AlignCenter,totalText);
        QFont caption = font(); caption.setPixelSize(12); painter.setFont(caption); painter.setPen(QColor("#64748b"));
        painter.drawText(pieRect_.adjusted(0,36,0,0),Qt::AlignCenter,caption_);
    }
    // 右侧图例逐项列出标签、数值与百分比
    const qreal legendX = pieRect_.right() + 22;
    const qreal rowHeight = 42;
    const qreal legendTop = (height() - slices_.size() * rowHeight) / 2;
    QFont legend = font(); legend.setPixelSize(12); painter.setFont(legend);
    for (int index = 0; index < slices_.size(); ++index) {
        const auto &slice = slices_[index];
        const QRectF row(legendX, legendTop+index*rowHeight, qMax(0.0,width()-legendX-6),rowHeight);
        legendRects_.append(row);
        if (hoveredSlice_ == index) {
            painter.setPen(Qt::NoPen); painter.setBrush(QColor("#f0f5fc"));
            painter.drawRoundedRect(row.adjusted(-3,0,0,0),4,4);
        }
        painter.setBrush(slice.color); painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(row.left(),row.top()+8,8,8),2,2);
        painter.setPen(QColor("#344054"));
        painter.drawText(row.adjusted(16,0,0,-20),Qt::AlignLeft|Qt::AlignVCenter,slice.label);
        painter.setPen(QColor("#64748b"));
        painter.drawText(row.adjusted(16,20,0,0),Qt::AlignLeft|Qt::AlignVCenter,
            QStringLiteral("%1%2  %3%").arg(slice.count/valueDivisor_,0,'f',valueDivisor_==1 ? 0 : 2).arg(valueUnit_).arg(total>0 ? 100.0*slice.count/total : 0,0,'f',1));
    }

}

// 判断坐标落在哪个图例行或哪个扇区，返回其序号
int PileStatusChart::sliceAt(const QPointF &point) const
{
    for (int index = 0; index < legendRects_.size(); ++index) {
        if (legendRects_.at(index).contains(point)) return index;
    }
    if (!pieRect_.contains(point)) return -1;
    const QPointF delta = point - pieRect_.center();
    const qreal radius = std::hypot(delta.x(), delta.y());
    if (radius > pieRect_.width() / 2.0
        || radius < pieRect_.width() * 0.34) return -1;
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

// 可交互时左键点击扇区发出状态键，供页面跳转明细
void PileStatusChart::mousePressEvent(QMouseEvent *event)
{
    if (interactive_ && event->button() == Qt::LeftButton) {
        intro_->finish();
        const int index = sliceAt(event->position());
        if (index >= 0) {
            emit statusClicked(slices_.at(index).key);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

// 悬停显示数值与占比提示，并高亮对应扇区
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
    QString tip = QStringLiteral("%1\n数值：%2 / %3 %5\n占比：%4%")
        .arg(slice.label)
        .arg(slice.count/valueDivisor_,0,'f',valueDivisor_==1 ? 0 : 2)
        .arg(total/valueDivisor_,0,'f',valueDivisor_==1 ? 0 : 2)
        .arg(QString::number(percentage, 'f', 1)).arg(valueUnit_);
    if (interactive_) tip += QStringLiteral("\n点击查看明细");
    setCursor(interactive_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
    if (hoveredSlice_ != index) {
        hoveredSlice_ = index;
        update();
    }
}

void PileStatusChart::keyPressEvent(QKeyEvent *event)
{
    if (interactive_ && !slices_.isEmpty() && (event->key()==Qt::Key_Down || event->key()==Qt::Key_Up)) {
        hoveredSlice_ = hoveredSlice_ < 0 ? 0 : (hoveredSlice_ + (event->key()==Qt::Key_Down ? 1 : slices_.size()-1)) % slices_.size();
        intro_->finish(); update(); event->accept(); return;
    }
    if (interactive_ && hoveredSlice_>=0 && hoveredSlice_<slices_.size()
        && (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter || event->key()==Qt::Key_Space)) {
        emit statusClicked(slices_[hoveredSlice_].key); event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}

// 鼠标离开时清除高亮与提示
void PileStatusChart::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    QToolTip::hideText();
    hoveredSlice_ = -1;
    setCursor(interactive_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

}  // namespace charging::server
