#pragma once

#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QVector>

class QPainter;

namespace charging::client {

// Bundled OpenStreetMap extract, prepared explicitly with the Overpass API.
// Reused by Mock and the clearly labelled online loading preview.
class DemoMapBackdrop final {
public:
    void prepare(const QSize &size, const QPointF &center, double scale, qreal pixelRatio);
    void paint(QPainter &painter, const QSize &size, const QPointF &center,
               double scale, qreal pixelRatio);
    [[nodiscard]] bool isReady() const { return !cache_.isNull(); }
    [[nodiscard]] int featureCount() const { return features_.size(); }

private:
    struct Feature {
        QPainterPath path;
        QRectF bounds;
        QString name;
        int layer = 0;
    };
    void buildGeometry();
    QVector<Feature> features_;
    QImage cache_;
    QPointF cachedCenter_;
    double cachedScale_ = 0;
    bool built_ = false;
};

}  // namespace charging::client
