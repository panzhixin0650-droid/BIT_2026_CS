#include "ui/station_browser_page.h"

#include "ui/charging_stop_dialog.h"
#include "ui/charging_art.h"
#include "ui/client_theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
#include <QWebEngineView>
#endif

#include <functional>
#include <utility>

namespace charging::client {

namespace {

class ClickableStationCard final : public QFrame {
public:
    explicit ClickableStationCard(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setActivatedHandler(std::function<void()> handler)
    {
        activatedHandler_ = std::move(handler);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            event->accept();
            if (activatedHandler_) {
                activatedHandler_();
            }
            return;
        }
        QFrame::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Space) {
            event->accept();
            if (activatedHandler_) {
                activatedHandler_();
            }
            return;
        }
        QFrame::keyPressEvent(event);
    }

private:
    std::function<void()> activatedHandler_;
};

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

QString congestionText(const std::optional<protocol::CongestionLevel> &level)
{
    if (!level.has_value()) {
        return QStringLiteral("拥堵预测暂不可用");
    }
    switch (*level) {
    case protocol::CongestionLevel::Low:
        return QStringLiteral("预计低拥堵");
    case protocol::CongestionLevel::Medium:
        return QStringLiteral("预计一般拥堵");
    case protocol::CongestionLevel::High:
        return QStringLiteral("预计高拥堵");
    }
    return QStringLiteral("拥堵预测暂不可用");
}

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

QFrame *createCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setProperty("role", "card");
    return card;
}

}  // namespace

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

    auto *homeScrollArea = new QScrollArea(listPage_);
    homeScrollArea->setObjectName(QStringLiteral("stationHomeScrollArea"));
    homeScrollArea->setWidgetResizable(true);
    homeScrollArea->setFrameShape(QFrame::NoFrame);
    auto *homeContent = new QWidget(homeScrollArea);
    homeContent->setObjectName(QStringLiteral("stationHomeScrollContent"));
    auto *homeContentLayout = new QVBoxLayout(homeContent);
    homeContentLayout->setContentsMargins(20, 20, 20, 16);
    homeContentLayout->setSpacing(12);
    homeScrollArea->setWidget(homeContent);
    listPageLayout->addWidget(homeScrollArea);

    welcomeLabel_ = new QLabel(homeContent);
    welcomeLabel_->setObjectName(QStringLiteral("welcomeLabel"));
    QFont welcomeFont = welcomeLabel_->font();
    welcomeFont.setPointSize(11);
    welcomeLabel_->setFont(welcomeFont);
    loginNoticeLabel_ = new QLabel(homeContent);
    loginNoticeLabel_->setObjectName(QStringLiteral("loginNoticeLabel"));
    loginNoticeLabel_->setStyleSheet(QStringLiteral("color: #245c45;"));
    loginNoticeLabel_->setProperty("role", "eyebrow");
    welcomeLabel_->setWordWrap(true);
    auto *brand = new QLabel(QStringLiteral("BIT  /  CHARGE     悦充"), homeContent);
    brand->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 700; color: #245c45;"));
    auto *greetingRow = new QHBoxLayout;
    greetingRow->addWidget(welcomeLabel_, 1);
    greetingRow->addWidget(loginNoticeLabel_);

    actionMessageLabel_ = new QLabel(homeContent);
    actionMessageLabel_->setObjectName(QStringLiteral("stationActionMessage"));
    actionMessageLabel_->setWordWrap(true);
    actionMessageLabel_->hide();

    currentOrderCard_ = createCard(homeContent);
    currentOrderCard_->setObjectName(QStringLiteral("currentOrderCard"));
    currentOrderCard_->setStyleSheet(QStringLiteral(
        "QFrame#currentOrderCard { background: #edf4e5; border: 1px solid #b9cfa7; border-radius: 18px; }"));
    auto *currentOrderLayout = new QVBoxLayout(currentOrderCard_);
    currentOrderLayout->setContentsMargins(14, 14, 14, 14);
    currentOrderLayout->setSpacing(7);
    auto *currentOrderTitle = new QLabel(QStringLiteral("当前进行中的订单"),
                                         currentOrderCard_);
    QFont currentOrderTitleFont = currentOrderTitle->font();
    currentOrderTitleFont.setBold(true);
    currentOrderTitle->setFont(currentOrderTitleFont);
    currentOrderSummaryLabel_ = new QLabel(currentOrderCard_);
    currentOrderSummaryLabel_->setObjectName(QStringLiteral("currentOrderSummary"));
    currentOrderSummaryLabel_->setWordWrap(true);
    currentOrderProgressLabel_ = new QLabel(currentOrderCard_);
    currentOrderProgressLabel_->setObjectName(
        QStringLiteral("currentOrderProgress"));
    currentOrderProgressLabel_->setWordWrap(true);
    currentOrderProgressLabel_->setStyleSheet(QStringLiteral(
        "color: #36583c; font-size: 15px; font-weight: 600; padding: 8px 0;"));
    cancelOrderButton_ = new QPushButton(QStringLiteral("取消预约"), currentOrderCard_);
    cancelOrderButton_->setObjectName(QStringLiteral("cancelReservationButton"));
    currentOrderNavigationButton_ =
        new QPushButton(QStringLiteral("导航"), currentOrderCard_);
    currentOrderNavigationButton_->setObjectName(
        QStringLiteral("currentOrderNavigationButton"));
    currentOrderNavigationButton_->setToolTip(
        QStringLiteral("导航到订单所属充电站"));
    reservationScanButton_ =
        new QPushButton(QStringLiteral("前往扫码充电"), currentOrderCard_);
    reservationScanButton_->setObjectName(
        QStringLiteral("startReservedChargingButton"));
    progressButton_ = new QPushButton(QStringLiteral("刷新充电进度"), currentOrderCard_);
    progressButton_->setObjectName(QStringLiteral("chargingProgressButton"));
    stopButton_ = new QPushButton(QStringLiteral("结束充电"), currentOrderCard_);
    stopButton_->setObjectName(QStringLiteral("chargingStopButton"));
    auto *currentOrderActions = new QHBoxLayout();
    currentOrderActions->setSpacing(6);
    for (auto *button : {currentOrderNavigationButton_, cancelOrderButton_,
                         reservationScanButton_, progressButton_, stopButton_}) {
        button->setStyleSheet(QStringLiteral("padding: 0 7px; font-size: 11px;"));
    }
    currentOrderActions->addWidget(currentOrderNavigationButton_);
    currentOrderActions->addWidget(cancelOrderButton_);
    currentOrderActions->addWidget(reservationScanButton_);
    currentOrderActions->addWidget(progressButton_);
    currentOrderActions->addWidget(stopButton_);
    currentOrderLayout->addWidget(currentOrderTitle);
    currentOrderLayout->addWidget(currentOrderSummaryLabel_);
    currentOrderLayout->addWidget(currentOrderProgressLabel_);
    currentOrderLayout->addLayout(currentOrderActions);
    currentOrderCard_->hide();

    auto *queryCard = createCard(homeContent);
    queryCard->setObjectName(QStringLiteral("stationQueryCard"));
    auto *queryLayout = new QVBoxLayout(queryCard);
    queryLayout->setContentsMargins(16, 14, 16, 14);
    queryLayout->setSpacing(10);
    advancedFilters_ = new QWidget(queryCard);
    advancedFilters_->setObjectName(QStringLiteral("stationAdvancedFilters"));
    advancedFilters_->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *advancedLayout = new QVBoxLayout(advancedFilters_);
    advancedLayout->setContentsMargins(0, 8, 0, 0);
    advancedLayout->setSpacing(8);
    auto *locationTitle = new QLabel(QStringLiteral("当前位置"), queryCard);
    locationTitle->setObjectName(QStringLiteral("stationLocationTitle"));
    QFont locationFont = locationTitle->font();
    locationFont.setBold(true);
    locationTitle->setFont(locationFont);
    demoLocationCheck_ = new QCheckBox(
        QStringLiteral("使用当前选定位置计算距离"), queryCard);
    demoLocationCheck_->setObjectName(QStringLiteral("demoLocationCheck"));
    demoLocationCheck_->setChecked(true);
    locationSummaryLabel_ = new QLabel(queryCard);
    locationSummaryLabel_->setObjectName(QStringLiteral("stationLocationSummary"));
    locationSummaryLabel_->setWordWrap(true);
    locationSummaryLabel_->setStyleSheet(QStringLiteral("color: #697969;"));
    locationPresetCombo_ = new QComboBox(queryCard);
    locationPresetCombo_->setObjectName(QStringLiteral("locationPresetCombo"));
    locationPresetCombo_->addItem(QStringLiteral("演示当前位置"),
                                  QStringLiteral("演示位置"));
    locationPresetCombo_->addItem(QStringLiteral("和平区"),
                                  QStringLiteral("沈阳市和平区"));
    locationPresetCombo_->addItem(QStringLiteral("浑南区"),
                                  QStringLiteral("沈阳市浑南区"));
    locationPresetCombo_->addItem(QStringLiteral("手动输入地址"), QString{});
    locationAddressInput_ = new QLineEdit(queryCard);
    locationAddressInput_->setObjectName(QStringLiteral("locationAddressInput"));
    locationAddressInput_->setPlaceholderText(
        QStringLiteral("输入城市和具体位置，如“沈阳市和平区青年大街”"));
    locationAddressInput_->setText(QStringLiteral("演示位置"));
    resolveLocationButton_ = new QPushButton(QStringLiteral("确定位置"), queryCard);
    resolveLocationButton_->setObjectName(QStringLiteral("resolveLocationButton"));
    auto *locationInputRow = new QHBoxLayout();
    locationInputRow->addWidget(locationAddressInput_, 1);
    locationInputRow->addWidget(resolveLocationButton_);
    auto *locationInputHint = new QLabel(
        QStringLiteral("请输入包含城市名称的完整地址，以便腾讯地图准确解析。"),
        queryCard);
    locationInputHint->setObjectName(QStringLiteral("locationInputHint"));
    locationInputHint->setStyleSheet(QStringLiteral("color: #697969;"));
    locationInputHint->setWordWrap(true);
    locationMessageLabel_ = new QLabel(queryCard);
    locationMessageLabel_->setObjectName(QStringLiteral("locationMessage"));
    locationMessageLabel_->setWordWrap(true);
    locationMessageLabel_->hide();

    auto *filterTitle = new QLabel(QStringLiteral("查找充电站"), queryCard);
    filterTitle->setObjectName(QStringLiteral("stationFilterTitle"));
    QFont filterFont = filterTitle->font();
    filterFont.setBold(true);
    filterTitle->setFont(filterFont);
    keywordInput_ = new QLineEdit(queryCard);
    keywordInput_->setObjectName(QStringLiteral("stationKeywordInput"));
    keywordInput_->setPlaceholderText(
        QStringLiteral("站名或地址关键词，如“和平”"));
    regionInput_ = new QLineEdit(queryCard);
    regionInput_->setObjectName(QStringLiteral("stationRegionInput"));
    regionInput_->setPlaceholderText(
        QStringLiteral("完整区域名（可选），如“和平区”"));
    refreshButton_ = new QPushButton(QStringLiteral("查询"), queryCard);
    refreshButton_->setObjectName(QStringLiteral("stationRefreshButton"));
    auto *searchRow = new QHBoxLayout();
    searchRow->addWidget(keywordInput_, 1);
    searchRow->addWidget(refreshButton_);
    auto *filterHint = new QLabel(
        QStringLiteral("关键词支持模糊匹配；区域按完整名称精确筛选。"),
        queryCard);
    filterHint->setObjectName(QStringLiteral("stationFilterHint"));
    filterHint->setStyleSheet(QStringLiteral("color: #697969;"));
    filterHint->setWordWrap(true);
    advancedLayout->addWidget(locationTitle);
    advancedLayout->addWidget(locationSummaryLabel_);
    advancedLayout->addWidget(demoLocationCheck_);
    advancedLayout->addWidget(locationPresetCombo_);
    advancedLayout->addLayout(locationInputRow);
    advancedLayout->addWidget(locationInputHint);
    advancedLayout->addWidget(locationMessageLabel_);
    advancedLayout->addWidget(regionInput_);
    advancedLayout->addWidget(filterHint);
    advancedFilters_->hide();
    locationCaption_ = new QLabel(queryCard);
    locationCaption_->setObjectName(QStringLiteral("stationLocationCaption"));
    locationCaption_->setWordWrap(true);
    locationCaption_->setStyleSheet(QStringLiteral("font-size: 11px; color: #65796c;"));
    filterToggle_ = new QPushButton(QStringLiteral("位置与筛选  +"), queryCard);
    filterToggle_->setObjectName(QStringLiteral("stationFilterToggle"));
    filterToggle_->setCheckable(true);
    filterToggle_->setFlat(true);
    filterToggle_->setAccessibleName(QStringLiteral("展开位置与区域筛选"));
    connect(regionInput_, &QLineEdit::textChanged, this, [this](const QString &region) {
        if (!filterToggle_->isChecked()) {
            filterToggle_->setText(region.trimmed().isEmpty()
                ? QStringLiteral("位置与筛选  +") : QStringLiteral("位置与筛选 · 1  +"));
        }
    });
    connect(filterToggle_, &QPushButton::toggled, this, [this](bool expanded) {
        advancedFilters_->setVisible(expanded);
        filterToggle_->setText(expanded ? QStringLiteral("收起筛选  −")
            : regionInput_->text().trimmed().isEmpty() ? QStringLiteral("位置与筛选  +")
                                                     : QStringLiteral("位置与筛选 · 1  +"));
        filterToggle_->setAccessibleName(expanded ? QStringLiteral("收起位置与区域筛选")
                                                 : QStringLiteral("展开位置与区域筛选"));
    });
    auto *locationRow = new QHBoxLayout();
    locationRow->addWidget(locationCaption_, 1);
    locationRow->addWidget(filterToggle_);
    queryLayout->addWidget(filterTitle);
    queryLayout->addLayout(searchRow);
    queryLayout->addLayout(locationRow);
    queryLayout->addWidget(advancedFilters_);

    listMessageLabel_ = new QLabel(homeContent);
    listMessageLabel_->setObjectName(QStringLiteral("stationListMessage"));
    listMessageLabel_->setWordWrap(true);
    listMessageLabel_->hide();

    stationListContent_ = new QWidget(homeContent);
    stationListContent_->setObjectName(QStringLiteral("stationListContent"));
    stationListLayout_ = new QVBoxLayout(stationListContent_);
    stationListLayout_->setContentsMargins(0, 0, 0, 0);
    stationListLayout_->setSpacing(10);
    stationListLayout_->addStretch();

    auto *stationHeadingRow = new QHBoxLayout();
    auto *stationHeading = new QLabel(QStringLiteral("附近好站"), homeContent);
    stationHeading->setProperty("role", "sectionTitle");
    stationCountLabel_ = new QLabel(homeContent);
    stationCountLabel_->setObjectName(QStringLiteral("stationResultCount"));
    stationCountLabel_->setProperty("role", "eyebrow");
    stationHeadingRow->addWidget(stationHeading, 1);
    stationHeadingRow->addWidget(stationCountLabel_);
    homeContentLayout->addWidget(brand);
    homeContentLayout->addLayout(greetingRow);
    homeContentLayout->addWidget(new ChargingArt(ChargingArt::Scene::Journey, homeContent));
    homeContentLayout->addWidget(actionMessageLabel_);
    homeContentLayout->addWidget(currentOrderCard_);
    homeContentLayout->addWidget(queryCard);
    homeContentLayout->addLayout(stationHeadingRow);
    homeContentLayout->addWidget(listMessageLabel_);
    homeContentLayout->addWidget(stationListContent_);
    homeContentLayout->addStretch();

    detailPage_ = new QWidget(pages_);
    detailPage_->setObjectName(QStringLiteral("stationDetailPage"));
    auto *detailPageLayout = new QVBoxLayout(detailPage_);
    detailPageLayout->setContentsMargins(20, 20, 20, 16);
    detailPageLayout->setSpacing(12);
    backButton_ = new QPushButton(QStringLiteral("‹ 返回充电站列表"), detailPage_);
    backButton_->setObjectName(QStringLiteral("stationDetailBackButton"));
    backButton_->setFlat(true);
    detailMessageLabel_ = new QLabel(detailPage_);
    detailMessageLabel_->setObjectName(QStringLiteral("stationDetailMessage"));
    detailMessageLabel_->setWordWrap(true);
    detailMessageLabel_->hide();

    auto *detailScrollArea = new QScrollArea(detailPage_);
    detailScrollArea->setWidgetResizable(true);
    detailScrollArea->setFrameShape(QFrame::NoFrame);
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
    detailPriceLabel_->setStyleSheet(QStringLiteral("color: #386a3c; font-weight: 600;"));
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
    detailLayout->addWidget(detailPriceLabel_);
    detailLayout->addWidget(detailNavigationButton_, 0, Qt::AlignLeft);
    detailLayout->addWidget(pileTitle);
    detailLayout->addLayout(pileListLayout_);
    detailLayout->addStretch();
    detailScrollArea->setWidget(detailContent_);
    detailPageLayout->addWidget(backButton_, 0, Qt::AlignLeft);
    detailPageLayout->addWidget(detailMessageLabel_);
    detailPageLayout->addWidget(detailScrollArea, 1);

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
    routeMessageLabel_->hide();
    routeDisplayLabel_ = new QLabel(
        QStringLiteral("选择出行方式后点击“开始导航”"), navigationPage_);
    routeDisplayLabel_->setObjectName(QStringLiteral("routeDisplay"));
    routeDisplayLabel_->setAlignment(Qt::AlignCenter);
    routeDisplayLabel_->setWordWrap(true);
    routeDisplayLabel_->setMinimumHeight(360);
    routeDisplayLabel_->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Expanding);
    routeDisplayLabel_->setStyleSheet(QStringLiteral(
        "background: #f0f3e9; border: 1px solid #acb8a6; border-radius: 12px; "
        "color: #536553; padding: 16px;"));
    routeDisplayStack_ = new QStackedWidget(navigationPage_);
    routeDisplayStack_->setObjectName(QStringLiteral("routeDisplayStack"));
    routeDisplayStack_->setMinimumHeight(360);
    routeDisplayStack_->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Expanding);
    routeDisplayStack_->addWidget(routeDisplayLabel_);
    navigationLayout->addLayout(navigationHeader);
    navigationLayout->addWidget(routeControlsCard);
    navigationLayout->addWidget(routeMessageLabel_);
    navigationLayout->addWidget(routeDisplayStack_, 1);

    pages_->addWidget(listPage_);
    pages_->addWidget(detailPage_);
    pages_->addWidget(navigationPage_);
    pages_->setCurrentWidget(listPage_);

    connect(refreshButton_, &QPushButton::clicked, this, &StationBrowserPage::refreshRequested);
    connect(regionInput_, &QLineEdit::returnPressed, this, &StationBrowserPage::refreshRequested);
    connect(keywordInput_, &QLineEdit::returnPressed, this, &StationBrowserPage::refreshRequested);
    connect(demoLocationCheck_, &QCheckBox::toggled,
            this, &StationBrowserPage::updateLocationSummary);
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
        navigationReturnPage_ = detailPage_;
        emit navigationRequested(navigationStation_);
    });
    connect(navigationBackButton, &QPushButton::clicked, this, [this]() {
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
        if (routeWebView_ != nullptr) {
            routeWebView_->stop();
        }
#endif
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
        emit progressRequested(progressButton_->property("orderId").toLongLong());
    });
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        if (confirmChargingStop(this)) {
            emit stopRequested(stopButton_->property("orderId").toLongLong());
        }
    });
    updateLocationSummary();
}

StationQuery StationBrowserPage::stationQuery() const
{
    StationQuery query;
    if (demoLocationCheck_->isChecked()) {
        query.longitude = currentLocation_.longitude;
        query.latitude = currentLocation_.latitude;
    }
    query.region = regionInput_->text().trimmed();
    query.keyword = keywordInput_->text().trimmed();
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
}

void StationBrowserPage::setListLoading(bool loading)
{
    refreshButton_->setDisabled(loading);
    regionInput_->setDisabled(loading);
    keywordInput_->setDisabled(loading);
    demoLocationCheck_->setDisabled(loading);
    stationListContent_->setDisabled(loading);
    listMessageLabel_->setStyleSheet(QStringLiteral("color: #697969;"));
    listMessageLabel_->setText(loading ? QStringLiteral("正在获取充电站…") : QString{});
    listMessageLabel_->setVisible(loading);
}

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
}

void StationBrowserPage::showStations(const QList<protocol::StationDto> &stations)
{
    clearStationCards();
    setListLoading(false);
    stationCountLabel_->setText(QStringLiteral("%1 个站点").arg(stations.size()));
    if (stations.isEmpty()) {
        listMessageLabel_->setText(QStringLiteral("没有找到符合条件的充电站"));
        listMessageLabel_->setStyleSheet(QStringLiteral("color: #697969;"));
        listMessageLabel_->show();
        return;
    }

    for (const auto &station : stations) {
        auto *card = new ClickableStationCard(stationListContent_);
        card->setFrameShape(QFrame::StyledPanel);
        card->setProperty("role", "card");
        card->setObjectName(QStringLiteral("stationCard_%1").arg(station.stationId));
        card->setStyleSheet(QStringLiteral(
            "QFrame#stationCard_%1:hover, QFrame#stationCard_%1:focus { "
            "border: 1px solid #789875; background: #f2f6ec; }")
                .arg(station.stationId));
        card->setAccessibleName(QStringLiteral("查看%1详情").arg(station.name));
        card->setActivatedHandler([this, stationId = station.stationId]() {
            emit stationSelected(stationId);
        });
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(9);
        auto *titleRow = new QHBoxLayout();
        auto *name = new QLabel(station.name, card);
        name->setObjectName(QStringLiteral("stationName_%1").arg(station.stationId));
        QFont nameFont = name->font();
        nameFont.setBold(true);
        nameFont.setPointSize(12);
        name->setFont(nameFont);
        name->setWordWrap(true);
        auto *stationIcon = new QLabel(card);
        stationIcon->setPixmap(clientNavigationIcon(NavigationIcon::Charging).pixmap(22, 22, QIcon::Selected));
        stationIcon->setFixedSize(34, 34);
        stationIcon->setAlignment(Qt::AlignCenter);
        stationIcon->setStyleSheet(QStringLiteral("background: #edf4e4; border-radius: 10px;"));
        stationIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(stationIcon);
        titleRow->addWidget(name, 1);
        if (station.recommended) {
            auto *badge = new QLabel(QStringLiteral("推荐"), card);
            badge->setObjectName(QStringLiteral("stationRecommended_%1")
                                     .arg(station.stationId));
            badge->setStyleSheet(QStringLiteral(
                "color: white; background: #386a3c; border-radius: 8px; padding: 2px 7px;"));
            badge->setAttribute(Qt::WA_TransparentForMouseEvents);
            titleRow->addWidget(badge);
        }
        auto *address = new QLabel(station.address, card);
        address->setStyleSheet(QStringLiteral("color: #697969;"));
        address->setWordWrap(true);
        const QString distance = station.distanceKm.has_value()
            ? QStringLiteral("%1 km").arg(*station.distanceKm, 0, 'f', 2)
            : QStringLiteral("距离待定位");
        auto *availability = new QLabel(
            QStringLiteral("空闲 %1/%2 · %3")
                .arg(station.availablePileCount)
                .arg(station.totalPileCount)
                .arg(distance),
            card);
        availability->setWordWrap(true);
        availability->setStyleSheet(QStringLiteral("color: #245c45; font-size: 12px; font-weight: 600;"));
        auto *price = new QLabel(formatPrice(station.priceCentsPerKwh), card);
        price->setObjectName(QStringLiteral("stationPrice_%1").arg(station.stationId));
        price->setStyleSheet(QStringLiteral("color: #245c45; font-size: 23px; font-weight: 700;"));
        price->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *prediction = new QLabel(congestionText(station.predictedCongestion), card);
        prediction->setStyleSheet(QStringLiteral("color: #697969;"));
        prediction->setWordWrap(true);
        auto *detailHint = new QLabel(QStringLiteral("点击卡片查看详情  ›"), card);
        detailHint->setObjectName(
            QStringLiteral("stationDetailHint_%1").arg(station.stationId));
        detailHint->setAlignment(Qt::AlignRight);
        detailHint->setStyleSheet(QStringLiteral("color: #245c45;"));
        auto *navigationButton = new QPushButton(QStringLiteral("导航"), card);
        navigationButton->setObjectName(
            QStringLiteral("stationNavigationButton_%1").arg(station.stationId));
        connect(navigationButton, &QPushButton::clicked, this, [this, station]() {
            navigationReturnPage_ = listPage_;
            emit navigationRequested(station);
        });
        auto *bottomRow = new QHBoxLayout();
        navigationButton->setIcon(clientNavigationIcon(NavigationIcon::Route));
        navigationButton->setAccessibleName(QStringLiteral("导航到%1").arg(station.name));
        bottomRow->addWidget(price, 1);
        bottomRow->addWidget(navigationButton);
        for (QLabel *label : {name, address, availability, prediction, detailHint}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        layout->addLayout(titleRow);
        layout->addWidget(address);
        layout->addWidget(availability);
        layout->addWidget(prediction);
        layout->addLayout(bottomRow);
        layout->addWidget(detailHint);
        stationListLayout_->addWidget(card);
    }
    stationListLayout_->addStretch();
}

void StationBrowserPage::showListError(const QString &message)
{
    setListLoading(false);
    listMessageLabel_->setText(message);
    listMessageLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    listMessageLabel_->show();
}

void StationBrowserPage::showListMessage(const QString &message, bool error)
{
    actionMessageLabel_->setText(message);
    actionMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                              : QStringLiteral("color: #386a3c;"));
    actionMessageLabel_->setVisible(!message.isEmpty());
}

void StationBrowserPage::showCurrentOrder(
    const std::optional<protocol::OrderDto> &order)
{
    if (!order.has_value()) {
        currentOrderCard_->hide();
        return;
    }

    currentOrderSummaryLabel_->setText(
        QStringLiteral("%1\n充电桩：%2\n状态：%3")
            .arg(order->stationName,
                 order->pileCode,
                 orderStatusText(order->status)));
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
    stopButton_->setVisible(order->status == protocol::OrderStatus::Charging);
    currentOrderProgressLabel_->hide();
    if (order->status == protocol::OrderStatus::Charging) {
        currentOrderProgressLabel_->setText(
            QStringLiteral("已充电 %1 度 · %2 分钟\n当前预估金额：%3")
                .arg(order->energyWh / 1000.0, 0, 'f', 2)
                .arg(order->durationSeconds / 60)
                .arg(formatMoney(order->amountCents)));
        currentOrderProgressLabel_->show();
    } else if (order->status == protocol::OrderStatus::PendingPayment) {
        currentOrderProgressLabel_->setText(
            QStringLiteral("待支付金额：%1\n请前往“订单”查看并完成结算。")
                .arg(formatMoney(order->amountCents)));
        currentOrderProgressLabel_->show();
    }
    cancelOrderButton_->setDisabled(reservationBusy_);
    currentOrderNavigationButton_->setDisabled(reservationBusy_);
    reservationScanButton_->setDisabled(reservationBusy_);
    progressButton_->setDisabled(reservationBusy_);
    stopButton_->setDisabled(reservationBusy_);
    currentOrderCard_->show();
}

void StationBrowserPage::showListPage()
{
    navigationReturnPage_ = listPage_;
    pages_->setCurrentWidget(listPage_);
}

void StationBrowserPage::showDetailLoading()
{
    pages_->setCurrentWidget(detailPage_);
    backButton_->setEnabled(true);
    detailContent_->hide();
    detailMessageLabel_->setText(QStringLiteral("正在获取充电站详情…"));
    detailMessageLabel_->setStyleSheet(QStringLiteral("color: #697969;"));
    detailMessageLabel_->show();
}

void StationBrowserPage::showStationDetail(const StationDetailPayload &detail)
{
    clearPileCards();
    navigationStation_ = detail.station;
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
        QStringLiteral("当前站点价格：%1").arg(formatPrice(detail.station.priceCentsPerKwh)));

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
        auto *rightLayout = new QVBoxLayout();
        rightLayout->addWidget(status, 0, Qt::AlignRight);
        rightLayout->addWidget(reserveButton, 0, Qt::AlignRight);
        layout->addWidget(description, 1);
        layout->addLayout(rightLayout);
        pileListLayout_->addWidget(card);
    }
    detailContent_->show();
    pages_->setCurrentWidget(detailPage_);
}

void StationBrowserPage::showDetailError(const QString &message)
{
    detailContent_->hide();
    detailMessageLabel_->setText(message);
    detailMessageLabel_->setStyleSheet(QStringLiteral("color: #c62828;"));
    detailMessageLabel_->show();
    pages_->setCurrentWidget(detailPage_);
}

void StationBrowserPage::showDetailMessage(const QString &message, bool error)
{
    detailMessageLabel_->setText(message);
    detailMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                              : QStringLiteral("color: #386a3c;"));
    detailMessageLabel_->setVisible(!message.isEmpty());
}

void StationBrowserPage::setLocationBusy(bool busy)
{
    locationPresetCombo_->setDisabled(busy);
    locationAddressInput_->setDisabled(busy);
    resolveLocationButton_->setDisabled(busy);
}

void StationBrowserPage::setResolvedLocation(const MapLocation &location)
{
    currentLocation_ = location;
    demoLocationCheck_->setChecked(true);
    updateLocationSummary();
}

void StationBrowserPage::showLocationMessage(const QString &message, bool error)
{
    locationMessageLabel_->setText(message);
    locationMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                                : QStringLiteral("color: #386a3c;"));
    locationMessageLabel_->setVisible(!message.isEmpty());
}

void StationBrowserPage::showNavigation(const protocol::StationDto &station,
                                        const MapLocation &start)
{
    navigationStation_ = station;
    routeStartInput_->setText(start.address);
    routeDestinationLabel_->setText(
        QStringLiteral("%1 · %2").arg(station.name, station.address));
    routeDisplayLabel_->setText(QStringLiteral("选择出行方式后点击“开始导航”"));
    routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (routeWebView_ != nullptr) {
        routeWebView_->stop();
    }
#endif
    routeMessageLabel_->hide();
    pages_->setCurrentWidget(navigationPage_);
}

void StationBrowserPage::setRouteBusy(bool busy)
{
    routeStartInput_->setDisabled(busy);
    routeModeCombo_->setDisabled(busy);
    routePlanButton_->setDisabled(busy);
}

void StationBrowserPage::showRouteMessage(const QString &message, bool error)
{
    routeMessageLabel_->setText(message);
    routeMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                             : QStringLiteral("color: #386a3c;"));
    routeMessageLabel_->setVisible(!message.isEmpty());
}

void StationBrowserPage::showRouteResult(const RouteResult &result)
{
    showRouteMessage(result.message);
    routeDisplayLabel_->setText(result.summary);
    const bool hasHtml = !result.routeHtml.isEmpty();
    const bool hasUrl = result.routeUrl.isValid() && !result.routeUrl.isEmpty();
    if (!hasHtml && !hasUrl) {
        routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
        return;
    }

#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (routeWebView_ == nullptr) {
        routeWebView_ = new QWebEngineView(routeDisplayStack_);
        routeWebView_->setObjectName(QStringLiteral("routeWebView"));
        routeWebView_->setMinimumHeight(360);
        routeWebView_->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Expanding);
        routeDisplayStack_->addWidget(routeWebView_);
        connect(routeWebView_, &QWebEngineView::loadFinished, this,
                [this](bool success) {
                    if (success) {
                        showRouteMessage(QStringLiteral("腾讯地图路线已加载"));
                    } else {
                        showRouteMessage(
                            QStringLiteral("腾讯地图页面加载失败，请检查网络或 Key 配置"),
                            true);
                    }
                });
    }
    routeDisplayStack_->setCurrentWidget(routeWebView_);
    if (hasHtml) {
        routeWebView_->setHtml(result.routeHtml,
                               QUrl(QStringLiteral("https://map.qq.com/")));
    } else {
        routeWebView_->load(result.routeUrl);
    }
#else
    routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
    showRouteMessage(
        QStringLiteral("当前构建未启用 Qt WebEngine，请使用 Mock 地图或重新配置客户端"),
        true);
#endif
}

void StationBrowserPage::reset()
{
    setListLoading(false);
    clearStationCards();
    stationCountLabel_->clear();
    filterToggle_->setChecked(false);
    clearPileCards();
    listMessageLabel_->hide();
    actionMessageLabel_->hide();
    currentOrderCard_->hide();
    detailMessageLabel_->hide();
    locationMessageLabel_->hide();
    routeMessageLabel_->hide();
    routeDisplayStack_->setCurrentWidget(routeDisplayLabel_);
#ifdef CHARGING_CLIENT_HAS_WEBENGINE
    if (routeWebView_ != nullptr) {
        routeWebView_->stop();
    }
#endif
    setLocationBusy(false);
    setRouteBusy(false);
    pages_->setCurrentWidget(listPage_);
}

void StationBrowserPage::clearStationCards()
{
    while (QLayoutItem *item = stationListLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void StationBrowserPage::clearPileCards()
{
    reservationButtons_.clear();
    while (QLayoutItem *item = pileListLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void StationBrowserPage::updateLocationSummary()
{
    locationCaption_->setText(demoLocationCheck_->isChecked()
        ? QStringLiteral("◎  %1").arg(currentLocation_.address)
        : QStringLiteral("◎  未指定位置"));
    if (demoLocationCheck_->isChecked()) {
        locationSummaryLabel_->setText(
            QStringLiteral("%1 · %2, %3\n"
                           "当前位置仅用于本次查询，不会保存到数据库。")
                .arg(currentLocation_.address)
                .arg(currentLocation_.longitude, 0, 'f', 4)
                .arg(currentLocation_.latitude, 0, 'f', 4));
        return;
    }

    locationSummaryLabel_->setText(
        QStringLiteral("未指定位置 · 当前查询不计算距离\n"
                       "仍可使用站名、地址关键词或完整区域名查找充电站。"));
}

}  // namespace charging::client
