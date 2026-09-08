#include "ui/station_browser_page.h"

#include "ui/station_map_view.h"
#include "ui/station_preview_card.h"
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace charging::client {

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

void StationBrowserPage::setupMapHome()
{
    stationMap_ = new StationMapView(listPage_);
    listPage_->layout()->addWidget(stationMap_);
    homeOverlay_ = new QWidget(listPage_);
    homeOverlay_->setObjectName(QStringLiteral("stationHomeOverlay"));
    auto *homeLayout = new QVBoxLayout(homeOverlay_);
    homeLayout->setContentsMargins(0, 0, 0, 0);
    homeLayout->setSpacing(8);
    auto *brandRow = new QHBoxLayout;
    auto *brand = new QLabel(QStringLiteral("BIT / CHARGE  悦充"), homeOverlay_);
    brand->setObjectName(QStringLiteral("stationHomeBrand"));
    welcomeLabel_ = new QLabel(homeOverlay_);
    welcomeLabel_->setObjectName(QStringLiteral("welcomeLabel"));
    welcomeLabel_->setTextFormat(Qt::PlainText);
    welcomeLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    welcomeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    loginNoticeLabel_ = new QLabel(homeOverlay_);
    loginNoticeLabel_->setObjectName(QStringLiteral("loginNoticeLabel"));
    loginNoticeLabel_->hide();
    brandRow->addWidget(brand);
    brandRow->addWidget(welcomeLabel_, 1);
    homeLayout->addLayout(brandRow);

    auto *queryCard = new QFrame(homeOverlay_);
    queryCard->setObjectName(QStringLiteral("stationQueryCard"));
    queryCard->setProperty("role", "mapCard");
    auto *searchShadow = new QGraphicsDropShadowEffect(queryCard);
    searchShadow->setBlurRadius(14);
    searchShadow->setOffset(0, 2);
    searchShadow->setColor(QColor(32, 61, 48, 20));
    queryCard->setGraphicsEffect(searchShadow);
    auto *searchRow = new QHBoxLayout(queryCard);
    searchRow->setContentsMargins(8, 7, 8, 7);
    searchRow->setSpacing(6);
    auto *searchIcon = new QLabel(QStringLiteral("⌕"), queryCard);
    searchIcon->setObjectName(QStringLiteral("stationSearchIcon"));
    searchIcon->setFixedWidth(18);
    searchRow->addWidget(searchIcon);
    keywordInput_ = new QLineEdit(queryCard);
    keywordInput_->setObjectName(QStringLiteral("stationKeywordInput"));
    keywordInput_->setPlaceholderText(QStringLiteral("搜索充电站、区域或地址"));
    keywordInput_->setAccessibleName(QStringLiteral("站名或地址关键词，例如和平"));
    keywordInput_->setMinimumWidth(0);
    refreshButton_ = new QPushButton(QStringLiteral("搜索"), queryCard);
    refreshButton_->setObjectName(QStringLiteral("stationRefreshButton"));
    refreshButton_->setFixedWidth(46);
    filterToggle_ = new QPushButton(QStringLiteral("筛选"), queryCard);
    filterToggle_->setObjectName(QStringLiteral("stationFilterToggle"));
    filterToggle_->setCheckable(true);
    filterToggle_->setFixedWidth(52);
    filterToggle_->setAccessibleName(QStringLiteral("展开位置与区域筛选"));
    searchRow->addWidget(keywordInput_, 1);
    searchRow->addWidget(refreshButton_);
    searchRow->addWidget(filterToggle_);
    homeLayout->addWidget(queryCard);

    auto *captionRow = new QHBoxLayout;
    locationCaption_ = new QLabel(homeOverlay_);
    locationCaption_->setObjectName(QStringLiteral("stationLocationCaption"));
    locationCaption_->setTextFormat(Qt::PlainText);
    locationCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    stationCountLabel_ = new QLabel(homeOverlay_);
    stationCountLabel_->setObjectName(QStringLiteral("stationResultCount"));
    captionRow->addWidget(locationCaption_, 1);
    captionRow->addWidget(stationCountLabel_);
    homeLayout->addLayout(captionRow);

    filterScroll_ = new QScrollArea(homeOverlay_);
    filterScroll_->setObjectName(QStringLiteral("stationFilterScrollArea"));
    filterScroll_->setWidgetResizable(true);
    filterScroll_->setFrameShape(QFrame::NoFrame);
    filterScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    advancedFilters_ = new QWidget(filterScroll_);
    advancedFilters_->setObjectName(QStringLiteral("stationAdvancedFilters"));
    auto *advancedLayout = new QVBoxLayout(advancedFilters_);
    advancedLayout->setContentsMargins(12, 10, 12, 10);
    advancedLayout->setSpacing(8);
    auto *locationTitle = new QLabel(QStringLiteral("当前位置"), advancedFilters_);
    locationTitle->setObjectName(QStringLiteral("stationLocationTitle"));
    locationSummaryLabel_ = new QLabel(advancedFilters_);
    locationSummaryLabel_->setObjectName(QStringLiteral("stationLocationSummary"));
    locationSummaryLabel_->setWordWrap(true);
    locationSummaryLabel_->setTextFormat(Qt::PlainText);
    demoLocationCheck_ = new QCheckBox(QStringLiteral("使用当前选定位置计算距离"), advancedFilters_);
    demoLocationCheck_->setObjectName(QStringLiteral("demoLocationCheck"));
    demoLocationCheck_->setChecked(true);
    locationPresetCombo_ = new QComboBox(advancedFilters_);
    locationPresetCombo_->setObjectName(QStringLiteral("locationPresetCombo"));
    locationPresetCombo_->addItem(QStringLiteral("演示当前位置"), QStringLiteral("演示位置"));
    locationPresetCombo_->addItem(QStringLiteral("和平区"), QStringLiteral("沈阳市和平区"));
    locationPresetCombo_->addItem(QStringLiteral("浑南区"), QStringLiteral("沈阳市浑南区"));
    locationPresetCombo_->addItem(QStringLiteral("手动输入地址"), QString{});
    locationAddressInput_ = new QLineEdit(advancedFilters_);
    locationAddressInput_->setObjectName(QStringLiteral("locationAddressInput"));
    locationAddressInput_->setPlaceholderText(QStringLiteral("输入城市和具体位置"));
    locationAddressInput_->setText(QStringLiteral("演示位置"));
    locationAddressInput_->setMinimumWidth(0);
    resolveLocationButton_ = new QPushButton(QStringLiteral("确定位置"), advancedFilters_);
    resolveLocationButton_->setObjectName(QStringLiteral("resolveLocationButton"));
    auto *locationInputRow = new QHBoxLayout;
    locationInputRow->addWidget(locationAddressInput_, 1);
    locationInputRow->addWidget(resolveLocationButton_);
    locationMessageLabel_ = new QLabel(advancedFilters_);
    locationMessageLabel_->setObjectName(QStringLiteral("locationMessage"));
    locationMessageLabel_->setTextFormat(Qt::PlainText);
    locationMessageLabel_->setWordWrap(true);
    locationMessageLabel_->hide();
    regionInput_ = new QLineEdit(advancedFilters_);
    regionInput_->setObjectName(QStringLiteral("stationRegionInput"));
    regionInput_->setPlaceholderText(QStringLiteral("完整区域名（可选），例如和平区"));
    auto *filterHint = new QLabel(QStringLiteral("关键词模糊匹配站名或地址；区域需填写完整名称。修改后点击搜索。"), advancedFilters_);
    filterHint->setObjectName(QStringLiteral("stationFilterHint"));
    filterHint->setWordWrap(true);
    advancedLayout->addWidget(locationTitle);
    advancedLayout->addWidget(locationSummaryLabel_);
    advancedLayout->addWidget(demoLocationCheck_);
    advancedLayout->addWidget(locationPresetCombo_);
    auto *locationHint = new QLabel(QStringLiteral("请输入包含城市名称的完整地址。"), advancedFilters_);
    locationHint->setObjectName(QStringLiteral("locationInputHint"));
    locationHint->setWordWrap(true);
    advancedLayout->addWidget(locationHint);
    advancedLayout->addLayout(locationInputRow);
    advancedLayout->addWidget(locationMessageLabel_);
    advancedLayout->addWidget(regionInput_);
    advancedLayout->addWidget(filterHint);
    filterScroll_->setWidget(advancedFilters_);
    filterScroll_->hide();
    homeLayout->addWidget(filterScroll_);
    connect(regionInput_, &QLineEdit::textChanged, this, [this](const QString &region) {
        filterToggle_->setText(region.trimmed().isEmpty() ? QStringLiteral("筛选") : QStringLiteral("筛选·1"));
    });
    connect(filterToggle_, &QPushButton::toggled, this, [this](bool expanded) {
        if (expanded) currentOrderToggle_->setChecked(false);
        filterScroll_->setVisible(expanded);
        filterToggle_->setAccessibleName(expanded ? QStringLiteral("收起位置与区域筛选")
                                                 : QStringLiteral("展开位置与区域筛选"));
        layoutHomeOverlays();
    });

    currentOrderCard_ = new QFrame(homeOverlay_);
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
    homeLayout->addWidget(currentOrderCard_);
    connect(currentOrderToggle_, &QPushButton::toggled, this, [this](bool expanded) {
        if (expanded) filterToggle_->setChecked(false);
        currentOrderDetails_->setVisible(expanded);
        currentOrderToggle_->setAccessibleName(expanded ? QStringLiteral("收起当前订单操作")
                                                       : QStringLiteral("展开当前订单操作"));
        layoutHomeOverlays();
    });

    actionMessageLabel_ = new QLabel(homeOverlay_);
    actionMessageLabel_->setObjectName(QStringLiteral("stationActionMessage"));
    listMessageLabel_ = new QLabel(homeOverlay_);
    listMessageLabel_->setObjectName(QStringLiteral("stationListMessage"));
    for (auto *label : {actionMessageLabel_, listMessageLabel_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setMaximumHeight(48);
        label->hide();
        homeLayout->addWidget(label);
    }
    stationPreview_ = new StationPreviewCard(listPage_);
    connect(stationMap_, &StationMapView::stationSelected, this, &StationBrowserPage::previewStation);
    connect(stationPreview_, &StationPreviewCard::dismissed, this, [this] { previewStation(0); });
    connect(stationPreview_, &StationPreviewCard::detailsRequested, this, &StationBrowserPage::stationSelected);
    connect(stationPreview_, &StationPreviewCard::navigationRequested, this, [this](const auto &station) {
        navigationReturnPage_ = listPage_;
        emit navigationRequested(station);
    });
    listPage_->installEventFilter(this);
    homeOverlay_->installEventFilter(this);
    stationPreview_->installEventFilter(this);
    stationMap_->setCurrentLocation(currentLocation_);
}

void StationBrowserPage::configureHomeMap(const QUrl &scriptUrl)
{
    stationMap_->setMapScriptUrl(scriptUrl);
}

void StationBrowserPage::previewStation(qint64 stationId)
{
    stationMap_->selectStation(stationId);
    for (const auto &station : stations_) {
        if (station.stationId == stationId) {
            stationPreview_->setStation(station);
            filterToggle_->setChecked(false);
            currentOrderToggle_->setChecked(false);
            layoutHomeOverlays();
            return;
        }
    }
    stationPreview_->hide();
    layoutHomeOverlays();
}

bool StationBrowserPage::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == listPage_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        || ((watched == homeOverlay_ || watched == stationPreview_) && event->type() == QEvent::LayoutRequest)) {
        layoutHomeOverlays();
    }
    return QWidget::eventFilter(watched, event);
}

void StationBrowserPage::layoutHomeOverlays()
{
    if (!stationPreview_) return;
    const int margin = listPage_->width() < 400 ? 12 : 16;
    const int availableWidth = qMax(0, listPage_->width() - 2 * margin);
    filterScroll_->setFixedHeight(qBound(140, listPage_->height() * 30 / 100, 240));
    homeOverlay_->setFixedWidth(availableWidth);
    homeOverlay_->layout()->activate();
    homeOverlay_->setGeometry(margin, 12, availableWidth, homeOverlay_->sizeHint().height());
    const bool showPreview = stationMap_->selectedStationId() > 0
        && !filterToggle_->isChecked() && !currentOrderToggle_->isChecked();
    stationPreview_->setVisible(showPreview);
    stationPreview_->setFixedWidth(availableWidth);
    stationPreview_->layout()->activate();
    const int cardHeight = stationPreview_->heightForWidth(availableWidth) > 0
        ? stationPreview_->heightForWidth(availableWidth) : stationPreview_->sizeHint().height();
    // Preserve Tencent attribution / the explicit Demo caption below the card.
    stationPreview_->setGeometry(margin, listPage_->height() - cardHeight - 30, availableWidth, cardHeight);
    homeOverlay_->raise();
    stationPreview_->raise();
    stationMap_->setViewportMargins(QMargins(32, homeOverlay_->geometry().bottom() + 24, 70,
                                             showPreview ? cardHeight + 52 : 50));
}

}  // namespace charging::client
