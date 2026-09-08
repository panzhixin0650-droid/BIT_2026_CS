#include "ui/charging_controller.h"
#include "ui/charging_page.h"
#include "ui/charging_stop_dialog.h"
#include "ui/api_error_message.h"
#include "api/i_charging_api.h"
#include <QTimer>
namespace charging::client {
using S=protocol::OrderStatus;
using namespace protocol::MessageType;
ChargingController::ChargingController(ChargingPage &page,IChargingApi &api,QObject *parent)
    :QObject(parent),page_(page),api_(api),timer_(new QTimer(this)) {
    timer_->setInterval(1000);connect(timer_,&QTimer::timeout,this,&ChargingController::refresh);
    connect(&page_,&ChargingPage::startRequested,this,&ChargingController::begin);
    connect(&page_,&ChargingPage::stopRequested,this,&ChargingController::stop);
    connect(&page_, &ChargingPage::quoteRetryRequested, this, &ChargingController::requestQuote);
    connect(&api_, &IChargingApi::stationListCompleted, this, [this](const StationListResult &r) {
        if (r.response.type != StationList || !acceptQuote(r.response)) return;
        if (!r.payload) {
            clearQuoteRequest();
            page_.showQuoteError(QStringLiteral("价格响应不完整，请重试加载，暂未产生费用"));
            return;
        }
        for (const auto &station : r.payload->items) quoteStations_.append(station.stationId);
        requestNextStation();
    });
    connect(&api_, &IChargingApi::stationDetailCompleted, this, [this](const StationDetailResult &r) {
        if (r.response.type != StationDetail || !acceptQuote(r.response)) return;
        if (!r.payload) {
            clearQuoteRequest();
            page_.showQuoteError(QStringLiteral("价格响应不完整，请重试加载，暂未产生费用"));
            return;
        }
        for (const auto &pile : r.payload->piles) {
            if (pile.pileCode == candidate_ && pile.stationId == r.payload->station.stationId
                && r.payload->station.priceCentsPerKwh > 0) {
                quote_ = r.payload->station;
                quoteRequestId_.clear();
                quoteStations_.clear();
                page_.showQuote(*quote_);
                return;
            }
        }
        requestNextStation();
    });
    connect(&api_,&IChargingApi::currentOrderCompleted,this,[this](const CurrentOrderResult &r){
        if(r.response.requestId!=requestId_||r.response.type!=OrderCurrent)return;
        const bool starting=action_==Action::StartCheck;
        if(!accept(r.response,OrderCurrent))return;
        if(!r.payload){finish();page_.showMessage(QStringLiteral("订单响应不完整，请重试"),true);return;}
        if(starting){
            if(!r.payload->order){start({});return;}
            auto current=*r.payload->order;
            if(current.status==S::Reserved&&current.pileCode==candidate_){start(current.orderId);return;}
            finish();apply(current);page_.showMessage(QStringLiteral("请先处理当前订单；预约充电请使用对应充电桩"),true);return;
        }
        if(r.payload->order){auto current=*r.payload->order;finish();apply(current);return;}
        if(order_&&(order_->status==S::Charging||order_->status==S::PendingPayment)){action_=Action::History;requestId_=api_.listOrders();return;}
        if(order_&&order_->status==S::Reserved){clearQuoteRequest();order_.reset();candidate_.clear();page_.reset();}
        finish();
    });
    connect(&api_,&IChargingApi::chargingStartCompleted,this,[this](const OrderResult&r){
        if(!accept(r.response,OrderStart))return;
        if(!r.payload){finish();return;}auto order=r.payload->order;finish();apply(order);page_.showMessage(QStringLiteral("充电已开始，您可以随时提前结束"));
    });
    connect(&api_,&IChargingApi::chargingStopCompleted,this,[this](const ChargingStopResult&r){
        if(r.response.requestId!=requestId_||r.response.type!=OrderStop)return;
        if(r.response.code==protocol::ErrorCode::IllegalOrderState){finish();refresh();return;}
        if(!accept(r.response,OrderStop))return;
        if(!r.payload){finish();return;}auto order=r.payload->order;finish();apply(order);
    });
    connect(&api_,&IChargingApi::orderListCompleted,this,[this](const OrderListResult&r){
        if(!accept(r.response,OrderList))return;
        if(r.payload&&order_){for(const auto&o:r.payload->items)if(o.orderId==order_->orderId){finish();apply(o);return;}}
        finish();page_.showMessage(QStringLiteral("暂未取得结束结果，请稍后重试"),true);
    });
}
bool ChargingController::accept(const ApiResponse&r,const char*type){
    if(requestId_.isEmpty()||r.requestId!=requestId_||r.type!=QString::fromLatin1(type))return false;
    if(r.code==protocol::ErrorCode::InvalidSession){reset();emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));return false;}
    if(!r.ok()){finish();page_.showMessage(apiErrorMessage(r,QStringLiteral("操作失败，请重试")),true);return false;}
    return true;
}
void ChargingController::activate(){active_=true;timer_->start();refresh();if(quoteRequestId_.isEmpty())requestQuote();}
void ChargingController::prepare(const QString &code){
    if(action_!=Action::None&&action_!=Action::Refresh)return;
    if(order_&&(order_->status==S::Charging||order_->status==S::PendingPayment)){page_.showOrder(*order_);return;}
    clearQuoteRequest();candidate_=code.trimmed();order_.reset();page_.prepare(candidate_);requestQuote();refresh();
}
void ChargingController::refresh(){if(!active_||action_!=Action::None)return;action_=Action::Refresh;requestId_=api_.getCurrentOrder();}
void ChargingController::begin(const QString &code){
    if(!active_||(action_!=Action::None&&action_!=Action::Refresh))return;
    const bool refreshing=action_==Action::Refresh;
    const auto requested = code.trimmed();
    if (requested.isEmpty()) { page_.showMessage(QStringLiteral("请先选择充电桩"), true); return; }
    if (requested != candidate_) { prepare(requested); return; }
    if (!quote_) { requestQuote(); return; }
    page_.setBusy(true);page_.showMessage(QStringLiteral("正在确认充电条件…"));action_=Action::StartCheck;if(!refreshing)requestId_=api_.getCurrentOrder();
}
void ChargingController::start(std::optional<qint64> reservation){action_=Action::Start;requestId_=api_.startCharging(candidate_,reservation);}
void ChargingController::stop(){
    if(action_==Action::Refresh)finish();
    if(action_!=Action::None||!order_||order_->status!=S::Charging)return;
    // Pause refresh while the confirmation dialog runs its nested event loop.
    const qint64 id=order_->orderId;timer_->stop();const bool confirmed=confirmChargingStop(&page_);if(active_)timer_->start();
    if(!confirmed||!active_)return;
    page_.setBusy(true);page_.showMessage(QStringLiteral("正在停止并结算…"));action_=Action::Stop;requestId_=api_.stopCharging(id);
}
void ChargingController::apply(const protocol::OrderDto &order){
    const bool started=order.status==S::Charging&&(!order_||order_->status!=S::Charging||order_->orderId!=order.orderId);
    const bool ended=order_&&order_->status==S::Charging&&order.status!=S::Charging;
    const bool newReservation = order.status == S::Reserved
        && (!order_ || order_->orderId != order.orderId || candidate_ != order.pileCode);
    if (order.status != S::Reserved || newReservation) clearQuoteRequest();
    order_=order;candidate_=order.pileCode;page_.showOrder(order);
    if (newReservation) requestQuote();
    emit orderChanged(order);
    if(started)emit sessionStarted();
    if(ended){page_.showMessage(order.status==S::PendingPayment?QStringLiteral("充电已结束，余额不足，请前往充值结算"):QStringLiteral("充电已结束并完成结算"));emit sessionFinished();}
}
void ChargingController::finish(){requestId_.clear();action_=Action::None;page_.setBusy(false);}
void ChargingController::reset(){active_=false;timer_->stop();finish();clearQuoteRequest();candidate_.clear();order_.reset();page_.reset();}

void ChargingController::clearQuoteRequest()
{
    quoteRequestId_.clear();
    quoteStations_.clear();
    quote_.reset();
}

void ChargingController::requestQuote()
{
    if (candidate_.isEmpty() || (order_ && order_->status != S::Reserved)) return;
    clearQuoteRequest();
    page_.clearQuote();
    // Reserved orders identify the station. A scanned code needs the existing
    // list/detail APIs to resolve it; the Demo has only a few stations.
    quoteRequestId_ = order_ ? api_.getStation(order_->stationId) : api_.listStations({});
}

void ChargingController::requestNextStation()
{
    if (quoteStations_.isEmpty()) {
        clearQuoteRequest();
        page_.showQuoteError(QStringLiteral("未找到该桩的参考价，请重新选桩或重试加载"));
        return;
    }
    quoteRequestId_ = api_.getStation(quoteStations_.takeFirst());
}

bool ChargingController::acceptQuote(const ApiResponse &response)
{
    if (quoteRequestId_.isEmpty() || response.requestId != quoteRequestId_) return false;
    if (response.code == protocol::ErrorCode::InvalidSession) {
        reset();
        emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));
        return false;
    }
    if (!response.ok()) {
        clearQuoteRequest();
        page_.showQuoteError(apiErrorMessage(response, QStringLiteral("价格加载失败，请重试加载")));
        return false;
    }
    return true;
}
}
