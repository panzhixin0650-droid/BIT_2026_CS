#pragma once

#include <QList>
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

    void setPoints(QList<RevenuePoint> points);
    [[nodiscard]] const QList<RevenuePoint> &points() const noexcept;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QList<RevenuePoint> points_;
    int hoveredIndex_ = -1;
};

}  // namespace charging::server
