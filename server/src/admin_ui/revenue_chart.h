#pragma once

#include <QList>
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

private:
    QList<RevenuePoint> points_;
};

}  // namespace charging::server
