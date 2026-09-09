#pragma once

#include "charging/protocol/dto.h"
#include "local/map_types.h"
#include "ui/demo_map_backdrop.h"

#include <QHash>
#include <QMargins>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QPushButton;
class QPainter;
class QThread;

namespace charging::client {

class RouteMapView;

// A station scene on the shared Tencent canvas, or an explicitly labelled,
// offline QPainter demo. Both use the same coordinates and selection model.
class StationMapView final : public QWidget {
    Q_OBJECT
public:
    // 公开接口：设置地图源、定位、电站集合与视图缩放
    explicit StationMapView(QWidget *parent = nullptr);
    ~StationMapView() override;
    void preload();
    [[nodiscard]] bool isReady() const;
    void setMapScriptUrl(const QUrl &url);
    void setCenter(const MapLocation &location);
    void setCurrentLocation(const std::optional<MapLocation> &location);
    void setStations(const QList<protocol::StationDto> &stations);
    void selectStation(qint64 stationId);
    void focusStation(qint64 stationId);
    void fitStations();
    void zoomIn();
    void zoomOut();
    void setViewportMargins(const QMargins &margins);
    [[nodiscard]] qint64 selectedStationId() const { return selectedId_; }
    [[nodiscard]] QPointF pointForLocation(const MapLocation &location) const;
    [[nodiscard]] QRectF usableViewport() const;

// 信号通知选中电站、点击空白、用户交互与地图就绪
signals:
    void stationSelected(qint64 stationId);
    void backgroundClicked();
    void interactionStarted();
    void mapReady();

// 重写绘制与鼠标事件，实现离线地图的平移缩放
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
    void beginInteraction();
    void activateStation(qint64 stationId);
    void paintOfflineMap(QPainter &painter);
    void updatePreview();
    // 以下保存电站、标记、视图中心与缩放等内部状态
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
    bool warming_ = false;
    bool preloadStarted_ = false;
    bool receivedStations_ = false;
    DemoMapBackdrop demoBackdrop_;
    // 离线底图预热线程，只计算几何与图像
    QThread *preloadThread_ = nullptr;
    QUrl scriptUrl_;
    RouteMapView *webMap_ = nullptr;
    QWidget *controls_;
    QWidget *loadingPreview_;
    QLabel *modeLabel_;
    QLabel *statusLabel_;
    QPushButton *retry_;
    QPushButton *locate_;
};

}  // namespace charging::client
