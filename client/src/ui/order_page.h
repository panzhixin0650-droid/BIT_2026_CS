#pragma once

#include "charging/protocol/dto.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace charging::client {

// 订单页：列表与详情两层界面
class OrderPage final : public QWidget {
    Q_OBJECT

public:
    explicit OrderPage(QWidget *parent = nullptr);

    // 由控制器驱动的加载、展示与提示接口
    void setLoading(bool loading);
    void setActionBusy(bool busy);
    void showOrders(const QList<protocol::OrderDto> &orders);
    void showError(const QString &message);
    void showMessage(const QString &message, bool error = false);
    void showDetailMessage(const QString &message, bool error = false);
    bool updateOrderDetail(const protocol::OrderDto &order);
    void showListPage();
    void reset();

// 用户操作以信号形式交给控制器处理
signals:
    void refreshRequested();
    void cancellationRequested(qint64 orderId);
    void reservationScanRequested(const QString &pileCode);
    void navigationRequested(qint64 stationId);
    void stopRequested(qint64 orderId);
    void progressRequested(qint64 orderId);
    void paymentRequested(qint64 orderId);
    void rechargeRequested();

private:
    // 内部维护卡片清理与详情渲染
    void clearOrderCards();
    void showOrderDetail(qint64 orderId);

    // 页面栈与列表、详情的关键控件
    QStackedWidget *pages_ = nullptr;
    QWidget *listPage_ = nullptr;
    QWidget *detailPage_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QLabel *messageLabel_ = nullptr;
    QWidget *orderListContent_ = nullptr;
    QVBoxLayout *orderListLayout_ = nullptr;
    QLabel *detailOrderNumberLabel_ = nullptr;
    QLabel *detailStatusLabel_ = nullptr;
    QLabel *detailBodyLabel_ = nullptr;
    QLabel *detailMessageLabel_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QPushButton *navigationButton_ = nullptr;
    QPushButton *reservationScanButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *progressButton_ = nullptr;
    QPushButton *payButton_ = nullptr;
    QPushButton *rechargeButton_ = nullptr;
    // 缓存订单数据与当前展示的订单编号
    QHash<qint64, protocol::OrderDto> ordersById_;
    qint64 displayedOrderId_ = 0;
    bool actionBusy_ = false;
};

}  // namespace charging::client
