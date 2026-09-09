// 横向条形图控件声明，含单条数据结构与点击信号
#pragma once
#include "chart_intro.h"
#include <QColor>
#include <QList>
#include <QWidget>

namespace charging::server {
// 一条柱子的数据：标签、原始值、已格式化文本与跳转键
struct AnalysisBar {
    QString label;
    double value = 0;
    QString formattedValue;
    QColor color;
    QString key; // Stable destination identifier, independent of sorting and display names.
};
// 自绘条形图控件，支持悬停提示与点击查看明细
class AnalysisBarChart final : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisBarChart(QWidget *parent = nullptr);
    void playIntro();
    // 设置数据，可传入固定最大值让多图共用同一刻度
    void setBars(QList<AnalysisBar> bars, double fixedMaximum = 0);
    const QList<AnalysisBar> &bars() const { return bars_; }
signals:
    void barClicked(const QString &key);
protected:
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
private:
    // 入场动画、当前数据与各行矩形等内部绘制状态
    ChartIntro *intro_ = nullptr;
    QList<AnalysisBar> bars_;
    QList<QRectF> rows_;
    double fixedMaximum_ = 0;
    int hovered_ = -1;
};
}
