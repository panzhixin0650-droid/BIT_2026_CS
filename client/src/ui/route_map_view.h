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
    void setRoute(const RouteResult &route);
    void clearRoute();
    void zoomIn();
    void zoomOut();
    void fitRoute();

signals:
    void loadingChanged(bool loading);
    void readyChanged(bool ready);
    void statusChanged(const QString &message, bool error);

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyRoute();
    void fail();
    void command(const QString &script);

    QWebEngineView *view_ = nullptr;
    QTimer timeout_;
    QUrl scriptUrl_;
    QJsonArray paths_;
    quint64 generation_ = 0;
    bool initialized_ = false;
};

}  // namespace charging::client
