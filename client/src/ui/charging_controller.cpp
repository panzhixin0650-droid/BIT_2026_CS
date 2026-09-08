#include "ui/charging_controller.h"
#include "ui/charging_page.h"
#include "ui/charging_stop_dialog.h"
#include "ui/api_error_message.h"
#include "common/charging_session_state.h"
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
    connect(&api_,&IChargingApi::currentOrderCompleted,this,[this](const CurrentOrderResult &r){
        if(r.response.requestId!=requestId_||r.response.type!=OrderCurrent)return;
        const bool starting=action_==Action::StartCheck;
        if(!accept(r.response,OrderCurrent))return;
        if(!r.payload){finish();page_.showMessage(QStringLiteral("订单响应不完整，请重试"),true);return;}
        if(starting){
            const auto decision=session::startDecision(r.payload->order,candidate_);
            if(decision==session::StartDecision::Direct){start({});return;}
            if(decision==session::StartDecision::UseReservation){start(r.payload->order->orderId);return;}
            auto current=*r.payload->order;
            finish();apply(current);page_.showMessage(QStringLiteral("请先处理当前订单；预约充电请使用对应充电桩"),true);return;
        }
        if(r.payload->order){auto current=*r.payload->order;finish();apply(current);return;}
        if(session::needsFinalHistory(order_)){action_=Action::History;requestId_=api_.listOrders();return;}
        if(order_&&order_->status==S::Reserved){order_.reset();candidate_.clear();page_.reset();}
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
void ChargingController::activate(){active_=true;timer_->start();refresh();}
void ChargingController::prepare(const QString &code){
    if(action_!=Action::None&&action_!=Action::Refresh)return;
    if(order_&&(order_->status==S::Charging||order_->status==S::PendingPayment)){page_.showOrder(*order_);return;}
    candidate_=code.trimmed();order_.reset();page_.prepare(candidate_);refresh();
}
void ChargingController::refresh(){if(!active_||action_!=Action::None)return;action_=Action::Refresh;requestId_=api_.getCurrentOrder();}
void ChargingController::begin(const QString &code){
    if(!active_||(action_!=Action::None&&action_!=Action::Refresh))return;
    const bool refreshing=action_==Action::Refresh;
    candidate_=code.trimmed();if(candidate_.isEmpty()){page_.showMessage(QStringLiteral("请先选择充电桩"),true);return;}
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
    order_=order;candidate_=order.pileCode;page_.showOrder(order);emit orderChanged(order);
    if(started)emit sessionStarted();
    if(ended){page_.showMessage(order.status==S::PendingPayment?QStringLiteral("充电已结束，余额不足，请前往充值结算"):QStringLiteral("充电已结束并完成结算"));emit sessionFinished();}
}
void ChargingController::finish(){requestId_.clear();action_=Action::None;page_.setBusy(false);}
void ChargingController::reset(){active_=false;timer_->stop();finish();candidate_.clear();order_.reset();page_.reset();}
}
