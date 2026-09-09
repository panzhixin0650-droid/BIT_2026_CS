// 文件用途：离线示例地图底图渲染类的声明
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
    // prepare 负责按需重绘缓存，paint 负责输出
    void prepare(const QSize &size, const QPointF &center, double scale, qreal pixelRatio);
    void paint(QPainter &painter, const QSize &size, const QPointF &center,
               double scale, qreal pixelRatio);
    [[nodiscard]] bool isReady() const { return !cache_.isNull(); }
    [[nodiscard]] int featureCount() const { return features_.size(); }

private:
    // Feature 保存单条要素的路径、包围盒、名称与图层
    struct Feature {
        QPainterPath path;
        QRectF bounds;
        QString name;
        int layer = 0;
    };
    // 首次绘制前解析内置地图数据
    void buildGeometry();
    QVector<Feature> features_;
    // 缓存图像与对应中心、缩放，用于判断能否复用
    QImage cache_;
    QPointF cachedCenter_;
    double cachedScale_ = 0;
    bool built_ = false;
};

}  // namespace charging::client
