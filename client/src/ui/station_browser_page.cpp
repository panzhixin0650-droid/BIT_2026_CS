#include "ui/station_browser_page.h"
#include "ui/reservation_hint.h"

#include "ui/charging_stop_dialog.h"
#include "ui/station_map_view.h"
#include "ui/station_preview_card.h"
#include "ui/client_theme.h"
#include "ui/pricing_hint.h"
#include "ui/pricing_info_button.h"
#include "ui/route_map_view.h"
#include <QPlainTextEdit>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

// 本文件实现电站浏览页主体：详情、导航路线与当前订单展示
namespace charging::client {

namespace {

// 金额与单价以整数分存储，显示时转换为元
QString formatPrice(qint64 centsPerKwh)
{
    return QStringLiteral("¥%1.%2/度")
        .arg(centsPerKwh / 100)
        .arg(centsPerKwh % 100, 2, 10, QChar('0'));
}

QString formatMoney(qint64 cents)
{
    return QStringLiteral("¥%1.%2")
        .arg(cents / 100)
        .arg(cents % 100, 2, 10, QChar('0'));
}

// 以下若干函数把枚举翻译为中文文案与状态颜色
QString pileTypeText(protocol::PileType type)
{
    return type == protocol::PileType::Fast ? QStringLiteral("快充")
                                            : QStringLiteral("慢充");
}

QString pileStatusText(protocol::PileStatus status)
{
    switch (status) {
    case protocol::PileStatus::Idle:
        return QStringLiteral("闲置 · 可预约");
    case protocol::PileStatus::Reserved:
        return QStringLiteral("已预约");
    case protocol::PileStatus::Charging:
        return QStringLiteral("使用中");
    case protocol::PileStatus::Fault:
        return QStringLiteral("故障");
    case protocol::PileStatus::Offline:
        return QStringLiteral("离线");
    }
    return QStringLiteral("未知");
}

QString pileStatusColor(protocol::PileStatus status)
{
    switch (status) {
    case protocol::PileStatus::Idle:
        return QStringLiteral("#386a3c");
    case protocol::PileStatus::Reserved:
        return QStringLiteral("#b06000");
    case protocol::PileStatus::Charging:
        return QStringLiteral("#245c45");
    case protocol::PileStatus::Fault:
        return QStringLiteral("#c62828");
    case protocol::PileStatus::Offline:
        return QStringLiteral("#697969");
    }
    return QStringLiteral("#697969");
}

QString orderStatusText(protocol::OrderStatus status)
{
    switch (status) {
    case protocol::OrderStatus::Reserved:
        return QStringLiteral("预约中");
    case protocol::OrderStatus::Charging:
        return QStringLiteral("充电中");
    case protocol::OrderStatus::PendingPayment:
        return QStringLiteral("待支付");
    case protocol::OrderStatus::Completed:
        return QStringLiteral("已完成");
    case protocol::OrderStatus::Cancelled:
        return QStringLiteral("已取消");
    }
    return QStringLiteral("未知");
}

// 统一创建带卡片样式的容器
QFrame *createCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setProperty("role", "card");
    return card;
}

}  // namespace

// 构造函数：装配地图首页、电站详情页与导航页
StationBrowserPage::StationBrowserPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("authenticatedHomePage"));
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("stationBrowserPages"));
    rootLayout->addWidget(pages_);

    listPage_ = new QWidget(pages_);
    listPage_->setObjectName(QStringLiteral("stationListPage"));
    auto *listPageLayout = new QVBoxLayout(listPage_);
    listPageLayout->setContentsMargins(0, 0, 0, 0);

    setupMapHome();

    // 电站详情页：站点信息、参考单价与充电桩列表
    detailPage_ = new QWidget(pages_);
    detailPage_->setObjectName(QStringLiteral("stationDetailPage"));
    auto *detailPageLayout = new QVBoxLayout(detailPage_);
    detailPageLayout->setContentsMargins(2, 4, 2, 4);
    detailPageLayout->setSpacing(12);
    backButton_ = new QPushButton(QStringLiteral("‹ 返回充电地图"), detailPage_);
    backButton_->setObjectName(QStringLiteral("stationDetailBackButton"));
    backButton_->setFlat(true);
    detailMessageLabel_ = new QLabel(detailPage_);
    detailMessageLabel_->setObjectName(QStringLiteral("stationDetailMessage"));
    detailMessageLabel_->setWordWrap(true);
    detailMessageLabel_->hide();

    auto *detailScrollArea = new QScrollArea(detailPage_);
    detailScrollArea->setWidgetResizable(true);
    detailScrollArea->setFrameShape(QFrame::NoFrame);
    detailScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailContent_ = new QWidget(detailScrollArea);
    detailContent_->setObjectName(QStringLiteral("stationDetailContent"));
    auto *detailLayout = new QVBoxLayout(detailContent_);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(12);
    detailNameLabel_ = new QLabel(detailContent_);
    detailNameLabel_->setObjectName(QStringLiteral("stationDetailName"));
    QFont detailNameFont = detailNameLabel_->font();
    detailNameFont.setPointSize(18);
    detailNameFont.setBold(true);
    detailNameLabel_->setFont(detailNameFont);
    detailNameLabel_->setWordWrap(true);
    detailMetaLabel_ = new QLabel(detailContent_);
    detailMetaLabel_->setObjectName(QStringLiteral("stationDetailMeta"));
    detailMetaLabel_->setWordWrap(true);
    detailPriceLabel_ = new QLabel(detailContent_);
    detailPriceLabel_->setObjectName(QStringLiteral("stationDetailPrice"));
    detailPriceLabel_->setWordWrap(true);
    detailPriceLabel_->setTextFormat(Qt::PlainText);
    detailPriceLabel_->setStyleSheet(QStringLiteral("color: #386a3c; font-weight: 600;"));
    detailPricingInfo_ = new PricingInfoButton(detailContent_);
    detailPricingInfo_->setObjectName(QStringLiteral("stationPricingInfoButton"));
    auto *detailPriceRow = new QHBoxLayout;
    detailPriceRow->setSpacing(6);
    detailPriceRow->addWidget(detailPriceLabel_, 0, Qt::AlignVCenter);
    detailPriceRow->addWidget(detailPricingInfo_, 0, Qt::AlignVCenter);
    detailPriceRow->addStretch();
    detailNavigationButton_ = new QPushButton(QStringLiteral("导航"), detailContent_);
    detailNavigationButton_->setObjectName(QStringLiteral("stationDetailNavigationButton"));
    auto *pileTitle = new QLabel(QStringLiteral("充电桩"), detailContent_);
    QFont pileTitleFont = pileTitle->font();
    pileTitleFont.setBold(true);
    pileTitle->setFont(pileTitleFont);
    pileListLayout_ = new QVBoxLayout();
    pileListLayout_->setSpacing(10);
    detailLayout->addWidget(detailNameLabel_);
    detailLayout->addWidget(detailMetaLabel_);
    detailLayout->addLayout(detailPriceRow);
    detailLayout->addWidget(detailNavigationButton_, 0, Qt::AlignLeft);
    detailLayout->addWidget(pileTitle);
    detailLayout->addLayout(pileListLayout_);
    detailLayout->addStretch();
    detailScrollArea->setWidget(detailContent_);
    detailPageLayout->addWidget(backButton_, 0, Qt::AlignLeft);
    detailPageLayout->addWidget(detailMessageLabel_);
    detailPageLayout->addWidget(detailScrollArea, 1);

    // 导航页：起终点、出行方式与路线结果展示
    navigationPage_ = new QWidget(pages_);
    navigationPage_->setObjectName(QStringLiteral("stationNavigationPage"));
    auto *navigationLayout = new QVBoxLayout(navigationPage_);
    navigationLayout->setContentsMargins(16, 12, 16, 12);
    navigationLayout->setSpacing(8);
    auto *navigationBackButton =
        new QPushButton(QStringLiteral("‹ 返回"), navigationPage_);
    navigationBackButton->setObjectName(QStringLiteral("navigationBackButton"));
    navigationBackButton->setAccessibleName(QStringLiteral("返回充电站"));
    navigationBackButton->setFlat(true);
    navigationBackButton->setFixedWidth(72);
    auto *navigationTitle = new QLabel(QStringLiteral("路线导航"), navigationPage_);
    navigationTitle->setObjectName(QStringLiteral("navigationHeading"));
    navigationTitle->setAlignment(Qt::AlignCenter);
    QFont navigationTitleFont = navigationTitle->font();
    navigationTitleFont.setPointSize(22);
    navigationTitleFont.setBold(true);
    navigationTitle->setFont(navigationTitleFont);

    auto *navigationHeader = new QHBoxLayout();
    navigationHeader->setSpacing(8);
    navigationHeader->setContentsMargins(0, 0, 0, 2);
    navigationHeader->addWidget(navigationBackButton, 0, Qt::AlignVCenter);
    navigationHeader->addWidget(navigationTitle, 1, Qt::AlignVCenter);
    auto *navigationHeaderBalance = new QWidget(navigationPage_);
    navigationHeaderBalance->setFixedWidth(72);
    navigationHeader->addWidget(navigationHeaderBalance);

    auto *routeControlsCard = createCard(navigationPage_);
    routeControlsCard->setObjectName(QStringLiteral("routeControlsCard"));
    auto *routeControlsLayout = new QVBoxLayout(routeControlsCard);
    routeControlsLayout->setContentsMargins(12, 10, 12, 10);
    routeControlsLayout->setSpacing(8);

    auto *startLabel = new QLabel(QStringLiteral("起点"), routeControlsCard);
    startLabel->setMinimumWidth(34);
    routeStartInput_ = new QLineEdit(routeControlsCard);
    routeStartInput_->setObjectName(QStringLiteral("routeStartInput"));
    routeStartInput_->setPlaceholderText(
        QStringLiteral("输入包含城市名称的路线起点"));
    auto *routeStartRow = new QHBoxLayout();
    routeStartRow->setSpacing(8);
    routeStartRow->addWidget(startLabel);
    routeStartRow->addWidget(routeStartInput_, 1);

    auto *destinationLabel = new QLabel(QStringLiteral("终点"), routeControlsCard);
    destinationLabel->setMinimumWidth(34);
    routeDestinationLabel_ = new QLabel(routeControlsCard);
    routeDestinationLabel_->setObjectName(QStringLiteral("routeDestination"));
    routeDestinationLabel_->setWordWrap(true);
    routeDestinationLabel_->setTextFormat(Qt::PlainText);
    routeDestinationLabel_->setMaximumHeight(44);
    routeDestinationLabel_->setStyleSheet(QStringLiteral("color: #536553;"));
    auto *routeDestinationRow = new QHBoxLayout();
    routeDestinationRow->setSpacing(8);
    routeDestinationRow->addWidget(destinationLabel, 0, Qt::AlignTop);
    routeDestinationRow->addWidget(routeDestinationLabel_, 1);

    auto *modeLabel = new QLabel(QStringLiteral("方式"), routeControlsCard);
    modeLabel->setMinimumWidth(34);
    routeModeCombo_ = new QComboBox(routeControlsCard);
    routeModeCombo_->setObjectName(QStringLiteral("routeModeCombo"));
    routeModeCombo_->addItem(QStringLiteral("驾车"),
                             static_cast<int>(RouteMode::Driving));
    routeModeCombo_->addItem(QStringLiteral("步行"),
                             static_cast<int>(RouteMode::Walking));
    routeModeCombo_->addItem(QStringLiteral("公共交通"),
                             static_cast<int>(RouteMode::Transit));
    routeModeCombo_->addItem(QStringLiteral("骑行"),
                             static_cast<int>(RouteMode::Cycling));
    routePlanButton_ = new QPushButton(QStringLiteral("开始导航"), routeControlsCard);
    routePlanButton_->setObjectName(QStringLiteral("routePlanButton"));
    auto *routeOptions = new QHBoxLayout();
    routeOptions->setSpacing(8);
    routeOptions->addWidget(modeLabel);
    routeOptions->addWidget(routeModeCombo_, 1);
    routeOptions->addWidget(routePlanButton_);
    routeControlsLayout->addLayout(routeStartRow);
    routeControlsLayout->addLayout(routeDestinationRow);
    routeControlsLayout->addLayout(routeOptions);

    routeMessageLabel_ = new QLabel(navigationPage_);
    routeMessageLabel_->setObjectName(QStringLiteral("routeMessage"));
    routeMessageLabel_->setWordWrap(true);
    routeMessageLabel_->setTextFormat(Qt::PlainText);
    routeMessageLabel_->setMaximumHeight(76);
    routeMessageLabel_->hide();
    routeDisplayLabel_ = new QLabel(
        QStringLiteral("选择出行方式后点击“开始导航”"), navigationPage_);
    routeDisplayLabel_->setObjectName(QStringLiteral("routeDisplay"));
    routeDisplayLabel_->setAlignment(Qt::AlignCenter);
    routeDisplayLabel_->setWordWrap(true);
    routeDisplayLabel_->setTextFormat(Qt::PlainText);
    routeDisplayLabel_->setMinimumHeight(120);
    routeDisplayLabel_->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Expanding);
    routeDisplayLabel_->setStyleSheet(QStringLiteral(
        "background: #f0f3e9; border: 1px solid #acb8a6; border-radius: 12px; "
        "color: #536553; padding: 16px;"));
    routeDisplayStack_ = new QStackedWidget(navigationPage_);
    routeDisplayStack_->setObjectName(QStringLiteral("routeDisplayStack"));
    routeDisplayStack_->setMinimumHeight(120);
    routeDisplayStack_->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Expanding);
    routeDisplayStack_->addWidget(routeDisplayLabel_);
    routeMapView_ = new RouteMapView(routeDisplayStack_);
    routeDisplayStack_->addWidget(routeMapView_);
    routeSummaryLabel_ = new QLabel(navigationPage_);
    routeSummaryLabel_->setObjectName(QStringLiteral("routeSummary"));
    routeSummaryLabel_->setTextFormat(Qt::PlainText);
    routeSummaryLabel_->setWordWrap(true);
    routeSummaryLabel_->hide();
    routeDetails_ = new QPlainTextEdit(navigationPage_);
    routeDetails_->setObjectName(QStringLiteral("routeDetails"));
    routeDetails_->setReadOnly(true);
    routeDetails_->setMinimumHeight(0);
    routeDetails_->setMaximumHeight(88);
    routeDetails_->hide();

    // 地图工具栏：缩放、全程、重载与分步详情
    auto *mapToolbar = new QHBoxLayout();
    mapToolbar->setSpacing(6);
    auto *zoomIn = new QPushButton(QStringLiteral("＋"), navigationPage_);
    auto *zoomOut = new QPushButton(QStringLiteral("−"), navigationPage_);
    auto *fitRoute = new QPushButton(QStringLiteral("显示全程"), navigationPage_);
    auto *retryMap = new QPushButton(QStringLiteral("重新加载"), navigationPage_);
    routeDetailsButton_ = new QPushButton(QStringLiteral("详情"), navigationPage_);
    zoomIn->setObjectName(QStringLiteral("mapZoomInButton"));
    zoomOut->setObjectName(QStringLiteral("mapZoomOutButton"));
    fitRoute->setObjectName(QStringLiteral("mapFitRouteButton"));
    retryMap->setObjectName(QStringLiteral("mapRetryButton"));
    routeDetailsButton_->setObjectName(QStringLiteral("routeDetailsButton"));
    zoomIn->setAccessibleName(QStringLiteral("放大地图"));
    zoomOut->setAccessibleName(QStringLiteral("缩小地图"));
    zoomIn->setToolTip(QStringLiteral("放大地图（也可使用鼠标滚轮）"));
    zoomOut->setToolTip(QStringLiteral("缩小地图（也可使用鼠标滚轮）"));
    fitRoute->setToolTip(QStringLiteral("调整地图视野，显示完整路线"));
    retryMap->setAccessibleName(QStringLiteral("重新加载地图"));
    retryMap->setToolTip(QStringLiteral("复用已获取的路线数据，重新加载地图画布"));
    for (auto *button : {zoomIn, zoomOut, fitRoute}) {
        button->setStyleSheet(QStringLiteral("min-height: 30px; padding: 0 8px;"));
        button->setEnabled(false);
        mapToolbar->addWidget(button);
    }
    zoomIn->setFixedWidth(38);
    zoomOut->setFixedWidth(38);
    retryMap->setStyleSheet(QStringLiteral("min-height: 30px; padding: 0 8px;"));
    retryMap->setEnabled(false);
    retryMap->hide();
    routeDetailsButton_->setStyleSheet(
        QStringLiteral("min-height: 30px; padding: 0 8px;"));
    routeDetailsButton_->setEnabled(false);
    routeDetailsButton_->setCheckable(true);
    mapToolbar->insertStretch(3);
    mapToolbar->addWidget(retryMap);
    mapToolbar->addWidget(routeDetailsButton_);
    connect(zoomIn, &QPushButton::clicked, routeMapView_, &RouteMapView::zoomIn);
    connect(zoomOut, &QPushButton::clicked, routeMapView_, &RouteMapView::zoomOut);
    connect(fitRoute, &QPushButton::clicked, routeMapView_, &RouteMapView::fitRoute);
    connect(retryMap, &QPushButton::clicked, routeMapView_, &RouteMapView::retry);
    connect(routeDetailsButton_, &QPushButton::toggled, routeDetails_, &QWidget::setVisible);
    connect(routeMapView_, &RouteMapView::preloadReady, this, [this]() {
        if (pages_->currentWidget() != navigationPage_) return;
        routeDisplayStack_->setCurrentWidget(routeMapView_);
        routeMapView_->prepareMap();
    });
    connect(routeMapView_, &RouteMapView::readyChanged, this,
            [zoomIn, zoomOut, fitRoute](bool ready) {
                for (auto *button : {zoomIn, zoomOut, fitRoute}) button->setEnabled(ready);
            });
    // 地图加载状态变化时同步禁用路线控件
    connect(routeMapView_, &RouteMapView::loadingChanged, this, [this](bool loading) {
        mapLoading_ = loading;
        if (loading) routeDisplayStack_->setCurrentWidget(routeMapView_);
        updateRouteControls();
    });
    connect(routeMapView_, &RouteMapView::retryAvailableChanged, this,
            [retryMap](bool available) {
                retryMap->setEnabled(available);
                retryMap->setVisible(available);
            });
    connect(routeMapView_, &RouteMapView::statusChanged, this,
            [this](const QString &message, bool error) {
                showRouteMessage(message, error);
                if (error) {
                    routeDisplayLabel_->setText(QStringLiteral(
                        "地图未能显示，具体原因见上方。\n已获取的路线说明仍可在“详情”中查看。"));
                    routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
                }
            });
    navigationLayout->addLayout(navigationHeader);
    navigationLayout->addWidget(routeControlsCard);
    navigationLayout->addWidget(routeMessageLabel_);
    navigationLayout->addWidget(routeSummaryLabel_);
    navigationLayout->addLayout(mapToolbar);
    navigationLayout->addWidget(routeDisplayStack_, 1);
    navigationLayout->addWidget(routeDetails_);

    pages_->addWidget(listPage_);
    sheetPages_->addWidget(detailPage_);
    pages_->addWidget(navigationPage_);
    connect(pages_, &QStackedWidget::currentChanged, this, [this]() {
        if (pages_->currentWidget() != navigationPage_) emit navigationClosed();
    });
    pages_->setCurrentWidget(listPage_);

    // 绑定搜索、位置、导航、订单等各类按钮信号
    const auto search = [this] { submitSearch(); };
    connect(refreshButton_, &QPushButton::clicked, this, search);
    connect(keywordInput_, &QLineEdit::returnPressed, this, search);
    connect(locationPresetCombo_, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                const QString address = locationPresetCombo_->itemData(index).toString();
                if (!address.isEmpty()) {
                    locationAddressInput_->setText(address);
                } else {
                    locationAddressInput_->clear();
                    locationAddressInput_->setFocus();
                }
            });
    connect(locationAddressInput_, &QLineEdit::textEdited, this,
            [this](const QString &) {
                const int manualIndex = locationPresetCombo_->findData(QString{});
                if (manualIndex >= 0
                    && locationPresetCombo_->currentIndex() != manualIndex) {
                    const QSignalBlocker blocker(locationPresetCombo_);
                    locationPresetCombo_->setCurrentIndex(manualIndex);
                }
            });
    connect(resolveLocationButton_, &QPushButton::clicked, this, [this]() {
        emit locationResolutionRequested(locationAddressInput_->text());
    });
    connect(locationAddressInput_, &QLineEdit::returnPressed, this, [this]() {
        emit locationResolutionRequested(locationAddressInput_->text());
    });
    connect(backButton_, &QPushButton::clicked, this, &StationBrowserPage::detailBackRequested);
    connect(detailNavigationButton_, &QPushButton::clicked, this, [this]() {
        navigationReturnPage_ = listPage_;
        emit navigationRequested(navigationStation_);
    });
    connect(navigationBackButton, &QPushButton::clicked, this, [this]() {
        routeMapView_->clearRoute();
        emit navigationClosed();
        pages_->setCurrentWidget(navigationReturnPage_ != nullptr
                                     ? navigationReturnPage_
                                     : listPage_);
    });
    connect(routePlanButton_, &QPushButton::clicked, this, [this]() {
        const auto mode = static_cast<RouteMode>(routeModeCombo_->currentData().toInt());
        emit routeRequested(routeStartInput_->text(), mode);
    });
    connect(cancelOrderButton_, &QPushButton::clicked, this, [this]() {
        emit cancellationRequested(cancelOrderButton_->property("orderId").toLongLong());
    });
    connect(currentOrderNavigationButton_, &QPushButton::clicked, this, [this]() {
        emit currentOrderNavigationRequested(
            currentOrderNavigationButton_->property("stationId").toLongLong());
    });
    connect(reservationScanButton_, &QPushButton::clicked, this, [this]() {
        emit reservationScanRequested(
            reservationScanButton_->property("pileCode").toString());
    });
    connect(progressButton_, &QPushButton::clicked, this, [this]() {
        if (currentOrder_) emit reservationScanRequested(currentOrder_->pileCode);
    });
    // 结束充电前需用户在对话框中确认
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        if (confirmChargingStop(this)) {
            emit stopRequested(stopButton_->property("orderId").toLongLong());
        }
    });
    updateLocationSummary();
}

// 按当前位置与关键词组装电站查询参数
StationQuery StationBrowserPage::stationQuery() const
{
    StationQuery query;
    query.longitude = currentLocation_.longitude;
    query.latitude = currentLocation_.latitude;
    query.keyword = appliedKeyword_;
    return query;
}

MapLocation StationBrowserPage::currentLocation() const
{
    return currentLocation_;
}

void StationBrowserPage::setGreeting(const QString &nickname, bool isNewUser)
{
    setGreetingNickname(nickname);
    loginNoticeLabel_->setText(isNewUser ? QStringLiteral("账号已自动注册并登录")
                                         : QStringLiteral("登录成功"));
}

void StationBrowserPage::setGreetingNickname(const QString &nickname)
{
    welcomeLabel_->setText(QStringLiteral("你好，%1").arg(nickname));
    welcomeLabel_->setToolTip(welcomeLabel_->text());
}

// 列表加载中禁用输入并显示提示
void StationBrowserPage::setListLoading(bool loading)
{
    refreshButton_->setDisabled(loading);
    keywordInput_->setDisabled(loading);
    stationPreview_->setDisabled(loading);
    listMessageLabel_->setStyleSheet(QStringLiteral("color: #697969;"));
    listMessageLabel_->setText(loading ? QStringLiteral("正在获取充电站…") : QString{});
    listMessageLabel_->setVisible(loading);
}

// 预约请求进行中统一禁用相关按钮
void StationBrowserPage::setReservationBusy(bool busy)
{
    reservationBusy_ = busy;
    cancelOrderButton_->setDisabled(busy);
    currentOrderNavigationButton_->setDisabled(busy);
    reservationScanButton_->setDisabled(busy);
    progressButton_->setDisabled(busy);
    stopButton_->setDisabled(busy);
    for (QPushButton *button : reservationButtons_) {
        const bool canReserve = button->property("canReserve").toBool();
        button->setDisabled(busy || !canReserve);
    }
    for (QPushButton *button : directChargingButtons_) {
        const bool canStart = button->property("canStart").toBool();
        button->setDisabled(busy || !canStart);
    }
}

// 收到电站列表后重新过滤与渲染
void StationBrowserPage::showStations(const QList<protocol::StationDto> &stations)
{
    catalog_ = stations;
    setListLoading(false);
    applyDiscovery();
}

void StationBrowserPage::showListError(const QString &message)
{
    setListLoading(false);
    listMessageLabel_->setText(message);
    listMessageLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    listMessageLabel_->show();
    searchMessage_->setText(message);
}

void StationBrowserPage::showListMessage(const QString &message, bool error)
{
    actionMessageLabel_->setText(message);
    actionMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                              : QStringLiteral("color: #386a3c;"));
    actionMessageLabel_->setVisible(!message.isEmpty());
}

// 刷新当前订单卡片：文案与可用操作随状态变化
void StationBrowserPage::showCurrentOrder(
    const std::optional<protocol::OrderDto> &order)
{
    const bool changedOrder = !currentOrder_ || !order
        || currentOrder_->orderId != order->orderId || currentOrder_->status != order->status;
    currentOrder_ = order;
    if (changedOrder) currentOrderToggle_->setChecked(false);
    updateDirectChargingButtons();
    if (!order.has_value()) {
        currentOrderCard_->hide();
        layoutHomeOverlays();
        return;
    }

    currentOrderSummaryLabel_->setText(
        QStringLiteral("%1 · %2 · %3")
            .arg(order->stationName,
                 order->pileCode,
                 orderStatusText(order->status)));
    currentOrderToggle_->setText(QStringLiteral("ϟ 当前%1 · %2  ›")
        .arg(orderStatusText(order->status), order->stationName));
    currentOrderToggle_->setToolTip(currentOrderToggle_->text());
    // 预约状态附加过期提示，状态由服务端判定
    if (order->status == protocol::OrderStatus::Reserved) {
        currentOrderSummaryLabel_->setText(currentOrderSummaryLabel_->text()
                                           + QChar('\n') + reservationHint(*order));
        currentOrderToggle_->setToolTip(currentOrderToggle_->text()
                                        + QChar('\n') + reservationHint(*order));
    }
    cancelOrderButton_->setProperty("orderId", order->orderId);
    currentOrderNavigationButton_->setProperty("stationId", order->stationId);
    reservationScanButton_->setProperty("pileCode", order->pileCode);
    progressButton_->setProperty("orderId", order->orderId);
    stopButton_->setProperty("orderId", order->orderId);
    cancelOrderButton_->setVisible(order->status == protocol::OrderStatus::Reserved);
    currentOrderNavigationButton_->setVisible(
        order->status == protocol::OrderStatus::Reserved
        || order->status == protocol::OrderStatus::Charging);
    reservationScanButton_->setVisible(
        order->status == protocol::OrderStatus::Reserved);
    progressButton_->setVisible(order->status == protocol::OrderStatus::Charging);
    stopButton_->hide();
    currentOrderProgressLabel_->hide();
    // 待支付订单提示前往我的订单完成结算
    if (order->status == protocol::OrderStatus::PendingPayment) {
        currentOrderProgressLabel_->setText(
            QStringLiteral("待支付金额：%1\n请前往“我的 → 我的订单”完成结算。")
                .arg(formatMoney(order->amountCents)));
        currentOrderProgressLabel_->show();
    }
    cancelOrderButton_->setDisabled(reservationBusy_);
    currentOrderNavigationButton_->setDisabled(reservationBusy_);
    reservationScanButton_->setDisabled(reservationBusy_);
    progressButton_->setDisabled(reservationBusy_);
    stopButton_->setDisabled(reservationBusy_);
    currentOrderCard_->show();
    layoutHomeOverlays();
}

bool StationBrowserPage::isShowingStationDetail() const
{
    return pages_->currentWidget() == listPage_ && sheetPages_->currentWidget() == detailPage_;
}

// 返回地图概览并清除地图上的选中站
void StationBrowserPage::showListPage()
{
    navigationReturnPage_ = listPage_;
    stationMap_->selectStation(0);
    sheetPages_->setCurrentWidget(overviewScroll_);
    pages_->setCurrentWidget(listPage_);
}

void StationBrowserPage::showDetailLoading()
{
    pages_->setCurrentWidget(listPage_);
    sheetPages_->setCurrentWidget(detailPage_);
    setSheetPosition(2);
    backButton_->setEnabled(true);
    detailContent_->hide();
    detailMessageLabel_->setText(QStringLiteral("正在获取充电站详情…"));
    detailMessageLabel_->setStyleSheet(QStringLiteral("color: #697969;"));
    detailMessageLabel_->show();
}

// 渲染电站详情与各充电桩的预约、直接充电按钮
void StationBrowserPage::showStationDetail(const StationDetailPayload &detail)
{
    const bool alreadyShowingDetail = sheetPages_->currentWidget() == detailPage_;
    clearPileCards();
    navigationStation_ = detail.station;
    stationMap_->selectStation(detail.station.stationId);
    detailMessageLabel_->hide();
    detailNameLabel_->setText(detail.station.name);
    detailMetaLabel_->setText(
        QStringLiteral("%1\n%2\n空闲 %3/%4 · 在线率 %5%")
            .arg(detail.station.region,
                 detail.station.address)
            .arg(detail.station.availablePileCount)
            .arg(detail.station.totalPileCount)
            .arg(detail.station.onlineRatePercent, 0, 'f', 0));
    detailPriceLabel_->setText(
        QStringLiteral("当前参考单价：%1").arg(formatPrice(detail.station.priceCentsPerKwh)));
    detailPricingInfo_->setRules(pricingHint(detail.station));

    for (const auto &pile : detail.piles) {
        auto *card = createCard(detailContent_);
        card->setObjectName(QStringLiteral("pileCard_%1").arg(pile.pileCode));
        auto *layout = new QHBoxLayout(card);
        layout->setContentsMargins(14, 12, 14, 12);
        auto *description = new QLabel(
            QStringLiteral("%1\n%2 · %3 kW")
                .arg(pile.pileCode, pileTypeText(pile.pileType))
                .arg(pile.ratedPowerKw, 0, 'f', 1),
            card);
        auto *status = new QLabel(pileStatusText(pile.status), card);
        status->setObjectName(QStringLiteral("pileStatus_%1").arg(pile.pileCode));
        status->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                                  .arg(pileStatusColor(pile.status)));
        // 仅闲置桩可预约，忙碌时按钮置灰
        const bool canReserve = pile.status == protocol::PileStatus::Idle;
        auto *reserveButton = new QPushButton(
            canReserve ? QStringLiteral("预约") : QStringLiteral("不可预约"), card);
        reserveButton->setObjectName(QStringLiteral("reserveButton_%1").arg(pile.pileCode));
        reserveButton->setProperty("canReserve", canReserve);
        reserveButton->setDisabled(reservationBusy_ || !canReserve);
        connect(reserveButton, &QPushButton::clicked, this, [this, pile]() {
            emit reservationRequested(pile.pileCode);
        });
        reservationButtons_.append(reserveButton);
        auto *directChargingButton = new QPushButton(card);
        directChargingButton->setObjectName(
            QStringLiteral("directChargeButton_%1").arg(pile.pileCode));
        directChargingButton->setProperty("role", "primary");
        directChargingButton->setProperty("pileCode", pile.pileCode);
        directChargingButton->setProperty("pileIdle", canReserve);
        connect(directChargingButton,
                &QPushButton::clicked,
                this,
                [this, pile]() { emit directChargingRequested(pile.pileCode); });
        directChargingButtons_.append(directChargingButton);
        auto *rightLayout = new QVBoxLayout();
        rightLayout->addWidget(status, 0, Qt::AlignRight);
        rightLayout->addWidget(reserveButton, 0, Qt::AlignRight);
        rightLayout->addWidget(directChargingButton, 0, Qt::AlignRight);
        layout->addWidget(description, 1);
        layout->addLayout(rightLayout);
        pileListLayout_->addWidget(card);
    }
    updateDirectChargingButtons();
    detailContent_->show();
    if (!alreadyShowingDetail) {
        pages_->setCurrentWidget(listPage_);
        sheetPages_->setCurrentWidget(detailPage_);
        setSheetPosition(2);
    }
}

void StationBrowserPage::showDetailError(const QString &message)
{
    const bool alreadyShowingDetail = sheetPages_->currentWidget() == detailPage_;
    detailContent_->hide();
    detailMessageLabel_->setText(message);
    detailMessageLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    detailMessageLabel_->show();
    if (!alreadyShowingDetail) {
        pages_->setCurrentWidget(listPage_);
        sheetPages_->setCurrentWidget(detailPage_);
        setSheetPosition(2);
    }
}

void StationBrowserPage::showDetailMessage(const QString &message, bool error)
{
    detailMessageLabel_->setText(message);
    detailMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                              : QStringLiteral("color: #386a3c;"));
    detailMessageLabel_->setVisible(!message.isEmpty());
}

// 位置解析过程中禁用位置设置控件
void StationBrowserPage::setLocationBusy(bool busy)
{
    locationPresetCombo_->setDisabled(busy);
    locationAddressInput_->setDisabled(busy);
    resolveLocationButton_->setDisabled(busy);
    findChild<QPushButton *>("stationLocationDefault")->setDisabled(busy);
}

// 位置更新后清空旧距离并重算推荐与地图中心
void StationBrowserPage::setResolvedLocation(const MapLocation &location)
{
    currentLocation_ = location;
    const QSignalBlocker presetSignals(locationPresetCombo_);
    const int preset = locationPresetCombo_->findData(location.address);
    locationPresetCombo_->setCurrentIndex(preset >= 0 ? preset : locationPresetCombo_->count() - 1);
    locationAddressInput_->setText(location.address);
    for (auto &station : catalog_) station.distanceKm.reset();
    applyDiscovery();
    stationMap_->setCurrentLocation(location);
    stationMap_->setCenter(location);
    updateLocationSummary();
}

void StationBrowserPage::showLocationMessage(const QString &message, bool error)
{
    locationMessageLabel_->setText(message);
    locationMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                                : QStringLiteral("color: #386a3c;"));
    locationMessageLabel_->setVisible(!message.isEmpty());
}

// 进入导航页并预填起点与目的地
void StationBrowserPage::showNavigation(const protocol::StationDto &station,
                                        const MapLocation &start)
{
    navigationStation_ = station;
    routeStartInput_->setText(start.address);
    routeDestinationLabel_->setText(
        QStringLiteral("%1 · %2").arg(station.name, station.address));
    routeDisplayLabel_->setText(QStringLiteral("选择出行方式后点击“开始导航”"));
    routeMapView_->clearRoute();
    if (routeMapView_->isPreloaded()) {
        routeDisplayStack_->setCurrentWidget(routeMapView_);
    } else {
        routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
    }
    routeSummaryLabel_->hide();
    routeDetailsButton_->setChecked(false);
    routeDetailsButton_->setEnabled(false);
    routeMessageLabel_->hide();
    pages_->setCurrentWidget(navigationPage_);
    if (routeMapView_->isPreloaded()) routeMapView_->prepareMap();
}

void StationBrowserPage::preloadMap(const QUrl &scriptUrl)
{
    routeMapView_->preload(scriptUrl);
}

// 路线请求中显示规划提示并锁定控件
void StationBrowserPage::setRouteBusy(bool busy)
{
    routeRequestBusy_ = busy;
    if (busy) {
        routeMapView_->clearRoute();
        routeDisplayLabel_->setText(QStringLiteral("正在规划路线…"));
        if (routeMapView_->isPreloaded()) {
            routeDisplayStack_->setCurrentWidget(routeMapView_);
            routeMapView_->prepareMap();
        } else {
            routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
        }
        routeSummaryLabel_->hide();
        routeDetailsButton_->setChecked(false);
        routeDetailsButton_->setEnabled(false);
    }
    updateRouteControls();
}

void StationBrowserPage::updateRouteControls()
{
    const bool busy = routeRequestBusy_ || mapLoading_;
    routeStartInput_->setDisabled(busy);
    routeModeCombo_->setDisabled(busy);
    routePlanButton_->setDisabled(busy);
    routePlanButton_->setText(busy ? QStringLiteral("加载中…") : QStringLiteral("开始导航"));
}

void StationBrowserPage::showRouteMessage(const QString &message, bool error)
{
    routeMessageLabel_->setText(message);
    routeMessageLabel_->setToolTip(message);
    routeMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                             : QStringLiteral("color: #386a3c;"));
    routeMessageLabel_->setVisible(!message.isEmpty());
}

// 展示路线结果：摘要、分步说明与地图折线
void StationBrowserPage::showRouteResult(const RouteResult &result)
{
    showRouteMessage(result.message);
    routeDisplayLabel_->setText(result.summary);
    routeDetails_->setPlainText(result.instructions.isEmpty()
        ? QStringLiteral("服务未返回分步说明，请参考地图路线。")
        : result.instructions.join(QStringLiteral("\n\n")));
    routeDetailsButton_->setEnabled(!result.instructions.isEmpty() || !result.paths.isEmpty());
    if (result.paths.isEmpty()) {
        routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
        return;
    }
    routeSummaryLabel_->setText(result.summary);
    routeSummaryLabel_->show();
    routeDisplayStack_->setCurrentWidget(routeMapView_);
    routeMapView_->setRoute(result);
}

// 退出登录等场景下重置页面到初始状态
void StationBrowserPage::reset()
{
    detailPricingInfo_->setRules({});
    setListLoading(false);
    userId_ = 0;
    searchHistory_.clear();
    visitedStationIds_.clear();
    visitHistoryFailed_ = false;
    catalog_.clear();
    appliedKeyword_.clear();
    keywordInput_->clear();
    currentLocation_ = {QStringLiteral("演示位置"), 123.42, 41.70};
    locationPresetCombo_->setCurrentIndex(0);
    locationAddressInput_->setText(currentLocation_.address);
    stationMap_->setCurrentLocation(currentLocation_);
    updateLocationSummary();
    sheetPages_->setCurrentWidget(overviewScroll_);
    setSheetPosition(1);
    stations_.clear();
    renderDiscovery();
    stationMap_->setStations({});
    stationPreview_->hide();
    currentOrderToggle_->setChecked(false);
    stationCountLabel_->clear();
    clearPileCards();
    listMessageLabel_->hide();
    actionMessageLabel_->hide();
    currentOrderCard_->hide();
    currentOrder_.reset();
    detailMessageLabel_->hide();
    locationMessageLabel_->hide();
    routeMessageLabel_->hide();
    routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
    routeMapView_->clearRoute();
    routeSummaryLabel_->hide();
    routeDetailsButton_->setChecked(false);
    routeDetailsButton_->setEnabled(false);
    setLocationBusy(false);
    setRouteBusy(false);
    pages_->setCurrentWidget(listPage_);
}

// 清空电桩卡片列表，同时释放按钮引用避免悬空指针
void StationBrowserPage::clearPileCards()
{
    reservationButtons_.clear();
    directChargingButtons_.clear();
    while (QLayoutItem *item = pileListLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

// 根据桩是否闲置或为本人预约，刷新直接充电按钮文案与可用性
void StationBrowserPage::updateDirectChargingButtons()
{
    for (QPushButton *button : directChargingButtons_) {
        const QString pileCode = button->property("pileCode").toString();
        const bool ownReservation = currentOrder_.has_value()
            && currentOrder_->status == protocol::OrderStatus::Reserved
            && currentOrder_->pileCode == pileCode;
        const bool canStart = button->property("pileIdle").toBool()
            || ownReservation;
        button->setProperty("canStart", canStart);
        button->setText(ownReservation ? QStringLiteral("开始充电")
                                       : canStart ? QStringLiteral("直接充电")
                                                  : QStringLiteral("不可充电"));
        button->setAccessibleName(
            canStart ? QStringLiteral("使用%1开始充电").arg(pileCode)
                     : QStringLiteral("%1当前不可开始充电").arg(pileCode));
        button->setToolTip(
            ownReservation ? QStringLiteral("进入充电页确认已预约的充电桩")
                           : canStart ? QStringLiteral("进入充电页确认此充电桩")
                                      : QStringLiteral("只有闲置或本人已预约的充电桩可以开始充电"));
        button->setDisabled(reservationBusy_ || !canStart);
    }
}

// 刷新当前定位文字，说明该位置用于算距离和路线起点
void StationBrowserPage::updateLocationSummary()
{
    locationCaption_->setText(currentLocation_.address == QStringLiteral("演示位置")
        ? QStringLiteral("默认定位（演示）") : currentLocation_.address);
    locationCaption_->setToolTip(locationCaption_->text());
    locationSummaryLabel_->setText(QStringLiteral("当前位置：%1\n此位置用于附近电站距离与路线起点。")
        .arg(currentLocation_.address));
}

}  // namespace charging::client
