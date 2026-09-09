#pragma once
#include "charging/protocol/dto.h"
#include <QWidget>
#include <optional>
class QLabel;
class QPushButton;
namespace charging::client {
class ChargingProgressRing;
class PricingInfoButton;
class ChargingPage final : public QWidget {
    Q_OBJECT
public:
    explicit ChargingPage(QWidget *parent = nullptr);
    void prepare(const QString &pileCode);
    void showOrder(const protocol::OrderDto &order);
    void clearQuote();
    void showQuote(const protocol::StationDto &station);
    void showQuoteError(const QString &message);
    void setBusy(bool busy);
    void showMessage(const QString &message, bool error = false);
    void reset();
    QString pileCode() const { return pileCode_; }
signals:
    void startRequested(const QString &pileCode);
    void quoteRetryRequested();
    void stopRequested();
    void homeRequested();
    void scanRequested();
    void rechargeRequested();
    void ordersRequested();
    void repairRequested(const QString &pileCode);
private:
    void render();
    QString pileCode_;
    std::optional<protocol::OrderDto> order_;
    std::optional<protocol::StationDto> quote_;
    QString quoteError_;
    bool quoteLoading_ = false;
    bool busy_ = false;
    ChargingProgressRing *ring_;
    QLabel *state_, *station_, *message_, *power_, *energy_, *duration_, *amount_, *price_;
    QLabel *reservationHint_;
    PricingInfoButton *pricingInfo_;
    QPushButton *start_, *stop_, *home_, *scan_, *recharge_, *orders_, *repair_;
};
}
