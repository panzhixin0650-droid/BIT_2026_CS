// 文件用途：用内置离线数据绘制示例地图底图
#include "ui/demo_map_backdrop.h"

#include <QFile>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSet>
#include <QTransform>

#include <algorithm>
#include <cmath>

// 注册内置地图资源文件
static void initializeOfflineMapResources() { Q_INIT_RESOURCE(map_resources); }

namespace charging::client {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double designScale = 1048576.0;
// 经纬度转墨卡托归一化坐标，便于统一缩放
QPointF projected(double longitude, double latitude)
{
    return {(longitude + 180) / 360,
            (1 - std::asinh(std::tan(latitude * pi / 180)) / pi) / 2};
}
const QPointF origin = projected(123.42, 41.75);
enum Layer { Urban, Industrial, Park, Water, River, Rail, Local, Street, Major, Highway };
}

// 解析内置 JSON，把各图层要素转成绘制路径
void DemoMapBackdrop::buildGeometry()
{
    if (built_) return;
    built_ = true;
    initializeOfflineMapResources();
    QFile file(QStringLiteral(":/map/shenyang.json"));
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto data = QJsonDocument::fromJson(file.readAll()).object();
    const QStringList layers{QStringLiteral("urban"), QStringLiteral("industrial"),
        QStringLiteral("park"), QStringLiteral("water"), QStringLiteral("river"),
        QStringLiteral("rail"), QStringLiteral("local"), QStringLiteral("street"),
        QStringLiteral("major"), QStringLiteral("highway")};
    for (const auto &value : data.value(QStringLiteral("features")).toArray()) {
        const auto object = value.toObject();
        Feature feature;
        feature.layer = layers.indexOf(object.value(QStringLiteral("kind")).toString());
        if (feature.layer < 0) continue;
        feature.name = object.value(QStringLiteral("name")).toString();
        feature.path.setFillRule(Qt::OddEvenFill);
        for (const auto &ring : object.value(QStringLiteral("paths")).toArray()) {
            bool first = true;
            for (const auto &entry : ring.toArray()) {
                const auto coordinate = entry.toArray();
                const auto point = (projected(coordinate[0].toDouble(), coordinate[1].toDouble()) - origin) * designScale;
                if (first) feature.path.moveTo(point); else feature.path.lineTo(point);
                first = false;
            }
            if (feature.layer <= Water) feature.path.closeSubpath();
        }
        feature.bounds = feature.path.boundingRect();
        features_.append(std::move(feature));
    }
    // 按图层排序，保证面在下、道路在上
    std::stable_sort(features_.begin(), features_.end(), [](const auto &a, const auto &b) {
        return a.layer < b.layer;
    });
}

// prepare 只在尺寸或视角变化时重绘并缓存
void DemoMapBackdrop::prepare(const QSize &size, const QPointF &center,
                              double scale, qreal pixelRatio)
{
    if (size.isEmpty()) return;
    const QSize pixels = size * pixelRatio;
    if (!cache_.isNull() && cache_.size() == pixels && cache_.devicePixelRatio() == pixelRatio
        && center == cachedCenter_ && scale == cachedScale_) return;
    buildGeometry();
    cachedCenter_ = center;
    cachedScale_ = scale;
    cache_ = QImage(pixels, QImage::Format_RGB32);
    cache_.setDevicePixelRatio(pixelRatio);
    cache_.fill(QColor(QStringLiteral("#f4f3ec")));
    QPainter painter(&cache_);
    painter.setRenderHint(QPainter::Antialiasing);
    // 算出缩放平移矩阵，只处理可见范围内的要素
    const double factor = scale / designScale;
    const QPointF offset = QPointF(size.width()/2.0, size.height()/2.0) + (origin-center)*scale;
    const QTransform transform(factor, 0, 0, factor, offset.x(), offset.y());
    painter.setTransform(transform);
    const QRectF visible = transform.inverted().mapRect(QRectF(QPointF{}, size)).adjusted(-2,-2,2,2);
    const double detail = std::clamp(std::sqrt(factor), .7, 2.5);
    const auto stroke = [&painter](const QPainterPath &path, const char *color, double width) {
        QPen pen(QColor(color), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setCosmetic(true);
        painter.strokePath(path, pen);
    };
    QVector<const Feature *> visibleFeatures;
    for (const auto &feature : features_) {
        if (!feature.bounds.adjusted(-1,-1,1,1).intersects(visible)) continue;
        visibleFeatures.append(&feature);
        // 按图层分别填充面块或描边道路水系
        switch (feature.layer) {
        case Urban: painter.fillPath(feature.path, QColor("#e9e9e1")); break;
        case Industrial: painter.fillPath(feature.path, QColor("#e7e5df")); break;
        case Park: painter.fillPath(feature.path, QColor("#d2e4c7")); break;
        case Water: painter.fillPath(feature.path, QColor("#abd4d5")); break;
        case River: stroke(feature.path, "#abd4d5", 2.0*detail); break;
        case Rail: stroke(feature.path, "#cbc8bd", .75*detail); break;
        default: {
            const double width = (feature.layer == Highway ? 3.7 : feature.layer == Major ? 2.8
                : feature.layer == Street ? 1.8 : 1.1) * detail;
            stroke(feature.path, feature.layer == Highway ? "#d8c69e" : "#d9dace", width+1.2);
            stroke(feature.path, feature.layer == Highway ? "#fff0c9" : "#fffef8", width);
        }
        }
    }
    painter.resetTransform();
    QVector<QRectF> occupied;
    QSet<QString> names;
    // Actual OSM names, not invented streets or district placement.
    // 按优先级放置地名标签，越界或重叠的跳过
    for (int priority : {River, Park, Major, Highway, Street}) {
        for (const auto *feature : visibleFeatures) {
            if (feature->layer != priority || feature->name.isEmpty() || names.contains(feature->name)) continue;
            const bool area = priority == Park;
            if (area && feature->bounds.width()*feature->bounds.height()*factor*factor < 650) continue;
            QFont font = painter.font();
            font.setPixelSize(priority == River ? 14 : area ? 11 : 10);
            font.setWeight(priority == River ? QFont::DemiBold : QFont::Normal);
            font.setLetterSpacing(QFont::AbsoluteSpacing, .5);
            const QFontMetricsF metrics(font);
            const QSizeF textSize(metrics.horizontalAdvance(feature->name), metrics.height());
            if (!area && feature->path.length()*factor < textSize.width()*1.3) continue;
            const QPointF position = transform.map(area ? feature->bounds.center() : feature->path.pointAtPercent(.5));
            double angle = area ? 0 : -feature->path.angleAtPercent(.5);
            while (angle < -90) angle += 180;
            while (angle > 90) angle -= 180;
            QTransform labelTransform;
            labelTransform.translate(position.x(), position.y());
            labelTransform.rotate(angle);
            const QRectF box(QPointF(-textSize.width()/2, -textSize.height()/2), textSize);
            const QRectF screenBox = labelTransform.mapRect(box).adjusted(-8,-6,8,6);
            if (!QRectF(QPointF(8,8), QSizeF(size)-QSizeF(16,40)).contains(screenBox)) continue;
            if (std::any_of(occupied.cbegin(), occupied.cend(), [&screenBox](const QRectF &other) {
                    return other.intersects(screenBox);
                })) continue;
            occupied.append(screenBox);
            names.insert(feature->name);
            painter.save();
            painter.setTransform(labelTransform);
            QPainterPath text;
            text.addText(QPointF(box.left(), (metrics.ascent()-metrics.descent())/2), font, feature->name);
            painter.setPen(QPen(QColor(250,250,244,230), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(priority == River ? QColor("#508c92") : area ? QColor("#688264") : QColor("#818579"));
            painter.drawPath(text);
            painter.setPen(Qt::NoPen);
            painter.drawPath(text);
            painter.restore();
        }
    }
    // 没有可见要素时提示超出离线地图范围
    if (visibleFeatures.isEmpty()) {
        painter.setPen(QColor("#708478"));
        painter.drawText(QRectF(QPointF{}, size), Qt::AlignCenter,
            QStringLiteral("超出沈阳离线地图范围\n请重新定位，或使用腾讯地图"));
    }
}

// paint 直接贴出已缓存的底图图像
void DemoMapBackdrop::paint(QPainter &painter, const QSize &size, const QPointF &center,
                            double scale, qreal pixelRatio)
{
    prepare(size, center, scale, pixelRatio);
    painter.drawImage(QPointF{}, cache_);
}
}  // namespace charging::client
