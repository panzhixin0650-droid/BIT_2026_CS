#pragma once

#include "local/map_types.h"

#include <QTimer>
#include <QWidget>

class QWebEngineView;

namespace charging::client {

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

signals:
    void preloadReady();
    void loadingChanged(bool loading);
    void readyChanged(bool ready);
    void retryAvailableChanged(bool available);
    void statusChanged(const QString &message, bool error);

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
        RenderProcessTerminated,
        Timeout,
    };

    void initialize(const QUrl &scriptUrl, bool reportFailure);
    void applyRoute();
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
    quint64 generation_ = 0;
    bool sdkLoaded_ = false;
    bool initialized_ = false;
    bool initializing_ = false;
    bool routePending_ = false;
    bool reportInitializationFailure_ = false;
};

}  // namespace charging::client
