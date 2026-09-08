#pragma once

#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QVector>

class QPainter;

namespace charging::client {

// Original, code-native cartographic artwork for the offline demo. Roads and
// land use are illustrative, not a routable / surveyed geographic database.
// Geometry is prepared once; the rendered viewport is reused across paints.
class DemoMapBackdrop final {
public:
    void prepare(const QSize &size, const QPointF &center, double scale, qreal pixelRatio);
    void paint(QPainter &painter, const QSize &size, const QPointF &center,
               double scale, qreal pixelRatio);
    [[nodiscard]] bool isReady() const { return !cache_.isNull(); }

private:
    struct Label {
        QPointF point;
        QString text;
        int kind = 0;
        double angle = 0;
    };
    struct UrbanTile {
        QPainterPath neighborhoods;
        QPainterPath buildings;
        QRectF bounds;
    };
    void buildGeometry();
    QPainterPath avenues_, ringRoad_, river_, parks_;
    QVector<QPainterPath> localRoads_;
    QVector<UrbanTile> urbanTiles_;
    QVector<Label> labels_;
    QImage cache_;
    QPointF cachedCenter_;
    double cachedScale_ = 0;
    bool built_ = false;
};

}  // namespace charging::client
