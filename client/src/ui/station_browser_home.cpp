#include "ui/station_browser_page.h"
#include "ui/station_map_view.h"
#include "ui/station_preview_card.h"
#include "ui/client_theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTouchEvent>

// 本文件搭建电站首页地图、底部抽屉与位置设置界面
namespace charging::client {

// 页面未显示时先强制布局，保证地图预加载尺寸正确
void StationBrowserPage::prepareHomeMap(const QSize &availableSize)
{
    if (!isVisible()) {
        ensurePolished();
        resize(availableSize);
        layout()->activate();
        pages_->resize(contentsRect().size());
        listPage_->resize(pages_->contentsRect().size());
        listPage_->layout()->activate();
        stationMap_->resize(listPage_->contentsRect().size());
        layoutHomeOverlays();
    }
    stationMap_->preload();
}

// 构建地图首页：地图、可拖动底部面板与搜索入口
void StationBrowserPage::setupMapHome()
{
    stationMap_ = new StationMapView(listPage_);
    listPage_->layout()->addWidget(stationMap_);
    homeOverlay_ = new QFrame(listPage_);
    homeOverlay_->setObjectName("stationHomeOverlay");
    auto *homeLayout = new QVBoxLayout(homeOverlay_);
    homeLayout->setContentsMargins(12, 0, 12, 12);
    homeLayout->setSpacing(6);
    homeLayout->setSizeConstraint(QLayout::SetNoConstraint);
    sheetHandle_ = new QPushButton(QStringLiteral("━━━━"), homeOverlay_);
    sheetHandle_->setObjectName("stationSheetHandle");
    sheetHandle_->setAccessibleName(QStringLiteral("拖动展开或收起电站面板，点击切换高度"));
    sheetHandle_->setFixedHeight(24);
    sheetHandle_->setCursor(Qt::SizeVerCursor);
    sheetHandle_->setAttribute(Qt::WA_AcceptTouchEvents);
    sheetHandle_->installEventFilter(this);
    homeLayout->addWidget(sheetHandle_);
    homeSearchButton_ = new QPushButton(QStringLiteral("⌕  搜索电站、区域或地址"), homeOverlay_);
    homeSearchButton_->setObjectName("stationSearchEntry");
    homeSearchButton_->setMinimumHeight(44);
    connect(homeSearchButton_, &QPushButton::clicked, this, &StationBrowserPage::openSearch);
    homeLayout->addWidget(homeSearchButton_);
    // 抽屉内用堆叠页在概览列表与电站预览间切换
    sheetPages_ = new QStackedWidget(homeOverlay_);
    sheetPages_->setObjectName("stationSheetPages");
    sheetPages_->setMinimumSize(0, 0);
    sheetPages_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    homeLayout->addWidget(sheetPages_, 1);
    overviewScroll_ = new QScrollArea(sheetPages_);
    overviewScroll_->setObjectName("stationDiscoveryScroll");
    overviewScroll_->setWidgetResizable(true);
    overviewScroll_->setFrameShape(QFrame::NoFrame);
    overviewScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(overviewScroll_->viewport(), QScroller::TouchGesture);
    overviewContent_ = new QWidget;
    auto *overviewLayout = new QVBoxLayout(overviewContent_);
    overviewLayout->setContentsMargins(0, 0, 0, 8);
    overviewLayout->setSpacing(8);
    overviewScroll_->setWidget(overviewContent_);
    sheetPages_->addWidget(overviewScroll_);
    welcomeLabel_ = new QLabel(this); welcomeLabel_->hide();
    welcomeLabel_->setObjectName("welcomeLabel");
    loginNoticeLabel_ = new QLabel(this); loginNoticeLabel_->hide();
    loginNoticeLabel_->setObjectName("loginNoticeLabel");
    auto *caption = new QHBoxLayout;
    locationCaption_ = new QLabel(overviewContent_);
    locationCaption_->setObjectName("stationLocationCaption");
    locationCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    stationCountLabel_ = new QLabel(overviewContent_);
    stationCountLabel_->setObjectName("stationResultCount");
    auto *homeLocation = new QPushButton(overviewContent_);
    homeLocation->setObjectName("stationHomeLocationButton");
    homeLocation->setIcon(clientNavigationIcon(NavigationIcon::Location));
    homeLocation->setIconSize(QSize(24,24));
    homeLocation->setFixedSize(44,44);
    homeLocation->setAccessibleName(QStringLiteral("修改当前位置"));
    homeLocation->setToolTip(homeLocation->accessibleName());
    connect(homeLocation, &QPushButton::clicked, this, &StationBrowserPage::openLocationSettings);
    caption->addWidget(locationCaption_, 1);
    caption->addWidget(stationCountLabel_);
    caption->addWidget(homeLocation);
    overviewLayout->addLayout(caption);

    // 搭建搜索页：返回、关键词输入与结果滚动区
    searchPage_ = new QWidget(pages_);
    searchPage_->setObjectName("stationSearchPage");
    auto *searchLayout = new QVBoxLayout(searchPage_);
    searchLayout->setContentsMargins(16, 14, 16, 12);
    auto *searchRow = new QHBoxLayout;
    auto *searchBack = new QPushButton(QStringLiteral("‹"), searchPage_);
    searchBack->setObjectName("stationSearchBack");
    searchBack->setAccessibleName(QStringLiteral("返回地图"));
    searchBack->setFixedSize(44, 44);
    keywordInput_ = new QLineEdit(searchPage_);
    keywordInput_->setObjectName("stationKeywordInput");
    keywordInput_->setPlaceholderText(QStringLiteral("搜索电站、区域或地址"));
    keywordInput_->setClearButtonEnabled(true);
    keywordInput_->setMinimumWidth(0);
    refreshButton_ = new QPushButton(QStringLiteral("搜索"), searchPage_);
    refreshButton_->setObjectName("stationRefreshButton");
    refreshButton_->setFixedWidth(48);
    locationEntry_ = new QPushButton(searchPage_);
    locationEntry_->setObjectName("stationLocationEntry");
    locationEntry_->setIcon(clientNavigationIcon(NavigationIcon::Location));
    locationEntry_->setIconSize(QSize(24,24));
    locationEntry_->setFixedSize(44,44);
    locationEntry_->setAccessibleName(QStringLiteral("修改当前位置"));
    locationEntry_->setToolTip(locationEntry_->accessibleName());

    searchRow->addWidget(searchBack);
    searchRow->addWidget(keywordInput_, 1);
    searchRow->addWidget(refreshButton_);
    searchLayout->addLayout(searchRow);
    searchLayout->addWidget(locationEntry_, 0, Qt::AlignRight);
    searchMessage_ = new QLabel(searchPage_);
    searchMessage_->setObjectName("stationSearchMessage");
    searchMessage_->setWordWrap(true);
    searchLayout->addWidget(searchMessage_);
    auto *searchScroll = new QScrollArea(searchPage_);
    searchScroll->setWidgetResizable(true);
    searchScroll->setFrameShape(QFrame::NoFrame);
    searchScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    searchContent_ = new QWidget;
    searchContent_->setObjectName("stationSearchContent");
    auto *resultsLayout = new QVBoxLayout(searchContent_);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    searchScroll->setWidget(searchContent_);
    QScroller::grabGesture(searchScroll->viewport(), QScroller::TouchGesture);
    searchLayout->addWidget(searchScroll, 1);
    pages_->addWidget(searchPage_);
    connect(searchBack, &QPushButton::clicked, this, [this] {
        pages_->setCurrentWidget(listPage_);
        keywordInput_->setText(appliedKeyword_);
    });
    connect(keywordInput_, &QLineEdit::textChanged, this, [this] { renderSearch(); });
    connect(locationEntry_, &QPushButton::clicked, this, &StationBrowserPage::openLocationSettings);

    // 搭建位置设置页：预设地点、手动地址与确定按钮
    locationPage_ = new QWidget(pages_);
    locationPage_->setObjectName("stationLocationPage");
    auto *locationLayout = new QVBoxLayout(locationPage_);
    auto *locationBack = new QPushButton(QStringLiteral("‹ 返回"), locationPage_);
    locationBack->setAccessibleDescription(QStringLiteral("返回地图或搜索页"));
    locationBack->setObjectName("stationLocationBack");
    locationLayout->addWidget(locationBack, 0, Qt::AlignLeft);
    locationScroll_ = new QScrollArea(locationPage_);
    locationScroll_->setObjectName(QStringLiteral("stationLocationScrollArea"));
    locationScroll_->setWidgetResizable(true);
    locationScroll_->setFrameShape(QFrame::NoFrame);
    locationScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    locationContent_ = new QWidget(locationScroll_);
    locationContent_->setObjectName(QStringLiteral("stationLocationContent"));
    auto *advancedLayout = new QVBoxLayout(locationContent_);
    advancedLayout->setContentsMargins(12, 10, 12, 10);
    advancedLayout->setSpacing(8);
    auto *locationTitle = new QLabel(QStringLiteral("修改当前位置"), locationContent_);
    locationTitle->setObjectName(QStringLiteral("stationLocationTitle"));
    locationSummaryLabel_ = new QLabel(locationContent_);
    locationSummaryLabel_->setObjectName(QStringLiteral("stationLocationSummary"));
    locationSummaryLabel_->setWordWrap(true);
    locationSummaryLabel_->setTextFormat(Qt::PlainText);
    locationPresetCombo_ = new QComboBox(locationContent_);
    locationPresetCombo_->setObjectName(QStringLiteral("locationPresetCombo"));
    locationPresetCombo_->addItem(QStringLiteral("默认定位（演示）"), QStringLiteral("演示位置"));
    locationPresetCombo_->addItem(QStringLiteral("和平区"), QStringLiteral("沈阳市和平区"));
    locationPresetCombo_->addItem(QStringLiteral("浑南区"), QStringLiteral("沈阳市浑南区"));
    locationPresetCombo_->addItem(QStringLiteral("手动输入地址"), QString{});
    locationAddressInput_ = new QLineEdit(locationContent_);
    locationAddressInput_->setObjectName(QStringLiteral("locationAddressInput"));
    locationAddressInput_->setPlaceholderText(QStringLiteral("输入城市和具体位置"));
    locationAddressInput_->setText(QStringLiteral("演示位置"));
    locationAddressInput_->setMinimumWidth(0);
    resolveLocationButton_ = new QPushButton(QStringLiteral("确定位置"), locationContent_);
    resolveLocationButton_->setObjectName(QStringLiteral("resolveLocationButton"));
    auto *locationInputRow = new QHBoxLayout;
    locationInputRow->addWidget(locationAddressInput_, 1);
    locationInputRow->addWidget(resolveLocationButton_);
    locationMessageLabel_ = new QLabel(locationContent_);
    locationMessageLabel_->setObjectName(QStringLiteral("locationMessage"));
    locationMessageLabel_->setTextFormat(Qt::PlainText);
    locationMessageLabel_->setWordWrap(true);
    locationMessageLabel_->hide();
    locationTitle->setProperty("role", "discoveryHeading");
    advancedLayout->addWidget(locationTitle);
    advancedLayout->addWidget(locationSummaryLabel_);

    advancedLayout->addWidget(locationPresetCombo_);
    auto *locationHint = new QLabel(QStringLiteral("没有设备定位时，可输入城市和地址。点击“确定位置”立即更新附近距离与路线起点。"), locationContent_);
    locationHint->setObjectName(QStringLiteral("locationInputHint"));
    locationHint->setWordWrap(true);
    advancedLayout->addWidget(locationHint);
    advancedLayout->addLayout(locationInputRow);
    advancedLayout->addWidget(locationMessageLabel_);
    advancedLayout->addStretch();
    locationScroll_->setWidget(locationContent_);

    locationLayout->addWidget(locationScroll_, 1);

    // 恢复默认位置按钮会请求解析演示位置
    auto *restore = new QPushButton(QStringLiteral("恢复默认位置"), locationPage_);
    restore->setObjectName("stationLocationDefault");
    locationLayout->addWidget(restore);
    pages_->addWidget(locationPage_);
    connect(locationBack, &QPushButton::clicked, this, [this] { pages_->setCurrentWidget(locationReturnPage_); });
    connect(restore, &QPushButton::clicked, this, [this] { emit locationResolutionRequested(QStringLiteral("演示位置")); });
    // 当前订单卡片，可折叠展开导航、取消、充电等操作
    currentOrderCard_ = new QFrame(overviewContent_);
    currentOrderCard_->setObjectName(QStringLiteral("currentOrderCard"));
    auto *currentOrderLayout = new QVBoxLayout(currentOrderCard_);
    currentOrderLayout->setContentsMargins(10, 3, 10, 5);
    currentOrderLayout->setSpacing(5);
    currentOrderToggle_ = new QPushButton(currentOrderCard_);
    currentOrderToggle_->setObjectName(QStringLiteral("currentOrderToggle"));
    currentOrderToggle_->setCheckable(true);
    currentOrderToggle_->setFlat(true);
    currentOrderToggle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    currentOrderToggle_->setAccessibleName(QStringLiteral("展开当前订单操作"));
    currentOrderDetails_ = new QWidget(currentOrderCard_);
    currentOrderDetails_->setObjectName(QStringLiteral("currentOrderDetails"));
    auto *orderDetailsLayout = new QVBoxLayout(currentOrderDetails_);
    orderDetailsLayout->setContentsMargins(2, 0, 2, 6);
    orderDetailsLayout->setSpacing(6);
    currentOrderSummaryLabel_ = new QLabel(currentOrderDetails_);
    currentOrderSummaryLabel_->setObjectName(QStringLiteral("currentOrderSummary"));
    currentOrderSummaryLabel_->setTextFormat(Qt::PlainText);
    currentOrderSummaryLabel_->setWordWrap(true);
    currentOrderSummaryLabel_->setMaximumHeight(44);
    currentOrderProgressLabel_ = new QLabel(currentOrderDetails_);
    currentOrderProgressLabel_->setObjectName(QStringLiteral("currentOrderProgress"));
    currentOrderProgressLabel_->setWordWrap(true);
    cancelOrderButton_ = new QPushButton(QStringLiteral("取消预约"), currentOrderDetails_);
    cancelOrderButton_->setObjectName(QStringLiteral("cancelReservationButton"));
    currentOrderNavigationButton_ = new QPushButton(QStringLiteral("导航"), currentOrderDetails_);
    currentOrderNavigationButton_->setObjectName(QStringLiteral("currentOrderNavigationButton"));
    reservationScanButton_ = new QPushButton(QStringLiteral("前往充电"), currentOrderDetails_);
    reservationScanButton_->setObjectName(QStringLiteral("startReservedChargingButton"));
    progressButton_ = new QPushButton(QStringLiteral("查看充电"), currentOrderDetails_);
    progressButton_->setObjectName(QStringLiteral("chargingProgressButton"));
    stopButton_ = new QPushButton(QStringLiteral("结束充电"), currentOrderDetails_);
    stopButton_->setObjectName(QStringLiteral("chargingStopButton"));
    auto *currentOrderActions = new QHBoxLayout;
    currentOrderActions->setSpacing(5);
    for (auto *button : {currentOrderNavigationButton_, cancelOrderButton_,
                         reservationScanButton_, progressButton_, stopButton_}) {
        currentOrderActions->addWidget(button);
    }
    orderDetailsLayout->addWidget(currentOrderSummaryLabel_);
    orderDetailsLayout->addWidget(currentOrderProgressLabel_);
    orderDetailsLayout->addLayout(currentOrderActions);
    currentOrderLayout->addWidget(currentOrderToggle_);
    currentOrderLayout->addWidget(currentOrderDetails_);
    currentOrderDetails_->hide();
    currentOrderCard_->hide();
    overviewLayout->addWidget(currentOrderCard_);
    // 展开状态变化后重新计算首页浮层高度
    connect(currentOrderToggle_, &QPushButton::toggled, this, [this](bool expanded) {
        if (expanded) locationEntry_->setChecked(false);
        currentOrderDetails_->setVisible(expanded);
        currentOrderToggle_->setAccessibleName(expanded ? QStringLiteral("收起当前订单操作")
                                                       : QStringLiteral("展开当前订单操作"));
        layoutHomeOverlays();
    });

    // 两个提示标签分别显示操作结果和列表状态
    actionMessageLabel_ = new QLabel(overviewContent_);
    actionMessageLabel_->setObjectName(QStringLiteral("stationActionMessage"));
    listMessageLabel_ = new QLabel(overviewContent_);
    listMessageLabel_->setObjectName(QStringLiteral("stationListMessage"));
    for (auto *label : {actionMessageLabel_, listMessageLabel_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setMaximumHeight(48);
        label->hide();
        overviewLayout->addWidget(label);
    }

    discoveryList_ = new QWidget(overviewContent_);
    discoveryList_->setObjectName("stationDiscoveryList");
    auto *discoveryLayout = new QVBoxLayout(discoveryList_);
    discoveryLayout->setContentsMargins(0, 0, 0, 0);
    overviewLayout->addWidget(discoveryList_);
    overviewLayout->addStretch();
    // 接入地图交互信号：拖动收起面板、点击空白切全屏
    stationPreview_ = new StationPreviewCard(sheetPages_);
    sheetPages_->addWidget(stationPreview_);
    connect(stationMap_, &StationMapView::interactionStarted, this, [this] {
        if (!sheetHidden_ && sheetPosition_ != 0) setSheetPosition(0);
    });
    connect(stationMap_, &StationMapView::backgroundClicked, this, [this] {
        if (sheetHidden_) setSheetPosition(1);
        else { sheetHidden_ = true; sheetDragging_ = false; emit mapFullscreenChanged(true); layoutHomeOverlays(); }
    });
    connect(stationMap_, &StationMapView::stationSelected, this, &StationBrowserPage::previewStation);
    connect(stationPreview_, &StationPreviewCard::dismissed, this, &StationBrowserPage::detailBackRequested);
    connect(stationPreview_, &StationPreviewCard::detailsRequested, this, &StationBrowserPage::stationSelected);
    connect(stationPreview_, &StationPreviewCard::navigationRequested, this, [this](const auto &station) {
        navigationReturnPage_ = listPage_;
        emit navigationRequested(station);
    });
    listPage_->installEventFilter(this);
    stationMap_->setCurrentLocation(currentLocation_);
}

// 设置地图脚本地址，供地图视图加载
void StationBrowserPage::configureHomeMap(const QUrl &scriptUrl)
{
    stationMap_->setMapScriptUrl(scriptUrl);
}

// 选中电站后弹出预览卡并把地图对准该站
void StationBrowserPage::previewStation(qint64 stationId)
{
    if (stationId <= 0) { emit detailBackRequested(); return; }
    emit detailBackRequested();
    stationMap_->selectStation(stationId);
    for (const auto &station : stations_) {
        if (station.stationId != stationId) continue;
        stationPreview_->setStation(station);
        sheetPages_->setCurrentWidget(stationPreview_);
        pages_->setCurrentWidget(listPage_);
        setSheetPosition(1);
        stationMap_->focusStation(stationId);
        return;
    }
}

// 设置抽屉档位：0收起、1常态、2展开
void StationBrowserPage::setSheetPosition(int position)
{
    const bool wasFullscreen = sheetHidden_;
    sheetHidden_ = false;
    sheetPosition_ = qBound(0, position, 2);
    sheetDragging_ = false;
    if (wasFullscreen) emit mapFullscreenChanged(false);
    layoutHomeOverlays();
}

// 事件过滤器处理抽屉把手的鼠标、触摸与键盘拖动
bool StationBrowserPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == sheetHandle_) {
        int y = 0;
        bool begin = false, move = false, end = false;
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (event->type() != QEvent::MouseMove && mouse->button() != Qt::LeftButton) return false;
            y = mouse->globalPosition().toPoint().y();
            begin = event->type() == QEvent::MouseButtonPress;
            move = event->type() == QEvent::MouseMove && sheetDragging_;
            end = event->type() == QEvent::MouseButtonRelease;
        } else if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
                   || event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
            auto *touch = static_cast<QTouchEvent *>(event);
            if (!touch->points().isEmpty()) y = touch->points().first().globalPosition().toPoint().y();
            begin = event->type() == QEvent::TouchBegin;
            move = event->type() == QEvent::TouchUpdate;
            end = event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel;
            event->accept();
        } else if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Space || key->key() == Qt::Key_Return) {
                setSheetPosition((sheetPosition_ + 1) % 3); return true;
            }
        }
        if (begin) {
            sheetDragging_ = true; dragStartY_ = y; dragStartHeight_ = homeOverlay_->height();
            dragHeight_ = dragStartHeight_; return true;
        }
        if (move && sheetDragging_) {
            dragHeight_ = dragStartHeight_ + dragStartY_ - y; layoutHomeOverlays(); return true;
        }
        // 松手后，轻点循环切档，拖动按方向升降一档
        if (end && sheetDragging_) {
            const int delta = dragHeight_ - dragStartHeight_;
            setSheetPosition(qAbs(delta) < 12 ? (sheetPosition_ + 1) % 3
                                             : sheetPosition_ + (delta > 0 ? 1 : -1));
            return true;
        }
    }
    // 首页尺寸变化时重新排布浮层
    if (watched == listPage_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        layoutHomeOverlays();
    return QWidget::eventFilter(watched, event);
}

// 记录底部导航高度，避免浮层被遮挡
void StationBrowserPage::setBottomNavigationInset(int inset)
{
    if (bottomNavigationInset_ == inset) return;
    bottomNavigationInset_ = inset;
    layoutHomeOverlays();
}

// 按当前档位计算抽屉高度并同步地图可视边距
void StationBrowserPage::layoutHomeOverlays()
{
    if (!stationPreview_) return;
    const int h = listPage_->height() - bottomNavigationInset_;
    const int collapsed = 92;
    const int expanded = qMax(collapsed, h - 170);
    const int previewHeight = sheetPages_->currentWidget() == stationPreview_
        ? stationPreview_->minimumSizeHint().height() + collapsed : 0;
    const int normal = qBound(collapsed, qMax(h * 46 / 100, previewHeight), expanded);
    const int target = sheetDragging_ ? qBound(collapsed, dragHeight_, expanded)
                                     : sheetPosition_ == 0 ? collapsed : sheetPosition_ == 1 ? normal : expanded;
    sheetPages_->setVisible(target > collapsed + 15);
    homeOverlay_->setGeometry(8, h - target - 28, qMax(0, listPage_->width() - 16), target);
    homeOverlay_->setVisible(!sheetHidden_);
    homeOverlay_->raise();
    stationMap_->setViewportMargins(QMargins(24, 24, 112, (sheetHidden_ ? 32 : target + 40) + bottomNavigationInset_));
}

} // namespace charging::client
