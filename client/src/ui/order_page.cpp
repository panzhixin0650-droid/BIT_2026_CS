#include "ui/order_page.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringList>
#include <QVBoxLayout>

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

QFrame *createCard(QWidget *parent, bool highlighted = false)
{
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(highlighted
            ? QStringLiteral("QFrame { background: #f5f9ff; border: 2px solid #91caff; "
                             "border-radius: 12px; } QLabel { border: none; }")
            : QStringLiteral("QFrame { background: white; border: 1px solid #e4e7ec; "
                             "border-radius: 12px; } QLabel { border: none; }"));
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
    QFont headingFont = heading->font();
    headingFont.setPointSize(20);
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
    detailOrderNumberLabel_ = new QLabel(detailPage_);
    detailOrderNumberLabel_->setObjectName(QStringLiteral("orderDetailNumber"));
    QFont orderNumberFont = detailOrderNumberLabel_->font();
    orderNumberFont.setPointSize(18);
    orderNumberFont.setBold(true);
    detailOrderNumberLabel_->setFont(orderNumberFont);
    detailStatusLabel_ = new QLabel(detailPage_);
    detailStatusLabel_->setObjectName(QStringLiteral("orderDetailStatus"));
    detailBodyLabel_ = new QLabel(detailPage_);
    detailBodyLabel_->setObjectName(QStringLiteral("orderDetailBody"));
    detailBodyLabel_->setWordWrap(true);
    detailBodyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailMessageLabel_ = new QLabel(detailPage_);
    detailMessageLabel_->setObjectName(QStringLiteral("orderDetailMessage"));
    detailMessageLabel_->setWordWrap(true);
    detailMessageLabel_->hide();
    cancelButton_ = new QPushButton(QStringLiteral("取消预约"), detailPage_);
    cancelButton_->setObjectName(QStringLiteral("orderDetailCancelButton"));

    detailLayout->addWidget(backButton, 0, Qt::AlignLeft);
    detailLayout->addWidget(detailOrderNumberLabel_);
    detailLayout->addWidget(detailStatusLabel_);
    detailLayout->addWidget(detailBodyLabel_);
    detailLayout->addWidget(detailMessageLabel_);
    detailLayout->addWidget(cancelButton_, 0, Qt::AlignRight);
    detailLayout->addStretch();

    pages_->addWidget(listPage_);
    pages_->addWidget(detailPage_);
    pages_->setCurrentWidget(listPage_);

    connect(refreshButton_, &QPushButton::clicked, this, &OrderPage::refreshRequested);
    connect(backButton, &QPushButton::clicked, this, &OrderPage::showListPage);
    connect(cancelButton_, &QPushButton::clicked, this, [this]() {
        emit cancellationRequested(cancelButton_->property("orderId").toLongLong());
    });
}

void OrderPage::setLoading(bool loading)
{
    refreshButton_->setDisabled(loading);
    orderListContent_->setDisabled(loading);
    if (loading) {
        showMessage(QStringLiteral("正在获取订单…"));
    }
}

void OrderPage::setActionBusy(bool busy)
{
    actionBusy_ = busy;
    cancelButton_->setDisabled(busy);
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
        auto *detailButton = new QPushButton(QStringLiteral("查看详情"), card);
        detailButton->setObjectName(
            QStringLiteral("orderDetailButton_%1").arg(order.orderId));
        connect(detailButton, &QPushButton::clicked, this, [this, orderId = order.orderId]() {
            showOrderDetail(orderId);
        });
        bottomRow->addWidget(amount, 1);
        bottomRow->addWidget(detailButton);
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
    detailStatusLabel_->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                                          .arg(orderStatusColor(order.status)));

    QStringList details{
        QStringLiteral("充电站：%1").arg(order.stationName),
        QStringLiteral("充电桩：%1").arg(order.pileCode),
        QStringLiteral("充电方式：%1").arg(orderModeText(order.mode)),
        QStringLiteral("创建时间：%1").arg(formatDateTime(order.createdAt)),
    };
    if (order.reservedAt.has_value()) {
        details.append(QStringLiteral("预约时间：%1").arg(formatDateTime(*order.reservedAt)));
    }
    if (order.startedAt.has_value()) {
        details.append(QStringLiteral("开始时间：%1").arg(formatDateTime(*order.startedAt)));
    }
    if (order.endedAt.has_value()) {
        details.append(QStringLiteral("结束时间：%1").arg(formatDateTime(*order.endedAt)));
    }
    if (order.paidAt.has_value()) {
        details.append(QStringLiteral("支付时间：%1").arg(formatDateTime(*order.paidAt)));
    }
    if (order.startedAt.has_value() || order.endedAt.has_value()) {
        details.append(QStringLiteral("充电时长：%1").arg(formatDuration(order.durationSeconds)));
        details.append(QStringLiteral("充电量：%1 度").arg(order.energyWh / 1000.0, 0, 'f', 2));
    }
    if (order.unitPriceCentsPerKwh.has_value()) {
        details.append(QStringLiteral("订单单价：%1/度")
                           .arg(formatMoney(*order.unitPriceCentsPerKwh)));
    }
    if (order.amountCents > 0) {
        details.append(QStringLiteral("订单金额：%1").arg(formatMoney(order.amountCents)));
    }
    detailBodyLabel_->setText(details.join(QChar('\n')));
    cancelButton_->setProperty("orderId", order.orderId);
    cancelButton_->setVisible(order.status == protocol::OrderStatus::Reserved);
    cancelButton_->setDisabled(actionBusy_);
    detailMessageLabel_->hide();
    pages_->setCurrentWidget(detailPage_);
}

}  // namespace charging::client
