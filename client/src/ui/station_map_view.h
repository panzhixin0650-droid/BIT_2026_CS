#pragma once

#include "charging/protocol/dto.h"
#include "local/map_types.h"

#include <QHash>
#include <QMargins>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QPushButton;

namespace charging::client {

class RouteMapView;

// A station scene on the shared Tencent canvas, or an explicitly labelled,
// offline QPainter demo. Both use the same coordinates and selection model.
class StationMapView final : public QWidget {
    Q_OBJECT
public:
    explicit StationMapView(QWidget *parent = nullptr);
    void setMapScriptUrl(const QUrl &url);
    void setCenter(const MapLocation &location);
    void setCurrentLocation(const std::optional<MapLocation> &location);
    void setStations(const QList<protocol::StationDto> &stations);
    void selectStation(qint64 stationId);
    void fitStations();
    void zoomIn();
    void zoomOut();
    void setViewportMargins(const QMargins &margins);
    [[nodiscard]] qint64 selectedStationId() const { return selectedId_; }
    [[nodiscard]] QPointF pointForLocation(const MapLocation &location) const;
    [[nodiscard]] QRectF usableViewport() const;

signals:
    void stationSelected(qint64 stationId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void updateMarkers();
    void updateControls();
    void applyWebScene();
    void changeZoom(int delta);
    void activateStation(qint64 stationId);
    QList<protocol::StationDto> stations_;
    QHash<qint64, QAbstractButton *> markers_;
    std::optional<MapLocation> location_;
    QMargins margins_{24, 120, 68, 36};
    QPointF center_;
    QPointF pressPosition_;
    QPointF lastPosition_;
    double scale_ = 1048576.0;
    qint64 selectedId_ = 0;
    bool autoFit_ = true;
    bool dragging_ = false;
    bool webSceneDirty_ = false;
    QUrl scriptUrl_;
    RouteMapView *webMap_ = nullptr;
    QWidget *controls_;
    QLabel *modeLabel_;
    QLabel *statusLabel_;
    QPushButton *retry_;
    QPushButton *locate_;
};

}  // namespace charging::client
