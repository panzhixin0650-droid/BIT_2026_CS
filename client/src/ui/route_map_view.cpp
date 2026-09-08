#include "ui/route_map_view.h"

#include <QFile>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QHideEvent>
#include <QJsonDocument>
#include <QPointer>
#include <QShowEvent>
#include <QVBoxLayout>
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif

static void initializeMapResources() { Q_INIT_RESOURCE(map_resources); }

namespace charging::client {

#ifdef CHARGING_CLIENT_HAS_WEBENGINE
namespace {
QWebEngineProfile *mapProfile()
{
    // Qt 6's implicit profile is off-the-record. Keep the map SDK's normal
    // HTTP cache across launches; honor response cache headers, no tile scraper.
    // Shared by home and navigation, but isolated from business/account pages.
    static QPointer<QWebEngineProfile> profile;
    if (!profile) {
        profile = new QWebEngineProfile(QStringLiteral("bit-map-v1"), QCoreApplication::instance());
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/map-webengine");
        profile->setCachePath(directory + QStringLiteral("/http"));
        profile->setPersistentStoragePath(directory + QStringLiteral("/storage"));
        profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
        profile->setHttpCacheMaximumSize(64 * 1024 * 1024);
        profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    }
    return profile;
}
}
#endif

RouteMapView::RouteMapView(QWidget *parent) : QWidget(parent)
{
    initializeMapResources();
    setObjectName(QStringLiteral("routeMapCanvas"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    timeout_.setSingleShot(true);
    timeout_.setInterval(15000);
    connect(&timeout_, &QTimer::timeout, this, [this]() {
        fail(FailureReason::Timeout,
             reportInitializationFailure_ || routePending_);
    });
    tileTimeout_.setSingleShot(true);
    tileTimeout_.setParent(this);
    tileTimeout_.setObjectName(QStringLiteral("stationTileTimeout"));
    tileTimeout_.setInterval(15000);
    connect(&tileTimeout_, &QTimer::timeout, this, [this] {
        if (stationMode_ && !baseMapReady_) fail(FailureReason::Timeout, isVisible());
    });
}

RouteMapView::~RouteMapView()
{
    ++generation_;
    timeout_.stop();
    tileTimeout_.stop();
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

void RouteMapView::preload(const QUrl &scriptUrl)
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (scriptUrl.isEmpty() || routePending_ ||
        (scriptUrl_ == scriptUrl && (sdkLoaded_ || initializing_))) {
        return;
    }
    paths_ = {};
    initialize(scriptUrl, false);
#else
    Q_UNUSED(scriptUrl);
#endif
}

void RouteMapView::prepareMap()
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (!sdkLoaded_ || initialized_ || !view_) return;
    view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
    view_->page()->setVisible(true);
    QPointer<RouteMapView> guard(this);
    QPointer<QWebEngineView> source(view_);
    view_->page()->runJavaScript(QStringLiteral("bitMap.init()"),
        [guard, source](const QVariant &value) {
            if (!guard || !source || source != guard->view_) return;
            if (value.toBool()) guard->initialized_ = true;
        });
#endif
}

void RouteMapView::setRoute(const RouteResult &route)
{
    stationMode_ = false;
    ++generation_;
    paths_ = route.paths;
    routePending_ = true;
    emit readyChanged(false);
    emit retryAvailableChanged(false);
    emit loadingChanged(true);
    emit statusChanged(QStringLiteral("正在加载地图…"), false);
    timeout_.setInterval(15000);
    timeout_.start();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (route.mapScriptUrl.isEmpty()) {
        fail(FailureReason::MissingScriptUrl, true);
        return;
    }
    if (sdkLoaded_ && scriptUrl_ == route.mapScriptUrl) {
        view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
        view_->page()->setVisible(true);
        applyRoute();
        return;
    }
    if (initializing_ && scriptUrl_ == route.mapScriptUrl) {
        reportInitializationFailure_ = true;
        view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
        view_->page()->setVisible(true);
        return;
    }
    initialize(route.mapScriptUrl, true);
#else
    routePending_ = false;
    timeout_.stop();
    emit loadingChanged(false);
    emit retryAvailableChanged(false);
    emit statusChanged(QStringLiteral("当前构建未启用 Qt WebEngine，请重新配置客户端"), true);
#endif
}

void RouteMapView::initialize(const QUrl &scriptUrl, bool reportFailure)
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    ++generation_;
    sdkLoaded_ = false;
    initialized_ = false;
    initializing_ = true;
    pageLoaded_ = false;
    channelReady_ = false;
    baseMapReady_ = false;
    emit baseMapReadyChanged(false);
    reportInitializationFailure_ = reportFailure;
    scriptUrl_ = scriptUrl;
    timeout_.setInterval(reportFailure ? 15000 : 60000);
    timeout_.start();
    // Only failed/cancelled initialization or a changed SDK configuration needs a new view.
    if (stationMode_) tileTimeout_.start();
    if (view_) {
        view_->disconnect(this);
        view_->stop();
        view_->hide();
        view_->deleteLater();
    }
    view_ = new QWebEngineView(this);
    view_->setPage(new QWebEnginePage(mapProfile(), view_));
    view_->setObjectName(stationMode_ ? QStringLiteral("stationWebView") : QStringLiteral("routeWebView"));
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    view_->setContextMenuPolicy(Qt::NoContextMenu);
    layout()->addWidget(view_);
    // Hidden QStackedWidget pages do not automatically receive their final
    // layout. Warm the actual map at a useful viewport, not Chromium's tiny
    // default size; no window is shown and login keeps keyboard focus.
    layout()->activate();
    view_->resize(contentsRect().size());
    view_->page()->setBackgroundColor(QColor(QStringLiteral("#eef3ec")));
    view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
    view_->page()->setVisible(true);
    QPointer<QWebEngineView> source(view_);
    auto *channel = new QWebChannel(view_->page());
    auto *bridge = new MapEventBridge(channel);
    channel->registerObject(QStringLiteral("mapEvents"), bridge);
    view_->page()->setWebChannel(channel);
    connect(bridge, &MapEventBridge::connected, this, [this, source] {
        if (!source || source != view_) return;
        channelReady_ = true;
        checkInitialization();
    });
    connect(bridge, &MapEventBridge::tilesLoaded, this, [this, source] {
        if (!source || source != view_ || baseMapReady_) return;
        baseMapReady_ = true;
        tileTimeout_.stop();
        emit baseMapReadyChanged(true);
    });
    connect(bridge, &MapEventBridge::stationClicked, this, [this, source](const QString &id) {
        if (source && source == view_ && stationMode_ && initialized_) emit stationSelected(id);
    });
    connect(view_, &QWebEngineView::loadFinished, this, [this, source](bool loaded) {
        if (!source || source != view_ || !initializing_) return;
        if (!loaded) {
            fail(FailureReason::PageLoadFailed, reportInitializationFailure_);
            return;
        }
        pageLoaded_ = true;
        view_->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
        view_->page()->setVisible(true);
        checkInitialization();
    });
    connect(view_, &QWebEngineView::renderProcessTerminated, this,
            [this, source](QWebEnginePage::RenderProcessTerminationStatus status,
                           int exitCode) {
                if (source && source == view_)
                    fail(FailureReason::RenderProcessTerminated,
                         reportInitializationFailure_ || routePending_
                             || !paths_.isEmpty() || isVisible(),
                         QStringLiteral("状态 %1，退出码 %2")
                             .arg(static_cast<int>(status))
                             .arg(exitCode));
            });
    QFile html(QStringLiteral(":/map/map.html"));
    if (!html.open(QIODevice::ReadOnly)) {
        fail(FailureReason::EmbeddedPageUnavailable, reportFailure);
        return;
    }
    const QString content = QString::fromUtf8(html.readAll()).replace(
        QStringLiteral("__MAP_SCRIPT_URL__"), scriptUrl_.toString(QUrl::FullyEncoded).toHtmlEscaped());
    view_->setHtml(content, scriptUrl_.adjusted(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment));
#else
    Q_UNUSED(scriptUrl);
    Q_UNUSED(reportFailure);
#endif
}

void RouteMapView::checkInitialization()
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (!initializing_ || !pageLoaded_ || !channelReady_ || !view_) return;
    QPointer<RouteMapView> guard(this);
    QPointer<QWebEngineView> source(view_);
    view_->page()->runJavaScript(QStringLiteral(R"JS(
        (() => {
            if (!window.bitMap || !bitMap.state) return "bridge-unavailable";
            if (bitMap.state.error) return "sdk-request-failed";
            if (!bitMap.state.sdkReady || typeof TMap === "undefined")
                return "sdk-unavailable";
            return "ready";
        })()
    )JS"),
        [guard, source](const QVariant &value) {
            if (!guard || !source || source != guard->view_ || !guard->initializing_) return;
            const QString diagnostic = value.toString();
            if (diagnostic != QStringLiteral("ready")) {
                const FailureReason reason =
                    diagnostic == QStringLiteral("sdk-request-failed")
                    ? FailureReason::SdkRequestFailed
                    : diagnostic == QStringLiteral("sdk-unavailable")
                    ? FailureReason::SdkUnavailable
                    : FailureReason::BridgeUnavailable;
                guard->fail(reason, guard->reportInitializationFailure_);
                return;
            }
            guard->sdkLoaded_ = true;
            guard->initializing_ = false;
            guard->timeout_.stop();
            emit guard->preloadReady();
            if (guard->routePending_) guard->applyRoute();
        });
#endif
}

void RouteMapView::applyRoute()
{
    if (stationMode_) { applyStations(); return; }
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    const QString data = QString::fromUtf8(QJsonDocument(paths_).toJson(QJsonDocument::Compact));
    const quint64 generation = generation_;
    QPointer<RouteMapView> guard(this);
    view_->page()->runJavaScript(
        QStringLiteral(R"JS(
            (() => {
                const drawn = bitMap.setRoute(%1);
                if (drawn) return "ready";
                return bitMap.state.ready
                    ? "route-rendering-failed"
                    : "map-initialization-failed";
            })()
        )JS").arg(data),
        [guard, generation](const QVariant &value) {
            if (!guard || generation != guard->generation_) return;
            const QString diagnostic = value.toString();
            if (diagnostic != QStringLiteral("ready")) {
                guard->fail(diagnostic == QStringLiteral("route-rendering-failed")
                                ? FailureReason::RouteRenderingFailed
                                : FailureReason::MapInitializationFailed,
                            true);
                return;
            }
            guard->initialized_ = true;
            guard->routePending_ = false;
            guard->reportInitializationFailure_ = false;
            guard->timeout_.stop();
            emit guard->loadingChanged(false);
            emit guard->readyChanged(true);
            emit guard->retryAvailableChanged(false);
            emit guard->statusChanged(QStringLiteral("路线已绘制 · 滚轮缩放，拖动平移"), false);
        });
#endif
}

void RouteMapView::clearRoute()
{
    ++generation_;
    paths_ = {};
    routePending_ = false;
    reportInitializationFailure_ = false;
    if (!initializing_) timeout_.stop();
    emit readyChanged(false);
    emit retryAvailableChanged(false);
    emit loadingChanged(false);
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (view_) {
        if (initialized_) {
            view_->page()->runJavaScript(QStringLiteral("bitMap.clear()"));
        }
    }
#endif
}

void RouteMapView::retry()
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (initializing_ || (!stationMode_ && paths_.isEmpty()) || scriptUrl_.isEmpty()) return;
    routePending_ = true;
    emit readyChanged(false);
    emit retryAvailableChanged(false);
    emit loadingChanged(true);
    emit statusChanged(QStringLiteral("正在重新加载地图…"), false);
    initialize(scriptUrl_, true);
#endif
}

void RouteMapView::fail(FailureReason reason, bool reportFailure,
                        const QString &detail)
{
    ++generation_;
    sdkLoaded_ = false;
    initialized_ = false;
    baseMapReady_ = false;
    emit baseMapReadyChanged(false);
    initializing_ = false;
    routePending_ = false;
    reportInitializationFailure_ = false;
    timeout_.stop();
    tileTimeout_.stop();
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (view_) {
        view_->disconnect(this);
        view_->stop();
        view_->hide();
        view_->deleteLater();
        view_ = nullptr;
    }
#endif
    if (!reportFailure) return;
    emit readyChanged(false);
    emit loadingChanged(false);
    emit retryAvailableChanged(canRetry(reason));
    emit statusChanged(failureMessage(reason, detail), true);
}

QString RouteMapView::failureMessage(FailureReason reason,
                                     const QString &detail) const
{
    QString message;
    switch (reason) {
    case FailureReason::MissingScriptUrl:
        message = QStringLiteral(
            "地图脚本地址为空：请检查腾讯地图 Key 是否已配置且格式正确。");
        break;
    case FailureReason::EmbeddedPageUnavailable:
        message = QStringLiteral(
            "客户端内置地图页面缺失：请重新构建或安装完整客户端。");
        break;
    case FailureReason::PageLoadFailed:
        message = QStringLiteral(
            "地图页面加载失败：请检查网络连接、代理或证书设置。");
        break;
    case FailureReason::BridgeUnavailable:
        message = QStringLiteral(
            "地图页面初始化异常：客户端地图脚本未能正常启动。");
        break;
    case FailureReason::SdkRequestFailed:
        message = QStringLiteral(
            "腾讯地图 SDK 脚本下载失败：请检查网络、代理，以及 Key 的 JavaScript API GL 权限或域名白名单。");
        break;
    case FailureReason::SdkUnavailable:
        message = QStringLiteral(
            "腾讯地图 SDK 未提供可用接口：请检查 Key 权限、配额和 SDK 服务状态。");
        break;
    case FailureReason::MapInitializationFailed:
        message = QStringLiteral(
            "地图实例初始化失败：可能是 SDK 配置、WebEngine 或显卡兼容问题。");
        break;
    case FailureReason::RouteRenderingFailed:
        message = QStringLiteral(
            "路线数据已获取，但地图绘制失败：可查看文字详情并重新加载地图。");
        break;
    case FailureReason::StationRenderingFailed:
        message = QStringLiteral("充电站地图绘制失败，请重新加载地图。");
        break;
    case FailureReason::RenderProcessTerminated:
        message = QStringLiteral(
            "地图渲染进程异常退出：可能是 WebEngine、显卡驱动或内存问题。");
        break;
    case FailureReason::Timeout:
        message = QStringLiteral(
            "地图加载超时（15 秒）：网络较慢、SDK 未响应或 Key 权限校验未完成。");
        break;
    }
    if (!detail.isEmpty()) message += QStringLiteral("（%1）").arg(detail);
    if (canRetry(reason)) {
        message += stationMode_ ? QStringLiteral(" 请点击“重新加载地图”重试。") : QStringLiteral(
            " 请点击“重新加载地图”重试；已获取的路线文字仍可查看。");
    }
    return message;
}

bool RouteMapView::canRetry(FailureReason reason) const
{
    return reason != FailureReason::MissingScriptUrl
        && reason != FailureReason::EmbeddedPageUnavailable
        && (stationMode_ || !paths_.isEmpty()) && !scriptUrl_.isEmpty();
}

void RouteMapView::command(const QString &script)
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (initialized_ && (stationMode_ || !paths_.isEmpty())) view_->page()->runJavaScript(script);
#else
    Q_UNUSED(script);
#endif
}

void RouteMapView::zoomIn() { command(QStringLiteral("bitMap.zoom(1)")); }
void RouteMapView::zoomOut() { command(QStringLiteral("bitMap.zoom(-1)")); }
void RouteMapView::fitRoute() { command(QStringLiteral("bitMap.fit()")); }

void RouteMapView::setStationScene(const QUrl &scriptUrl, const QJsonObject &scene)
{
    stationMode_ = true;
    stationScene_ = scene;
    ++generation_;
    routePending_ = true;
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (sdkLoaded_ && scriptUrl_ == scriptUrl) { applyStations(); return; }
    if (initializing_ && scriptUrl_ == scriptUrl) return;
    emit loadingChanged(true);
    emit retryAvailableChanged(false);
    initialize(scriptUrl, true);
#else
    Q_UNUSED(scriptUrl);
    emit statusChanged(QStringLiteral("当前构建未启用 Qt WebEngine，请重新配置客户端"), true);
#endif
}

void RouteMapView::applyStations()
{
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    const quint64 generation = generation_;
    QPointer<RouteMapView> guard(this);
    const QString data = QString::fromUtf8(QJsonDocument(stationScene_).toJson(QJsonDocument::Compact));
    view_->page()->runJavaScript(QStringLiteral("bitMap.setStations(%1)").arg(data),
        [guard, generation](const QVariant &result) {
            if (!guard || generation != guard->generation_) return;
            if (!result.toBool()) { guard->fail(FailureReason::StationRenderingFailed, true); return; }
            guard->initialized_ = true;
            guard->routePending_ = false;
            guard->reportInitializationFailure_ = false;
            guard->timeout_.stop();
            emit guard->loadingChanged(false);
            emit guard->readyChanged(true);
            emit guard->retryAvailableChanged(false);
            emit guard->statusChanged({}, false);
        });
#endif
}

void RouteMapView::selectStation(const QString &stationId)
{
    stationScene_.insert(QStringLiteral("selected"), stationId);
    const QString data = QString::fromUtf8(QJsonDocument(QJsonArray{stationId}).toJson(QJsonDocument::Compact));
    command(QStringLiteral("bitMap.selectStation(%1[0])").arg(data));
}

void RouteMapView::setStationCenter(const QJsonObject &coordinate)
{
    command(QStringLiteral("bitMap.setCenter(%1)").arg(QString::fromUtf8(
        QJsonDocument(coordinate).toJson(QJsonDocument::Compact))));
}

void RouteMapView::setStationViewport(const QMargins &margins)
{
    const QJsonObject padding{{QStringLiteral("left"), margins.left()}, {QStringLiteral("top"), margins.top()},
        {QStringLiteral("right"), margins.right()}, {QStringLiteral("bottom"), margins.bottom()}};
    stationScene_.insert(QStringLiteral("padding"), padding);
    command(QStringLiteral("bitMap.setStationPadding(%1)").arg(QString::fromUtf8(
        QJsonDocument(padding).toJson(QJsonDocument::Compact))));
}

void RouteMapView::fitStations() { command(QStringLiteral("bitMap.fitStations()")); }

void RouteMapView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (view_ && sdkLoaded_ && !initializing_) {
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
        if (initialized_) {
            view_->page()->runJavaScript(QStringLiteral("bitMap.resume()"));
        }
    }
#endif
}

}  // namespace charging::client
