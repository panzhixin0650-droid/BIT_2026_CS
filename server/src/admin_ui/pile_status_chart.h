// 环形占比图控件声明，含扇区数据与点击信号
#pragma once
#include "chart_intro.h"

#include <QColor>
#include <QList>
#include <QRectF>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QEvent;

namespace charging::server {

// 一个扇区：跳转键、显示标签、数量与颜色
struct PileStatusSlice {
    QString key;
    QString label;
    qint64 count = 0;
    QColor color;
};

// 自绘环形图控件，可显示合计并支持点击查看明细
class PileStatusChart final : public QWidget {
    Q_OBJECT

public:
    explicit PileStatusChart(QWidget *parent = nullptr);
    void playIntro();

    // 设置扇区数据、中心标题与数值单位换算
    void setSlices(QList<PileStatusSlice> slices);
    void setCaption(const QString &caption, bool interactive = true);
    void setValueFormat(qreal divisor, const QString &unit);

signals:
    void statusClicked(const QString &statusKey);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // 入场动画、命中区域与显示格式等内部状态
    ChartIntro *intro_ = nullptr;
    int sliceAt(const QPointF &point) const;

    QList<PileStatusSlice> slices_;
    QRectF pieRect_;
    QList<QRectF> legendRects_;
    int hoveredSlice_ = -1;
    QString caption_ = QStringLiteral("电桩总数");
    bool interactive_ = true;
    qreal valueDivisor_ = 1;
    QString valueUnit_;
};

}  // namespace charging::server
