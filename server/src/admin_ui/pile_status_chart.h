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
    void playIntro();

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
