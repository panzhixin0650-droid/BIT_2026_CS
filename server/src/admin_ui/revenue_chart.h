#pragma once
#include "chart_intro.h"

#include <QList>
#include <QColor>
#include <QMouseEvent>
#include <QString>
#include <QWidget>

namespace charging::server {

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
    ChartIntro *intro_ = nullptr;
    QList<RevenuePoint> points_;
    int hoveredIndex_ = -1;
    QString label_ = QStringLiteral("实收");
    QString unit_ = QStringLiteral("元");
    qreal divisor_ = 100;
    QColor color_{"#2f6fed"};
};

}  // namespace charging::server
