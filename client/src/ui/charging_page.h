// 文件用途：充电页界面的声明
#pragma once
#include "charging/protocol/dto.h"
#include <QWidget>
#include <optional>
class QLabel;
class QPushButton;
namespace charging::client {
class ChargingRing;
class PricingInfoButton;
class ChargingPage final : public QWidget {
    Q_OBJECT
public:
    explicit ChargingPage(QWidget *parent = nullptr);
    // 页面数据入口：准备桩号、显示订单
    void prepare(const QString &pileCode);
    void showOrder(const protocol::OrderDto &order);
    // 报价的加载、成功与失败三种展示
    void clearQuote();
    void showQuote(const protocol::StationDto &station);
    void showQuoteError(const QString &message);
    void setBusy(bool busy);
    void showMessage(const QString &message, bool error = false);
    void reset();
    QString pileCode() const { return pileCode_; }
// 用户操作以信号形式交给控制器处理
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
    // render 统一根据成员状态重绘界面
    void render();
    QString pileCode_;
    std::optional<protocol::OrderDto> order_;
    // quote_ 为参考价站点，quoteError_ 记录失败提示
    std::optional<protocol::StationDto> quote_;
    QString quoteError_;
    bool quoteLoading_ = false;
    bool busy_ = false;
    // 页面内各标签与按钮控件
    ChargingRing *ring_;
    QLabel *state_, *station_, *message_, *power_, *energy_, *duration_, *amount_, *price_;
    QLabel *reservationHint_;
    PricingInfoButton *pricingInfo_;
    QPushButton *start_, *stop_, *home_, *scan_, *recharge_, *orders_, *repair_;
};
}
