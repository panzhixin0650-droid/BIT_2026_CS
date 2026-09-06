#include "ui/route_map_view.h"

#include <QFile>
#include <QHideEvent>
#include <QJsonDocument>
#include <QPointer>
#include <QShowEvent>
#include <QVBoxLayout>
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

static void initializeMapResources() { Q_INIT_RESOURCE(map_resources); }

namespace charging::client {

RouteMapView::RouteMapView(QWidget *parent) : QWidget(parent)
{
    initializeMapResources();
    setObjectName(QStringLiteral("routeMapCanvas"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    timeout_.setSingleShot(true);
    timeout_.setInterval(15000);
    connect(&timeout_, &QTimer::timeout, this, &RouteMapView::fail);
}

RouteMapView::~RouteMapView()
{
    ++generation_;
    timeout_.stop();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    // WebEngine may complete a JavaScript callback while its page is destroyed.
    // Invalidate callbacks while this object's members are still alive.
    if (view_) {
        view_->disconnect(this);
        delete view_;
        view_ = nullptr;
    }
#endif
}

void RouteMapView::setRoute(const RouteResult &route)
{
    ++generation_;
    paths_ = route.paths;
    emit readyChanged(false);
    emit loadingChanged(true);
    emit statusChanged(QStringLiteral("正在加载地图…"), false);
    timeout_.start();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (initialized_ && scriptUrl_ == route.mapScriptUrl) {
        applyRoute();
        return;
    }
    const quint64 generation = generation_;
    initialized_ = false;
    scriptUrl_ = route.mapScriptUrl;
    // Only failed/cancelled initialization or a changed SDK configuration needs a new view.
    if (view_) {
        view_->disconnect(this);
        view_->stop();
        view_->hide();
        view_->deleteLater();
    }
    view_ = new QWebEngineView(this);
    view_->setObjectName(QStringLiteral("routeWebView"));
    view_->setContextMenuPolicy(Qt::NoContextMenu);
    layout()->addWidget(view_);
    connect(view_, &QWebEngineView::loadFinished, this, [this, generation](bool loaded) {
        if (generation != generation_) return;
        if (!loaded) { fail(); return; }
        QPointer<RouteMapView> guard(this);
        view_->page()->runJavaScript(QStringLiteral("!!(window.bitMap && bitMap.state.ready && !bitMap.state.error)"),
            [guard, generation](const QVariant &value) {
                if (!guard || generation != guard->generation_) return;
                if (!value.toBool()) { guard->fail(); return; }
                guard->initialized_ = true;
                guard->applyRoute();
            });
    });
    connect(view_, &QWebEngineView::renderProcessTerminated, this,
            [this, source = view_](QWebEnginePage::RenderProcessTerminationStatus, int) {
                if (source == view_) fail();
            });
    QFile html(QStringLiteral(":/map/map.html"));
    if (!html.open(QIODevice::ReadOnly)) { fail(); return; }
    const QString content = QString::fromUtf8(html.readAll()).replace(
        QStringLiteral("__MAP_SCRIPT_URL__"), scriptUrl_.toString(QUrl::FullyEncoded).toHtmlEscaped());
    view_->setHtml(content, scriptUrl_.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment));
#else
    timeout_.stop();
    emit loadingChanged(false);
    emit statusChanged(QStringLiteral("当前构建未启用 Qt WebEngine，请重新配置客户端"), true);
#endif
}

void RouteMapView::applyRoute()
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    const QString data = QString::fromUtf8(QJsonDocument(paths_).toJson(QJsonDocument::Compact));
    const quint64 generation = generation_;
    QPointer<RouteMapView> guard(this);
    view_->page()->runJavaScript(QStringLiteral("bitMap.setRoute(%1)").arg(data),
        [guard, generation](const QVariant &value) {
            if (!guard || generation != guard->generation_) return;
            if (!value.toBool()) { guard->fail(); return; }
            guard->timeout_.stop();
            emit guard->loadingChanged(false);
            emit guard->readyChanged(true);
            emit guard->statusChanged(QStringLiteral("路线已绘制 · 滚轮缩放，拖动平移"), false);
        });
#endif
}

void RouteMapView::clearRoute()
{
    ++generation_;
    timeout_.stop();
    paths_ = {};
    emit readyChanged(false);
    emit loadingChanged(false);
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (view_) {
        if (initialized_) {
            view_->page()->runJavaScript(QStringLiteral("bitMap.clear()"));
        } else {
            view_->stop();
        }
    }
#endif
}

void RouteMapView::fail()
{
    ++generation_;
    initialized_ = false;
    timeout_.stop();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (view_) {
        view_->disconnect(this);
        view_->stop();
        view_->hide();
        view_->deleteLater();
        view_ = nullptr;
    }
#endif
    emit readyChanged(false);
    emit loadingChanged(false);
    emit statusChanged(QStringLiteral("地图加载失败或超时，请检查网络、Key 的 JavaScript API GL 权限后重新规划"), true);
}

void RouteMapView::command(const QString &script)
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (initialized_ && !paths_.isEmpty()) view_->page()->runJavaScript(script);
#else
    Q_UNUSED(script);
#endif
}

void RouteMapView::zoomIn() { command(QStringLiteral("bitMap.zoom(1)")); }
void RouteMapView::zoomOut() { command(QStringLiteral("bitMap.zoom(-1)")); }
void RouteMapView::fitRoute() { command(QStringLiteral("bitMap.fit()")); }

void RouteMapView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    clearRoute();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (view_ && initialized_) {
        view_->page()->setVisible(false);
        view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Frozen);
    }
#endif
}

void RouteMapView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (view_) {
        view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
        view_->page()->setVisible(true);
    }
#endif
}

}  // namespace charging::client
