#pragma once
#include "api/api_result.h"
#include <QObject>
class QTimer;
namespace charging::client {
class IChargingApi;
class ChargingPage;
class ChargingController final : public QObject {
    Q_OBJECT
public:
    ChargingController(ChargingPage &page,IChargingApi &api,QObject *parent=nullptr);
    void prepare(const QString &pileCode);
    void activate();
    void refresh();
    void reset();
signals:
    void authenticationRequired(const QString &message);
    void orderChanged(const charging::protocol::OrderDto &order);
    void sessionStarted();
    void sessionFinished();
private:
    enum class Action {None, Refresh, StartCheck, Start, Stop, History};
    void begin(const QString &pileCode);
    void start(std::optional<qint64> reservation);
    void stop();
    void apply(const protocol::OrderDto &order);
    bool accept(const ApiResponse &response,const char *type);
    void finish();
    void requestQuote();
    void requestNextStation();
    void clearQuoteRequest();
    bool acceptQuote(const ApiResponse &response);
    ChargingPage &page_;
    IChargingApi &api_;
    QTimer *timer_;
    Action action_=Action::None;
    QString requestId_,candidate_;
    QString quoteRequestId_;
    QList<qint64> quoteStations_;
    std::optional<protocol::StationDto> quote_;
    std::optional<protocol::OrderDto> order_;
    bool active_=false;
};
}
