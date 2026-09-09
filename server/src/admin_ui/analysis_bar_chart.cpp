// 管理台横向条形图控件，用于展示分类统计并支持点击查看明细
#include "analysis_bar_chart.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QToolTip>
#include <algorithm>
#include <utility>

namespace charging::server {
AnalysisBarChart::AnalysisBarChart(QWidget *parent) : QWidget(parent)
{
    intro_ = new ChartIntro(this);
    setMinimumHeight(296);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
// 仅在有数据时重播入场动画
void AnalysisBarChart::playIntro()
{
    if (!bars_.isEmpty()) intro_->replay();
}


// 更新数据并同步无障碍描述，动画立即结束避免闪动
void AnalysisBarChart::setBars(QList<AnalysisBar> bars, double fixedMaximum)
{
    intro_->finish();
    setCursor(Qt::ArrowCursor);
    bars_ = std::move(bars); fixedMaximum_ = fixedMaximum; hovered_ = -1;
    QStringList description;
    for (const auto &bar : bars_) description << bar.label + ": " + bar.formattedValue;
    setAccessibleDescription(description.join(QStringLiteral("；")));
    QToolTip::hideText(); update();
}
// 绘制每行标签、数值与进度条，宽度按最大值归一化
void AnalysisBarChart::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white); rows_.clear();
    QFont textFont = font(); textFont.setPixelSize(12); p.setFont(textFont);
    if (bars_.isEmpty()) {
        p.setPen(QColor("#64748b")); p.drawText(rect(), Qt::AlignCenter, QStringLiteral("当前范围暂无数据")); return;
    }
    double maximum = fixedMaximum_;
    for (const auto &bar : bars_) maximum = std::max(maximum, bar.value);
    maximum = std::max(1.0, maximum);
    const qreal spacing = qMin(44.0, (height() - 16.0) / bars_.size());
    for (int i = 0; i < bars_.size(); ++i) {
        const auto &bar = bars_[i];
        const QRectF row(4, 6+i*spacing, width()-8, spacing);
        rows_.append(row);
        p.setPen(Qt::NoPen);
        if (i == hovered_) { p.setBrush(QColor("#f5f8fd")); p.drawRoundedRect(row,6,6); }
        p.setPen(QColor("#52637c"));
        const int valueWidth = qMin(width()/2, p.fontMetrics().horizontalAdvance(bar.formattedValue) + 12);
        p.drawText(row.adjusted(2,0,-valueWidth,-spacing+20), Qt::AlignLeft|Qt::AlignVCenter,
                   p.fontMetrics().elidedText(bar.label, Qt::ElideRight, qMax(0,width()-valueWidth-16)));
        p.setPen(QColor("#243044"));
        p.drawText(row.adjusted(0,0,-2,-spacing+20),Qt::AlignRight|Qt::AlignVCenter,bar.formattedValue);
        const QRectF track(6,row.top()+24,width()-12,7);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#edf2f9")); p.drawRoundedRect(track,3.5,3.5);
        if (bar.value > 0) {
            p.setBrush(bar.color.isValid() ? bar.color : QColor("#2f6fed"));
            p.drawRoundedRect(QRectF(track.topLeft(),QSizeF(track.width()*bar.value/maximum*intro_->progress(),track.height())),3.5,3.5);
        }
    }
}
// 鼠标移动时高亮所在行并显示数值提示
void AnalysisBarChart::mouseMoveEvent(QMouseEvent *event)
{
    int hovered = -1;
    for (int i=0;i<rows_.size();++i) if (rows_[i].contains(event->position())) hovered = i;
    if (hovered != hovered_) { hovered_ = hovered; update(); }
    if (hovered < 0) { setCursor(Qt::ArrowCursor); QToolTip::hideText(); return; }
    const auto &bar = bars_[hovered];
    setCursor(bar.key.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    QToolTip::showText(event->globalPosition().toPoint(), QStringLiteral("<qt>%1<br>%2%3</qt>").arg(
        bar.label.toHtmlEscaped(), bar.formattedValue.toHtmlEscaped(),
        bar.key.isEmpty() ? QString() : QStringLiteral("<br>点击查看明细")), this);
}
// 点击带 key 的条目发出信号，交由页面跳转明细
void AnalysisBarChart::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        for (int i=0; i<rows_.size(); ++i) {
            if (rows_[i].contains(event->position()) && !bars_[i].key.isEmpty()) {
                const auto key = bars_[i].key;
                intro_->finish(); QToolTip::hideText(); emit barClicked(key); return;
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}
// 支持上下键切换选中、回车触发点击，便于键盘操作
void AnalysisBarChart::keyPressEvent(QKeyEvent *event)
{
    if (!bars_.isEmpty() && (event->key()==Qt::Key_Down || event->key()==Qt::Key_Up)) {
        hovered_ = hovered_ < 0 ? 0 : (hovered_ + (event->key()==Qt::Key_Down ? 1 : bars_.size()-1)) % bars_.size();
        intro_->finish(); update(); event->accept(); return;
    }
    if (hovered_>=0 && hovered_<bars_.size() && !bars_[hovered_].key.isEmpty()
        && (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter || event->key()==Qt::Key_Space)) {
        emit barClicked(bars_[hovered_].key); event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}
void AnalysisBarChart::leaveEvent(QEvent *) { setCursor(Qt::ArrowCursor); hovered_=-1; QToolTip::hideText(); update(); }
}
