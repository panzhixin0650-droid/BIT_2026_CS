// 文件用途：充电页控制器的声明
#pragma once
#include "api/api_result.h"
#include <QObject>
class QTimer;
namespace charging::client {
class IChargingApi;
class ChargingPage;
// 控制器把页面操作转成接口调用，并回填订单状态
class ChargingController final : public QObject {
    Q_OBJECT
public:
    ChargingController(ChargingPage &page,IChargingApi &api,QObject *parent=nullptr);
    // prepare 设定候选桩，activate 启动轮询
    void prepare(const QString &pileCode);
    void activate();
    void refresh();
    void reset();
// 对外信号：登录失效、订单变化、会话开始结束与预约释放
signals:
    void authenticationRequired(const QString &message);
    void orderChanged(const charging::protocol::OrderDto &order);
    void sessionStarted();
    void sessionFinished();
    void reservationReleased();
private:
    // 记录当前在办的请求类型，防止操作并发
    enum class Action {None, Refresh, StartCheck, Start, Stop, History};
    void begin(const QString &pileCode);
    void start(std::optional<qint64> reservation);
    void stop();
    void apply(const protocol::OrderDto &order);
    bool accept(const ApiResponse &response,const char *type);
    void finish();
    // 参考价加载的内部步骤
    void requestQuote();
    void requestNextStation();
    void clearQuoteRequest();
    bool acceptQuote(const ApiResponse &response);
    ChargingPage &page_;
    IChargingApi &api_;
    QTimer *timer_;
    Action action_=Action::None;
    // requestId_ 标识在途请求，candidate_ 是当前选中桩
    QString requestId_,candidate_;
    QString quoteRequestId_;
    QList<qint64> quoteStations_;
    // quote_ 缓存参考价站点，order_ 缓存最新订单
    std::optional<protocol::StationDto> quote_;
    std::optional<protocol::OrderDto> order_;
    bool active_=false;
};
}
