#pragma once

#include "local/map_types.h"

#include <QTimer>
#include <QJsonObject>
#include <QMargins>
#include <QWidget>

class QWebEngineView;

namespace charging::client {

// Only map events are exposed to JavaScript; no page or business API methods.
class MapEventBridge final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
public slots:
    void ready() { emit connected(); }
    void selectStation(const QString &id) { emit stationClicked(id); }
signals:
    void connected();
    void stationClicked(const QString &id);
};

class RouteMapView final : public QWidget {
    Q_OBJECT
public:
    explicit RouteMapView(QWidget *parent = nullptr);
    ~RouteMapView() override;
    [[nodiscard]] bool isPreloaded() const { return sdkLoaded_; }
    void preload(const QUrl &scriptUrl);
    void prepareMap();
    void setRoute(const RouteResult &route);
    void clearRoute();
    void retry();
    void zoomIn();
    void zoomOut();
    void fitRoute();
    void setStationScene(const QUrl &scriptUrl, const QJsonObject &scene);
    void selectStation(const QString &stationId);
    void setStationCenter(const QJsonObject &coordinate);
    void setStationViewport(const QMargins &margins);
    void fitStations();

signals:
    void preloadReady();
    void loadingChanged(bool loading);
    void readyChanged(bool ready);
    void retryAvailableChanged(bool available);
    void statusChanged(const QString &message, bool error);
    void stationSelected(const QString &stationId);

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class FailureReason {
        MissingScriptUrl,
        EmbeddedPageUnavailable,
        PageLoadFailed,
        BridgeUnavailable,
        SdkRequestFailed,
        SdkUnavailable,
        MapInitializationFailed,
        RouteRenderingFailed,
        StationRenderingFailed,
        RenderProcessTerminated,
        Timeout,
    };

    void initialize(const QUrl &scriptUrl, bool reportFailure);
    void checkInitialization();
    void applyRoute();
    void applyStations();
    void fail(FailureReason reason, bool reportFailure,
              const QString &detail = {});
    [[nodiscard]] QString failureMessage(FailureReason reason,
                                         const QString &detail) const;
    [[nodiscard]] bool canRetry(FailureReason reason) const;
    void command(const QString &script);

    QWebEngineView *view_ = nullptr;
    QTimer timeout_;
    QUrl scriptUrl_;
    QJsonArray paths_;
    QJsonObject stationScene_;
    quint64 generation_ = 0;
    bool sdkLoaded_ = false;
    bool initialized_ = false;
    bool initializing_ = false;
    bool routePending_ = false;
    bool reportInitializationFailure_ = false;
    bool stationMode_ = false;
    bool pageLoaded_ = false;
    bool channelReady_ = false;
};

}  // namespace charging::client
