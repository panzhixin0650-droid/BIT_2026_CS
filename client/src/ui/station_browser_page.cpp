#include "ui/station_browser_page.h"

#include "ui/charging_stop_dialog.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

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
        return QStringLiteral("#137333");
    case protocol::PileStatus::Reserved:
        return QStringLiteral("#b06000");
    case protocol::PileStatus::Charging:
        return QStringLiteral("#1677ff");
    case protocol::PileStatus::Fault:
        return QStringLiteral("#c62828");
    case protocol::PileStatus::Offline:
        return QStringLiteral("#667085");
    }
    return QStringLiteral("#667085");
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
    card->setStyleSheet(QStringLiteral(
        "QFrame { background: white; border: 1px solid #e4e7ec; "
        "border-radius: 12px; } QLabel { border: none; }"));
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
    welcomeFont.setPointSize(18);
    welcomeFont.setBold(true);
    welcomeLabel_->setFont(welcomeFont);
    loginNoticeLabel_ = new QLabel(homeContent);
    loginNoticeLabel_->setObjectName(QStringLiteral("loginNoticeLabel"));
    loginNoticeLabel_->setStyleSheet(QStringLiteral("color: #1677ff;"));

    actionMessageLabel_ = new QLabel(homeContent);
    actionMessageLabel_->setObjectName(QStringLiteral("stationActionMessage"));
    actionMessageLabel_->setWordWrap(true);
    actionMessageLabel_->hide();

    currentOrderCard_ = createCard(homeContent);
    currentOrderCard_->setObjectName(QStringLiteral("currentOrderCard"));
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
    currentOrderProgressLabel_->setStyleSheet(QStringLiteral("color: #667085;"));
    cancelOrderButton_ = new QPushButton(QStringLiteral("取消预约"), currentOrderCard_);
    cancelOrderButton_->setObjectName(QStringLiteral("cancelReservationButton"));
    reservationScanButton_ =
        new QPushButton(QStringLiteral("前往扫码充电"), currentOrderCard_);
    reservationScanButton_->setObjectName(
        QStringLiteral("startReservedChargingButton"));
    progressButton_ = new QPushButton(QStringLiteral("刷新充电进度"), currentOrderCard_);
    progressButton_->setObjectName(QStringLiteral("chargingProgressButton"));
    stopButton_ = new QPushButton(QStringLiteral("结束充电"), currentOrderCard_);
    stopButton_->setObjectName(QStringLiteral("chargingStopButton"));
    auto *currentOrderActions = new QHBoxLayout();
    currentOrderActions->addStretch();
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
    queryLayout->setContentsMargins(14, 14, 14, 14);
    queryLayout->setSpacing(8);
    auto *locationTitle = new QLabel(QStringLiteral("当前位置"), queryCard);
    locationTitle->setObjectName(QStringLiteral("stationLocationTitle"));
    QFont locationFont = locationTitle->font();
    locationFont.setBold(true);
    locationTitle->setFont(locationFont);
    demoLocationCheck_ = new QCheckBox(
        QStringLiteral("使用演示位置计算距离"), queryCard);
    demoLocationCheck_->setObjectName(QStringLiteral("demoLocationCheck"));
    demoLocationCheck_->setChecked(true);
    locationSummaryLabel_ = new QLabel(queryCard);
    locationSummaryLabel_->setObjectName(QStringLiteral("stationLocationSummary"));
    locationSummaryLabel_->setWordWrap(true);
    locationSummaryLabel_->setStyleSheet(QStringLiteral("color: #667085;"));

    auto *filterTitle = new QLabel(QStringLiteral("查找充电站"), queryCard);
    filterTitle->setObjectName(QStringLiteral("stationFilterTitle"));
    QFont filterFont = filterTitle->font();
    filterFont.setBold(true);
    filterTitle->setFont(filterFont);
    auto *filterLayout = new QVBoxLayout();
    filterLayout->setSpacing(8);
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
    auto *regionLayout = new QHBoxLayout();
    regionLayout->addWidget(regionInput_, 1);
    regionLayout->addWidget(refreshButton_);
    filterLayout->addWidget(keywordInput_);
    filterLayout->addLayout(regionLayout);
    auto *filterHint = new QLabel(
        QStringLiteral("关键词支持模糊匹配；区域按完整名称精确筛选。"),
        queryCard);
    filterHint->setObjectName(QStringLiteral("stationFilterHint"));
    filterHint->setStyleSheet(QStringLiteral("color: #667085;"));
    filterHint->setWordWrap(true);
    queryLayout->addWidget(locationTitle);
    queryLayout->addWidget(locationSummaryLabel_);
    queryLayout->addWidget(demoLocationCheck_);
    queryLayout->addSpacing(4);
    queryLayout->addWidget(filterTitle);
    queryLayout->addLayout(filterLayout);
    queryLayout->addWidget(filterHint);

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

    homeContentLayout->addWidget(welcomeLabel_);
    homeContentLayout->addWidget(loginNoticeLabel_);
    homeContentLayout->addWidget(actionMessageLabel_);
    homeContentLayout->addWidget(currentOrderCard_);
    homeContentLayout->addWidget(queryCard);
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
    detailMetaLabel_ = new QLabel(detailContent_);
    detailMetaLabel_->setObjectName(QStringLiteral("stationDetailMeta"));
    detailMetaLabel_->setWordWrap(true);
    detailPriceLabel_ = new QLabel(detailContent_);
    detailPriceLabel_->setObjectName(QStringLiteral("stationDetailPrice"));
    detailPriceLabel_->setStyleSheet(QStringLiteral("color: #137333; font-weight: 600;"));
    auto *pileTitle = new QLabel(QStringLiteral("充电桩"), detailContent_);
    QFont pileTitleFont = pileTitle->font();
    pileTitleFont.setBold(true);
    pileTitle->setFont(pileTitleFont);
    pileListLayout_ = new QVBoxLayout();
    pileListLayout_->setSpacing(10);
    detailLayout->addWidget(detailNameLabel_);
    detailLayout->addWidget(detailMetaLabel_);
    detailLayout->addWidget(detailPriceLabel_);
    detailLayout->addWidget(pileTitle);
    detailLayout->addLayout(pileListLayout_);
    detailLayout->addStretch();
    detailScrollArea->setWidget(detailContent_);
    detailPageLayout->addWidget(backButton_, 0, Qt::AlignLeft);
    detailPageLayout->addWidget(detailMessageLabel_);
    detailPageLayout->addWidget(detailScrollArea, 1);

    pages_->addWidget(listPage_);
    pages_->addWidget(detailPage_);
    pages_->setCurrentWidget(listPage_);

    connect(refreshButton_, &QPushButton::clicked, this, &StationBrowserPage::refreshRequested);
    connect(regionInput_, &QLineEdit::returnPressed, this, &StationBrowserPage::refreshRequested);
    connect(keywordInput_, &QLineEdit::returnPressed, this, &StationBrowserPage::refreshRequested);
    connect(demoLocationCheck_, &QCheckBox::toggled,
            this, &StationBrowserPage::updateLocationSummary);
    connect(backButton_, &QPushButton::clicked, this, &StationBrowserPage::detailBackRequested);
    connect(cancelOrderButton_, &QPushButton::clicked, this, [this]() {
        emit cancellationRequested(cancelOrderButton_->property("orderId").toLongLong());
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
        query.longitude = 123.42;
        query.latitude = 41.70;
    }
    query.region = regionInput_->text().trimmed();
    query.keyword = keywordInput_->text().trimmed();
    return query;
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
    listMessageLabel_->setStyleSheet(QStringLiteral("color: #667085;"));
    listMessageLabel_->setText(loading ? QStringLiteral("正在获取充电站…") : QString{});
    listMessageLabel_->setVisible(loading);
}

void StationBrowserPage::setReservationBusy(bool busy)
{
    reservationBusy_ = busy;
    cancelOrderButton_->setDisabled(busy);
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
    if (stations.isEmpty()) {
        listMessageLabel_->setText(QStringLiteral("没有找到符合条件的充电站"));
        listMessageLabel_->setStyleSheet(QStringLiteral("color: #667085;"));
        listMessageLabel_->show();
        return;
    }

    for (const auto &station : stations) {
        auto *card = new ClickableStationCard(stationListContent_);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background: white; border: 1px solid #e4e7ec; "
            "border-radius: 12px; } "
            "QFrame:hover, QFrame:focus { border: 2px solid #91caff; "
            "background: #f5f9ff; } QLabel { border: none; background: transparent; }"));
        card->setObjectName(QStringLiteral("stationCard_%1").arg(station.stationId));
        card->setAccessibleName(QStringLiteral("查看%1详情").arg(station.name));
        card->setActivatedHandler([this, stationId = station.stationId]() {
            emit stationSelected(stationId);
        });
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(6);
        auto *titleRow = new QHBoxLayout();
        auto *name = new QLabel(station.name, card);
        name->setObjectName(QStringLiteral("stationName_%1").arg(station.stationId));
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        titleRow->addWidget(name, 1);
        if (station.recommended) {
            auto *badge = new QLabel(QStringLiteral("推荐"), card);
            badge->setObjectName(QStringLiteral("stationRecommended_%1")
                                     .arg(station.stationId));
            badge->setStyleSheet(QStringLiteral(
                "color: white; background: #137333; border-radius: 8px; padding: 2px 7px;"));
            badge->setAttribute(Qt::WA_TransparentForMouseEvents);
            titleRow->addWidget(badge);
        }
        auto *address = new QLabel(station.address, card);
        address->setStyleSheet(QStringLiteral("color: #667085;"));
        address->setWordWrap(true);
        const QString distance = station.distanceKm.has_value()
            ? QStringLiteral("%1 km").arg(*station.distanceKm, 0, 'f', 2)
            : QStringLiteral("距离待定位");
        auto *availability = new QLabel(
            QStringLiteral("空闲 %1/%2 · %3 · %4")
                .arg(station.availablePileCount)
                .arg(station.totalPileCount)
                .arg(distance, formatPrice(station.priceCentsPerKwh)),
            card);
        auto *prediction = new QLabel(congestionText(station.predictedCongestion), card);
        prediction->setStyleSheet(QStringLiteral("color: #667085;"));
        auto *detailHint = new QLabel(QStringLiteral("点击卡片查看详情  ›"), card);
        detailHint->setObjectName(
            QStringLiteral("stationDetailHint_%1").arg(station.stationId));
        detailHint->setAlignment(Qt::AlignRight);
        detailHint->setStyleSheet(QStringLiteral("color: #1677ff;"));
        for (QLabel *label : {name, address, availability, prediction, detailHint}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        layout->addLayout(titleRow);
        layout->addWidget(address);
        layout->addWidget(availability);
        layout->addWidget(prediction);
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
                                              : QStringLiteral("color: #137333;"));
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
    reservationScanButton_->setProperty("pileCode", order->pileCode);
    progressButton_->setProperty("orderId", order->orderId);
    stopButton_->setProperty("orderId", order->orderId);
    cancelOrderButton_->setVisible(order->status == protocol::OrderStatus::Reserved);
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
    reservationScanButton_->setDisabled(reservationBusy_);
    progressButton_->setDisabled(reservationBusy_);
    stopButton_->setDisabled(reservationBusy_);
    currentOrderCard_->show();
}

void StationBrowserPage::showListPage()
{
    pages_->setCurrentWidget(listPage_);
}

void StationBrowserPage::showDetailLoading()
{
    pages_->setCurrentWidget(detailPage_);
    backButton_->setEnabled(true);
    detailContent_->hide();
    detailMessageLabel_->setText(QStringLiteral("正在获取充电站详情…"));
    detailMessageLabel_->setStyleSheet(QStringLiteral("color: #667085;"));
    detailMessageLabel_->show();
}

void StationBrowserPage::showStationDetail(const StationDetailPayload &detail)
{
    clearPileCards();
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
                                              : QStringLiteral("color: #137333;"));
    detailMessageLabel_->setVisible(!message.isEmpty());
}

void StationBrowserPage::reset()
{
    setListLoading(false);
    clearStationCards();
    clearPileCards();
    listMessageLabel_->hide();
    actionMessageLabel_->hide();
    currentOrderCard_->hide();
    detailMessageLabel_->hide();
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
    if (demoLocationCheck_->isChecked()) {
        locationSummaryLabel_->setText(
            QStringLiteral("演示位置 · 123.4200, 41.7000\n"
                           "地图地址搜索、自动定位和地图选点将在地图适配器接入后提供。"));
        return;
    }

    locationSummaryLabel_->setText(
        QStringLiteral("未指定位置 · 当前查询不计算距离\n"
                       "仍可使用站名、地址关键词或完整区域名查找充电站。"));
}

}  // namespace charging::client
