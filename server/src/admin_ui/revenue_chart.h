// 折线趋势图控件声明，含单日数据点与日期点击信号
#pragma once
#include "chart_intro.h"

#include <QList>
#include <QColor>
#include <QMouseEvent>
#include <QString>
#include <QWidget>

namespace charging::server {

// 一个数据点：日期字符串与该日金额（整数分）
struct RevenuePoint {
    QString date;
    qint64 revenueCents = 0;
};

// Lightweight Qt Widgets chart used while Qt Charts remains optional.
class RevenueChart final : public QWidget {
    Q_OBJECT

public:
    explicit RevenueChart(QWidget *parent = nullptr);
    void playIntro();

    // 设置数据点，以及指标名、单位、换算倍数与线条颜色
    void setPoints(QList<RevenuePoint> points);
    void setMetric(const QString &label, const QString &unit, qreal divisor, const QColor &color);
    [[nodiscard]] const QList<RevenuePoint> &points() const noexcept;

signals:
    void dateClicked(const QString &date);

protected:
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // 动画对象、数据点与当前指标显示配置
    ChartIntro *intro_ = nullptr;
    QList<RevenuePoint> points_;
    int hoveredIndex_ = -1;
    QString label_ = QStringLiteral("实收");
    QString unit_ = QStringLiteral("元");
    qreal divisor_ = 100;
    QColor color_{"#2f6fed"};
};

}  // namespace charging::server
