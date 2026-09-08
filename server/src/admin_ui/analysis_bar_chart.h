#pragma once
#include "chart_intro.h"
#include <QColor>
#include <QList>
#include <QWidget>

namespace charging::server {
struct AnalysisBar {
    QString label;
    double value = 0;
    QString formattedValue;
    QColor color;
    QString key; // Stable destination identifier, independent of sorting and display names.
};
class AnalysisBarChart final : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisBarChart(QWidget *parent = nullptr);
    void playIntro();
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
    ChartIntro *intro_ = nullptr;
    QList<AnalysisBar> bars_;
    QList<QRectF> rows_;
    double fixedMaximum_ = 0;
    int hovered_ = -1;
};
}
