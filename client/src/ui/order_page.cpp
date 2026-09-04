#include "ui/order_page.h"

#include "ui/charging_stop_dialog.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace charging::client {

namespace {

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

QString orderStatusColor(protocol::OrderStatus status)
{
    switch (status) {
    case protocol::OrderStatus::Reserved:
        return QStringLiteral("#b06000");
    case protocol::OrderStatus::Charging:
        return QStringLiteral("#1677ff");
    case protocol::OrderStatus::PendingPayment:
        return QStringLiteral("#c62828");
    case protocol::OrderStatus::Completed:
        return QStringLiteral("#137333");
    case protocol::OrderStatus::Cancelled:
        return QStringLiteral("#667085");
    }
    return QStringLiteral("#667085");
}

bool isCurrentStatus(protocol::OrderStatus status)
{
    return status == protocol::OrderStatus::Reserved
        || status == protocol::OrderStatus::Charging
        || status == protocol::OrderStatus::PendingPayment;
}

QString formatMoney(qint64 cents)
{
    return QStringLiteral("¥%1.%2")
        .arg(cents / 100)
        .arg(cents % 100, 2, 10, QChar('0'));
}

QString formatDateTime(const QString &isoDateTime)
{
    const QDateTime parsed = QDateTime::fromString(isoDateTime, Qt::ISODate);
    return parsed.isValid() ? parsed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                            : isoDateTime;
}

QString formatDuration(qint64 seconds)
{
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours > 0) {
        return QStringLiteral("%1小时%2分钟").arg(hours).arg(minutes);
    }
    return QStringLiteral("%1分钟").arg(minutes);
}

QString orderModeText(protocol::OrderMode mode)
{
    return mode == protocol::OrderMode::Reservation ? QStringLiteral("预约充电")
                                                     : QStringLiteral("直接充电");
}

class ClickableOrderCard final : public QFrame {
public:
    explicit ClickableOrderCard(QWidget *parent = nullptr)
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

ClickableOrderCard *createCard(QWidget *parent, bool highlighted = false)
{
    auto *card = new ClickableOrderCard(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(highlighted
            ? QStringLiteral("QFrame { background: #eef6ff; border: 2px solid #1677ff; "
                             "border-radius: 12px; } "
                             "QFrame:hover, QFrame:focus { background: #f2f4f7; "
                             "border: 2px solid #667085; } "
                             "QLabel { border: none; background: transparent; }")
            : QStringLiteral("QFrame { background: white; border: 1px solid #e4e7ec; "
                             "border-radius: 12px; } "
                             "QFrame:hover, QFrame:focus { background: #f2f4f7; "
                             "border: 2px solid #667085; } "
                             "QLabel { border: none; background: transparent; }"));
    card->setProperty("currentOrderHighlighted", highlighted);
    return card;
}

}  // namespace

OrderPage::OrderPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ordersPage"));
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("orderPages"));
    rootLayout->addWidget(pages_);

    listPage_ = new QWidget(pages_);
    listPage_->setObjectName(QStringLiteral("orderListPage"));
    auto *listLayout = new QVBoxLayout(listPage_);
    listLayout->setContentsMargins(20, 20, 20, 16);
    listLayout->setSpacing(12);

    auto *headingRow = new QHBoxLayout();
    auto *heading = new QLabel(QStringLiteral("我的订单"), listPage_);
    heading->setObjectName(QStringLiteral("orderListHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSize(24);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    refreshButton_ = new QPushButton(QStringLiteral("刷新"), listPage_);
    refreshButton_->setObjectName(QStringLiteral("orderRefreshButton"));
    headingRow->addWidget(heading, 1);
    headingRow->addWidget(refreshButton_);

    auto *description = new QLabel(
        QStringLiteral("按创建时间倒序显示；进行中的订单会优先高亮。"), listPage_);
    description->setStyleSheet(QStringLiteral("color: #667085;"));
    description->setWordWrap(true);
    messageLabel_ = new QLabel(listPage_);
    messageLabel_->setObjectName(QStringLiteral("orderListMessage"));
    messageLabel_->setWordWrap(true);
    messageLabel_->hide();

    auto *scrollArea = new QScrollArea(listPage_);
    scrollArea->setObjectName(QStringLiteral("orderListScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    orderListContent_ = new QWidget(scrollArea);
    orderListContent_->setObjectName(QStringLiteral("orderListContent"));
    orderListLayout_ = new QVBoxLayout(orderListContent_);
    orderListLayout_->setContentsMargins(0, 0, 0, 0);
    orderListLayout_->setSpacing(10);
    orderListLayout_->addStretch();
    scrollArea->setWidget(orderListContent_);

    listLayout->addLayout(headingRow);
    listLayout->addWidget(description);
    listLayout->addWidget(messageLabel_);
    listLayout->addWidget(scrollArea, 1);

    detailPage_ = new QWidget(pages_);
    detailPage_->setObjectName(QStringLiteral("orderDetailPage"));
    auto *detailLayout = new QVBoxLayout(detailPage_);
    detailLayout->setContentsMargins(20, 20, 20, 16);
    detailLayout->setSpacing(12);
    auto *backButton = new QPushButton(QStringLiteral("‹ 返回订单列表"), detailPage_);
    backButton->setObjectName(QStringLiteral("orderDetailBackButton"));
    backButton->setFlat(true);
    auto *detailHeading = new QLabel(QStringLiteral("订单详情"), detailPage_);
    detailHeading->setObjectName(QStringLiteral("orderDetailHeading"));
    QFont detailHeadingFont = detailHeading->font();
    detailHeadingFont.setPointSize(24);
    detailHeadingFont.setBold(true);
    detailHeading->setFont(detailHeadingFont);

    auto *detailScrollArea = new QScrollArea(detailPage_);
    detailScrollArea->setObjectName(QStringLiteral("orderDetailScrollArea"));
    detailScrollArea->setWidgetResizable(true);
    detailScrollArea->setFrameShape(QFrame::NoFrame);
    auto *detailContent = new QWidget(detailScrollArea);
    auto *detailContentLayout = new QVBoxLayout(detailContent);
    detailContentLayout->setContentsMargins(0, 0, 0, 0);

    auto *detailCard = new QFrame(detailContent);
    detailCard->setObjectName(QStringLiteral("orderDetailCard"));
    detailCard->setStyleSheet(QStringLiteral(
        "QFrame#orderDetailCard { background: white; border: 1px solid #e4e7ec; "
        "border-radius: 14px; } "
        "QFrame#orderDetailCard QLabel { border: none; background: transparent; }"));
    auto *detailCardLayout = new QVBoxLayout(detailCard);
    detailCardLayout->setContentsMargins(20, 20, 20, 20);
    detailCardLayout->setSpacing(14);

    auto *detailSummaryRow = new QHBoxLayout();
    detailSummaryRow->setSpacing(12);
    detailOrderNumberLabel_ = new QLabel(detailCard);
    detailOrderNumberLabel_->setObjectName(QStringLiteral("orderDetailNumber"));
    QFont orderNumberFont = detailOrderNumberLabel_->font();
    orderNumberFont.setPointSize(17);
    orderNumberFont.setBold(true);
    detailOrderNumberLabel_->setFont(orderNumberFont);
    detailOrderNumberLabel_->setWordWrap(true);
    detailStatusLabel_ = new QLabel(detailCard);
    detailStatusLabel_->setObjectName(QStringLiteral("orderDetailStatus"));
    detailStatusLabel_->setAlignment(Qt::AlignCenter);
    detailStatusLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    detailSummaryRow->addWidget(detailOrderNumberLabel_, 1);
    detailSummaryRow->addWidget(detailStatusLabel_, 0, Qt::AlignTop);

    auto *separator = new QFrame(detailCard);
    separator->setObjectName(QStringLiteral("orderDetailSeparator"));
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color: #e4e7ec;"));

    detailBodyLabel_ = new QLabel(detailCard);
    detailBodyLabel_->setObjectName(QStringLiteral("orderDetailBody"));
    detailBodyLabel_->setTextFormat(Qt::RichText);
    detailBodyLabel_->setWordWrap(true);
    detailBodyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailBodyLabel_->setStyleSheet(QStringLiteral("color: #475467;"));
    detailMessageLabel_ = new QLabel(detailCard);
    detailMessageLabel_->setObjectName(QStringLiteral("orderDetailMessage"));
    detailMessageLabel_->setWordWrap(true);
    detailMessageLabel_->hide();
    cancelButton_ = new QPushButton(QStringLiteral("取消预约"), detailCard);
    cancelButton_->setObjectName(QStringLiteral("orderDetailCancelButton"));
    navigationButton_ =
        new QPushButton(QStringLiteral("导航到充电站"), detailCard);
    navigationButton_->setObjectName(
        QStringLiteral("orderDetailNavigationButton"));
    reservationScanButton_ =
        new QPushButton(QStringLiteral("前往扫码充电"), detailCard);
    reservationScanButton_->setObjectName(
        QStringLiteral("orderDetailReservationScanButton"));
    stopButton_ = new QPushButton(QStringLiteral("结束充电"), detailCard);
    stopButton_->setObjectName(QStringLiteral("orderDetailStopButton"));
    progressButton_ = new QPushButton(QStringLiteral("刷新充电进度"), detailCard);
    progressButton_->setObjectName(QStringLiteral("orderDetailProgressButton"));
    payButton_ = new QPushButton(QStringLiteral("立即结算"), detailCard);
    payButton_->setObjectName(QStringLiteral("orderDetailPayButton"));
    rechargeButton_ = new QPushButton(QStringLiteral("前往充值"), detailCard);
    rechargeButton_->setObjectName(QStringLiteral("orderDetailRechargeButton"));
    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);
    actionRow->addWidget(rechargeButton_);
    actionRow->addWidget(cancelButton_);
    actionRow->addWidget(reservationScanButton_);
    actionRow->addWidget(progressButton_);
    actionRow->addWidget(stopButton_);
    actionRow->addWidget(payButton_);

    detailCardLayout->addLayout(detailSummaryRow);
    detailCardLayout->addWidget(separator);
    detailCardLayout->addWidget(detailBodyLabel_);
    detailCardLayout->addWidget(detailMessageLabel_);
    detailCardLayout->addSpacing(2);
    detailCardLayout->addWidget(navigationButton_);
    detailCardLayout->addLayout(actionRow);
    detailContentLayout->addWidget(detailCard);
    detailContentLayout->addStretch();
    detailScrollArea->setWidget(detailContent);

    detailLayout->addWidget(backButton, 0, Qt::AlignLeft);
    detailLayout->addWidget(detailHeading);
    detailLayout->addWidget(detailScrollArea, 1);

    pages_->addWidget(listPage_);
    pages_->addWidget(detailPage_);
    pages_->setCurrentWidget(listPage_);

    connect(refreshButton_, &QPushButton::clicked, this, &OrderPage::refreshRequested);
    connect(backButton, &QPushButton::clicked, this, &OrderPage::showListPage);
    connect(cancelButton_, &QPushButton::clicked, this, [this]() {
        emit cancellationRequested(cancelButton_->property("orderId").toLongLong());
    });
    connect(navigationButton_, &QPushButton::clicked, this, [this]() {
        emit navigationRequested(
            navigationButton_->property("stationId").toLongLong());
    });
    connect(reservationScanButton_, &QPushButton::clicked, this, [this]() {
        showListPage();
        emit reservationScanRequested(
            reservationScanButton_->property("pileCode").toString());
    });
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        if (confirmChargingStop(this)) {
            emit stopRequested(stopButton_->property("orderId").toLongLong());
        }
    });
    connect(progressButton_, &QPushButton::clicked, this, [this]() {
        emit progressRequested(progressButton_->property("orderId").toLongLong());
    });
    connect(payButton_, &QPushButton::clicked, this, [this]() {
        emit paymentRequested(payButton_->property("orderId").toLongLong());
    });
    connect(rechargeButton_, &QPushButton::clicked, this, [this]() {
        showListPage();
        emit rechargeRequested();
    });
}

void OrderPage::setLoading(bool loading)
{
    refreshButton_->setDisabled(loading);
    orderListContent_->setDisabled(loading);
    setActionBusy(loading);
    if (loading) {
        showMessage(QStringLiteral("正在获取订单…"));
    }
}

void OrderPage::setActionBusy(bool busy)
{
    actionBusy_ = busy;
    cancelButton_->setDisabled(busy);
    navigationButton_->setDisabled(busy);
    reservationScanButton_->setDisabled(busy);
    stopButton_->setDisabled(busy);
    progressButton_->setDisabled(busy);
    payButton_->setDisabled(busy);
    rechargeButton_->setDisabled(busy);
}

void OrderPage::showOrders(const QList<protocol::OrderDto> &orders)
{
    clearOrderCards();
    setLoading(false);
    messageLabel_->hide();
    for (const auto &order : orders) {
        ordersById_.insert(order.orderId, order);
        auto *card = createCard(orderListContent_, isCurrentStatus(order.status));
        card->setObjectName(QStringLiteral("orderCard_%1").arg(order.orderId));
        card->setAccessibleName(QStringLiteral("查看订单%1详情").arg(order.orderNo));
        card->setActivatedHandler([this, orderId = order.orderId]() {
            showOrderDetail(orderId);
        });
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(6);
        auto *titleRow = new QHBoxLayout();
        auto *station = new QLabel(order.stationName, card);
        QFont stationFont = station->font();
        stationFont.setBold(true);
        station->setFont(stationFont);
        auto *status = new QLabel(orderStatusText(order.status), card);
        status->setObjectName(QStringLiteral("orderStatus_%1").arg(order.orderId));
        status->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                                  .arg(orderStatusColor(order.status)));
        titleRow->addWidget(station, 1);
        titleRow->addWidget(status);
        auto *summary = new QLabel(
            QStringLiteral("充电桩：%1\n创建时间：%2")
                .arg(order.pileCode, formatDateTime(order.createdAt)), card);
        summary->setStyleSheet(QStringLiteral("color: #667085;"));
        auto *bottomRow = new QHBoxLayout();
        auto *amount = new QLabel(
            order.amountCents > 0 ? QStringLiteral("金额：%1").arg(formatMoney(order.amountCents))
                                  : QStringLiteral("金额：待产生"),
            card);
        auto *detailHint = new QLabel(QStringLiteral("点击卡片查看详情  ›"), card);
        detailHint->setObjectName(
            QStringLiteral("orderDetailHint_%1").arg(order.orderId));
        detailHint->setAlignment(Qt::AlignRight);
        detailHint->setStyleSheet(QStringLiteral("color: #1677ff;"));
        for (QLabel *label : {station, status, summary, amount, detailHint}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        bottomRow->addWidget(amount, 1);
        bottomRow->addWidget(detailHint);
        layout->addLayout(titleRow);
        layout->addWidget(summary);
        layout->addLayout(bottomRow);
        orderListLayout_->addWidget(card);
    }
    orderListLayout_->addStretch();

    if (orders.isEmpty()) {
        showMessage(QStringLiteral("暂无订单"));
    }
}

void OrderPage::showError(const QString &message)
{
    setLoading(false);
    showMessage(message, true);
}

void OrderPage::showMessage(const QString &message, bool error)
{
    messageLabel_->setText(message);
    messageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                       : QStringLiteral("color: #667085;"));
    messageLabel_->setVisible(!message.isEmpty());
}

void OrderPage::showDetailMessage(const QString &message, bool error)
{
    detailMessageLabel_->setText(message);
    detailMessageLabel_->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                             : QStringLiteral("color: #667085;"));
    detailMessageLabel_->setVisible(!message.isEmpty());
}

void OrderPage::updateOrderDetail(const protocol::OrderDto &order)
{
    ordersById_.insert(order.orderId, order);
    showOrderDetail(order.orderId);
}

void OrderPage::showListPage()
{
    pages_->setCurrentWidget(listPage_);
}

void OrderPage::reset()
{
    setLoading(false);
    setActionBusy(false);
    clearOrderCards();
    messageLabel_->hide();
    detailMessageLabel_->hide();
    pages_->setCurrentWidget(listPage_);
}

void OrderPage::clearOrderCards()
{
    ordersById_.clear();
    while (QLayoutItem *item = orderListLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void OrderPage::showOrderDetail(qint64 orderId)
{
    if (!ordersById_.contains(orderId)) {
        return;
    }
    const protocol::OrderDto order = ordersById_.value(orderId);
    detailOrderNumberLabel_->setText(QStringLiteral("订单 %1").arg(order.orderNo));
    detailStatusLabel_->setText(orderStatusText(order.status));
    detailStatusLabel_->setStyleSheet(
        QStringLiteral("color: %1; background: %2; border-radius: 10px; "
                       "padding: 5px 10px; font-weight: 700;")
            .arg(orderStatusColor(order.status),
                 order.status == protocol::OrderStatus::Completed
                     ? QStringLiteral("#e8f7ee")
                 : order.status == protocol::OrderStatus::Cancelled
                     ? QStringLiteral("#f2f4f7")
                 : order.status == protocol::OrderStatus::PendingPayment
                     ? QStringLiteral("#fff0ee")
                     : QStringLiteral("#eaf2ff")));

    QString detailTable = QStringLiteral(
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    QStringList accessibleDetails;
    const auto appendDetail = [&detailTable, &accessibleDetails](
                                  const QString &label,
                                  const QString &value) {
        detailTable += QStringLiteral(
                           "<tr><td width=\"92\" valign=\"top\" "
                           "style=\"padding: 0 12px 9px 0; color: #667085; "
                           "white-space: nowrap;\">%1：</td>"
                           "<td valign=\"top\" style=\"padding: 0 0 9px 0; "
                           "color: #1d2939;\">%2</td></tr>")
                           .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
        accessibleDetails.append(QStringLiteral("%1：%2").arg(label, value));
    };
    appendDetail(QStringLiteral("充电站"), order.stationName);
    appendDetail(QStringLiteral("充电桩"), order.pileCode);
    appendDetail(QStringLiteral("充电方式"), orderModeText(order.mode));
    appendDetail(QStringLiteral("创建时间"), formatDateTime(order.createdAt));
    if (order.reservedAt.has_value()) {
        appendDetail(QStringLiteral("预约时间"),
                     formatDateTime(*order.reservedAt));
    }
    if (order.startedAt.has_value()) {
        appendDetail(QStringLiteral("开始时间"),
                     formatDateTime(*order.startedAt));
    }
    if (order.endedAt.has_value()) {
        appendDetail(QStringLiteral("结束时间"),
                     formatDateTime(*order.endedAt));
    }
    if (order.paidAt.has_value()) {
        appendDetail(QStringLiteral("支付时间"),
                     formatDateTime(*order.paidAt));
    }
    if (order.startedAt.has_value() || order.endedAt.has_value()) {
        appendDetail(QStringLiteral("充电时长"),
                     formatDuration(order.durationSeconds));
        appendDetail(QStringLiteral("充电量"),
                     QStringLiteral("%1 度").arg(
                         order.energyWh / 1000.0, 0, 'f', 2));
    }
    if (order.unitPriceCentsPerKwh.has_value()) {
        appendDetail(QStringLiteral("订单单价"),
                     QStringLiteral("%1/度").arg(
                         formatMoney(*order.unitPriceCentsPerKwh)));
    }
    if (order.amountCents > 0) {
        appendDetail(QStringLiteral("订单金额"),
                     formatMoney(order.amountCents));
    }
    detailTable += QStringLiteral("</table>");
    detailBodyLabel_->setText(detailTable);
    detailBodyLabel_->setAccessibleDescription(
        accessibleDetails.join(QChar('\n')));
    cancelButton_->setProperty("orderId", order.orderId);
    navigationButton_->setProperty("stationId", order.stationId);
    navigationButton_->setVisible(
        order.status == protocol::OrderStatus::Reserved
        || order.status == protocol::OrderStatus::Charging);
    navigationButton_->setDisabled(actionBusy_);
    cancelButton_->setVisible(order.status == protocol::OrderStatus::Reserved);
    cancelButton_->setDisabled(actionBusy_);
    reservationScanButton_->setProperty("pileCode", order.pileCode);
    reservationScanButton_->setVisible(
        order.status == protocol::OrderStatus::Reserved);
    reservationScanButton_->setDisabled(actionBusy_);
    stopButton_->setProperty("orderId", order.orderId);
    stopButton_->setVisible(order.status == protocol::OrderStatus::Charging);
    stopButton_->setDisabled(actionBusy_);
    progressButton_->setProperty("orderId", order.orderId);
    progressButton_->setVisible(order.status == protocol::OrderStatus::Charging);
    progressButton_->setDisabled(actionBusy_);
    payButton_->setProperty("orderId", order.orderId);
    const bool pendingPayment =
        order.status == protocol::OrderStatus::PendingPayment;
    payButton_->setVisible(pendingPayment);
    rechargeButton_->setVisible(pendingPayment);
    payButton_->setDisabled(actionBusy_);
    rechargeButton_->setDisabled(actionBusy_);
    detailMessageLabel_->hide();
    pages_->setCurrentWidget(detailPage_);
}

}  // namespace charging::client
