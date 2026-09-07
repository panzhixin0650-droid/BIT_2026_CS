#include "ui/demo_map_backdrop.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace charging::client {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double designScale = 1048576.0;

QPointF projected(double longitude, double latitude)
{
    return {(longitude + 180) / 360,
            (1 - std::asinh(std::tan(latitude * pi / 180)) / pi) / 2};
}

const QPointF origin = projected(123.42, 41.75);

QPointF point(double longitude, double latitude)
{
    return (projected(longitude, latitude) - origin) * designScale;
}

void road(QPainterPath &path, std::initializer_list<QPointF> points)
{
    auto it = points.begin();
    path.moveTo(*it++);
    for (; it != points.end(); ++it) path.lineTo(*it);
}
}  // namespace

void DemoMapBackdrop::buildGeometry()
{
    if (built_) return;
    built_ = true;
    // A deliberately authored city illustration: smaller blocks, courtyards,
    // varied street directions, landscaped riverbanks and road hierarchy.
    // No network, keys, map SDK, random regeneration or bitmap asset is needed.
    urbanTiles_.resize(9 * 8);
    for (int row = -34; row <= 34; ++row) {
        for (int column = -30; column <= 30; ++column) {
            const double lng = 123.42 + column * .0046 + (row > 0 ? row * .00020 : 0);
            const double lat = 41.75 + row * .0035;
            const QRectF block = QRectF(point(lng + .00048, lat + .00040),
                                        point(lng + .00390, lat + .00295)).normalized();
            auto &tile = urbanTiles_[((row + 34) / 8) * 8 + (column + 30) / 8];
            tile.bounds = tile.bounds.united(block);
            tile.neighborhoods.addRoundedRect(block, 1.1, 1.1);
            if ((row + column * 3) % 11 == 0) {
                parks_.addRoundedRect(block.adjusted(.5, .5, -.5, -.5), 1.5, 1.5);
            } else {
                const qreal gap = block.width() * .16;
                const qreal height = block.height() * .20;
                tile.buildings.addRect(QRectF(block.left() + gap, block.top() + gap,
                                           block.width() - gap * 2, height));
                if ((row - column) % 3 != 0) {
                    tile.buildings.addRect(QRectF(block.left() + gap, block.bottom() - gap - height,
                                               block.width() * .58, height));
                }
            }
        }
        const double lat = 41.75 + row * .0035;
        localRoads_.append(QPainterPath{});
        road(localRoads_.last(), {point(123.27, lat), point(123.42, lat), point(123.58, lat + .005)});
    }
    for (int column = -30; column <= 30; ++column) {
        const double lng = 123.42 + column * .0046;
        localRoads_.append(QPainterPath{});
        road(localRoads_.last(), {point(lng, 41.62), point(lng, 41.75), point(lng + .008, 41.88)});
    }

    river_.moveTo(point(123.24, 41.731));
    river_.cubicTo(point(123.31, 41.722), point(123.35, 41.738), point(123.392, 41.746));
    river_.cubicTo(point(123.432, 41.749), point(123.440, 41.775), point(123.487, 41.770));
    river_.cubicTo(point(123.535, 41.764), point(123.55, 41.800), point(123.60, 41.798));

    // The large parks break up the street texture and stay legible at home zoom.
    for (const auto &park : {
             QRectF(point(123.376, 41.773), point(123.387, 41.765)).normalized(),
             QRectF(point(123.427, 41.735), point(123.437, 41.727)).normalized(),
             QRectF(point(123.452, 41.706), point(123.465, 41.695)).normalized(),
             QRectF(point(123.402, 41.813), point(123.414, 41.804)).normalized()}) {
        parks_.addRoundedRect(park, 5, 5);
    }
    for (double lat : {41.694, 41.718, 41.744, 41.778, 41.804, 41.827}) {
        road(avenues_, {point(123.28, lat - .006), point(123.387, lat),
                         point(123.449, lat), point(123.57, lat + .004)});
    }
    for (double lng : {123.354, 123.387, 123.438, 123.470, 123.512}) {
        road(avenues_, {point(lng + .006, 41.64), point(lng, 41.73),
                         point(lng, 41.795), point(lng + .011, 41.87)});
    }
    ringRoad_.moveTo(point(123.303, 41.861));
    ringRoad_.cubicTo(point(123.322, 41.81), point(123.310, 41.759), point(123.35, 41.706));
    ringRoad_.cubicTo(point(123.386, 41.675), point(123.465, 41.678), point(123.517, 41.710));
    ringRoad_.cubicTo(point(123.55, 41.750), point(123.534, 41.806), point(123.551, 41.851));

    labels_ = {
        {point(123.410, 41.772), QStringLiteral("和平区"), 2},
        {point(123.449, 41.720), QStringLiteral("浑南区"), 2},
        {point(123.451, 41.810), QStringLiteral("沈河区"), 2},
        {point(123.374, 41.799), QStringLiteral("城市街区"), 1},
        {point(123.436, 41.757), QStringLiteral("浑河 · 示意"), 3, -26},
        {point(123.382, 41.769), QStringLiteral("滨水公园"), 1},
        {point(123.432, 41.731), QStringLiteral("城市绿地"), 1},
        {point(123.458, 41.701), QStringLiteral("生态公园"), 1},
        {point(123.409, 41.808), QStringLiteral("社区公园"), 1},
        {point(123.438, 41.791), QStringLiteral("青年大街"), 0, -90},
        {point(123.438, 41.714), QStringLiteral("城市主干道"), 0, -87},
        {point(123.418, 41.778), QStringLiteral("滨河大道"), 0},
        {point(123.418, 41.718), QStringLiteral("创新路"), 0},
        {point(123.407, 41.804), QStringLiteral("城市大道"), 0},
        {point(123.387, 41.711), QStringLiteral("和平南街"), 0, -87},
        {point(123.407, 41.694), QStringLiteral("南部大道"), 0},
    };
}

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
    cache_.fill(QColor(QStringLiteral("#f3f3eb")));
    QPainter painter(&cache_);
    painter.setRenderHint(QPainter::Antialiasing);
    const double factor = scale / designScale;
    const QPointF offset = QPointF(size.width() / 2.0, size.height() / 2.0) + (origin - center) * scale;
    const QTransform transform(factor, 0, 0, factor, offset.x(), offset.y());
    painter.setTransform(transform);
    const QRectF visible = transform.inverted().mapRect(QRectF(QPointF{}, size));
    for (const auto &tile : urbanTiles_) {
        if (!tile.bounds.intersects(visible)) continue;
        painter.fillPath(tile.neighborhoods, QColor(QStringLiteral("#e8eae1")));
        if (factor >= .9) painter.fillPath(tile.buildings, QColor(QStringLiteral("#dfe3da")));
    }
    painter.fillPath(parks_, QColor(QStringLiteral("#d4e5c9")));
    const auto stroke = [&painter](const QPainterPath &path, const QColor &color, double width) {
        QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        pen.setCosmetic(true);
        painter.strokePath(path, pen);
    };
    const double detail = std::clamp(std::sqrt(factor), .65, 2.4);
    // Keep intersecting roads as separate strokes. Combining an entire grid
    // into one antialiased path makes Qt's stroker do expensive intersections
    // and can stall login/network events for hundreds of milliseconds.
    for (const auto &path : localRoads_) {
        if (path.boundingRect().intersects(visible))
            stroke(path, QColor(QStringLiteral("#fafbf5")), 1.2 * detail);
    }
    stroke(river_, QColor(QStringLiteral("#d5e6d2")), 24 * factor);
    stroke(river_, QColor(QStringLiteral("#a7cfcb")), 17 * factor);
    stroke(river_, QColor(QStringLiteral("#b4d8d5")), 14 * factor);
    stroke(avenues_, QColor(QStringLiteral("#d9dece")), 5.7 * detail);
    stroke(avenues_, QColor(QStringLiteral("#fffef6")), 3.8 * detail);
    stroke(ringRoad_, QColor(QStringLiteral("#dbd2b0")), 7 * detail);
    stroke(ringRoad_, QColor(QStringLiteral("#faf0cd")), 4.8 * detail);
    painter.resetTransform();

    QVector<QRectF> occupied;
    // District labels first, so fine road labels cannot crowd them out.
    for (int priority : {2, 3, 1, 0}) {
        for (const auto &label : labels_) {
            if (label.kind != priority) continue;
            QFont font = painter.font();
            font.setPixelSize(label.kind == 2 ? 17 : label.kind == 3 ? 12 : 11);
            font.setWeight(label.kind == 2 ? QFont::DemiBold : QFont::Normal);
            font.setLetterSpacing(QFont::AbsoluteSpacing, label.kind == 2 ? 3 : .5);
            const QFontMetricsF metrics(font);
            const QSizeF textSize(metrics.horizontalAdvance(label.text), metrics.height());
            const QPointF position = transform.map(label.point);
            QTransform labelTransform;
            labelTransform.translate(position.x(), position.y());
            labelTransform.rotate(label.angle);
            const QRectF box(QPointF(-textSize.width() / 2, -textSize.height() / 2), textSize);
            const QRectF screenBox = labelTransform.mapRect(box).adjusted(-5, -4, 5, 4);
            if (!QRectF(QPointF(8, 8), QSizeF(size) - QSizeF(16, 35)).contains(screenBox)) continue;
            if (std::any_of(occupied.cbegin(), occupied.cend(), [&screenBox](const QRectF &other) {
                    return other.intersects(screenBox);
                })) continue;
            occupied.append(screenBox);
            painter.save();
            painter.setTransform(labelTransform);
            QPainterPath text;
            text.addText(QPointF(box.left(), (metrics.ascent() - metrics.descent()) / 2), font, label.text);
            painter.setPen(QPen(QColor(249, 250, 242, 220), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(label.kind == 3 ? QColor(QStringLiteral("#548f8c"))
                : label.kind == 0 ? QColor(QStringLiteral("#8b9485")) : QColor(QStringLiteral("#708e72")));
            painter.drawPath(text);
            painter.setPen(Qt::NoPen);
            painter.drawPath(text);
            painter.restore();
        }
    }
}

void DemoMapBackdrop::paint(QPainter &painter, const QSize &size, const QPointF &center,
                            double scale, qreal pixelRatio)
{
    prepare(size, center, scale, pixelRatio);
    painter.drawImage(QPointF{}, cache_);
}

}  // namespace charging::client
