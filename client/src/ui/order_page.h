#pragma once

#include "charging/protocol/dto.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace charging::client {

class OrderPage final : public QWidget {
    Q_OBJECT

public:
    explicit OrderPage(QWidget *parent = nullptr);

    void setLoading(bool loading);
    void setActionBusy(bool busy);
    void showOrders(const QList<protocol::OrderDto> &orders);
    void showError(const QString &message);
    void showMessage(const QString &message, bool error = false);
    void showDetailMessage(const QString &message, bool error = false);
    void updateOrderDetail(const protocol::OrderDto &order);
    void showListPage();
    void reset();

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
    void clearOrderCards();
    void showOrderDetail(qint64 orderId);

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
    QHash<qint64, protocol::OrderDto> ordersById_;
    bool actionBusy_ = false;
};

}  // namespace charging::client
