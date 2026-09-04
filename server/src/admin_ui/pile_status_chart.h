#pragma once

#include <QColor>
#include <QList>
#include <QRectF>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QEvent;

namespace charging::server {

struct PileStatusSlice {
    QString key;
    QString label;
    qint64 count = 0;
    QColor color;
};

class PileStatusChart final : public QWidget {
    Q_OBJECT

public:
    explicit PileStatusChart(QWidget *parent = nullptr);

    void setSlices(QList<PileStatusSlice> slices);

signals:
    void statusClicked(const QString &statusKey);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int sliceAt(const QPointF &point) const;

    QList<PileStatusSlice> slices_;
    QRectF pieRect_;
    QList<QRectF> legendRects_;
    int hoveredSlice_ = -1;
};

}  // namespace charging::server
