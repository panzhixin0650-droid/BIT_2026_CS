// 文件用途：充电页控制器，负责订单轮询、开始与结束充电的流程编排
#include "ui/charging_controller.h"
#include "ui/charging_page.h"
#include "ui/charging_stop_dialog.h"
#include "ui/api_error_message.h"
#include "api/i_charging_api.h"
#include <QTimer>
namespace charging::client {
using S=protocol::OrderStatus;
using namespace protocol::MessageType;
// 构造时接好每秒轮询定时器、页面按钮与各接口回调
ChargingController::ChargingController(ChargingPage &page,IChargingApi &api,QObject *parent)
    :QObject(parent),page_(page),api_(api),timer_(new QTimer(this)) {
    timer_->setInterval(1000);connect(timer_,&QTimer::timeout,this,&ChargingController::refresh);
    connect(&page_,&ChargingPage::startRequested,this,&ChargingController::begin);
    connect(&page_,&ChargingPage::stopRequested,this,&ChargingController::stop);
    connect(&page_, &ChargingPage::quoteRetryRequested, this, &ChargingController::requestQuote);
    // 站点列表回调：收集站点ID，逐个查详情来找参考价
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
    // 站点详情里匹配到目标桩且有单价时，展示参考价
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
    // 当前订单回调：区分普通刷新与开始充电前的条件确认
    connect(&api_,&IChargingApi::currentOrderCompleted,this,[this](const CurrentOrderResult &r){
        if(r.response.requestId!=requestId_||r.response.type!=OrderCurrent)return;
        const bool starting=action_==Action::StartCheck;
        if(!accept(r.response,OrderCurrent))return;
        if(!r.payload){finish();page_.showMessage(QStringLiteral("订单响应不完整，请重试"),true);return;}
        if(starting){
            if (order_ && order_->status == S::Reserved && r.payload->order
                && r.payload->order->orderId != order_->orderId) {
                auto current = *r.payload->order;
                finish();apply(current);
                page_.showMessage(QStringLiteral("当前订单已变化，请重新确认"), true);
                return;
            }
            if (!r.payload->order && order_ && order_->status == S::Reserved) {
                // The reservation disappeared during confirmation. Resolve its final
                // state; never silently fall through into a new DIRECT order.
                page_.showMessage(QStringLiteral("预约已失效，正在刷新订单，请重新选桩"), true);
                action_ = Action::History;
                requestId_ = api_.listOrders();
                return;
            }
            if(!r.payload->order){start({});return;}
            auto current=*r.payload->order;
            if(current.status==S::Reserved&&current.pileCode==candidate_){start(current.orderId);return;}
            finish();apply(current);page_.showMessage(QStringLiteral("请先处理当前订单；预约充电请使用对应充电桩"),true);return;
        }
        if(r.payload->order){auto current=*r.payload->order;finish();apply(current);return;}
        if(order_&&(order_->status==S::Charging||order_->status==S::PendingPayment||order_->status==S::Reserved)){
            page_.setBusy(true);action_=Action::History;requestId_=api_.listOrders();return;
        }
        finish();
    });
    // 开始充电结果：状态已变化时提示并重新刷新订单
    connect(&api_,&IChargingApi::chargingStartCompleted,this,[this](const OrderResult&r){
        if (r.response.requestId == requestId_ && r.response.type == OrderStart
            && r.response.code == protocol::ErrorCode::IllegalOrderState) {
            finish();
            page_.showMessage(QStringLiteral("预约或电桩状态已变化，正在刷新，请重新确认"), true);
            refresh();
            page_.setBusy(true);
            return;
        }
        if(!accept(r.response,OrderStart))return;
        if(!r.payload){finish();return;}auto order=r.payload->order;finish();apply(order);page_.showMessage(QStringLiteral("充电已开始，您可以随时提前结束"));
    });
    // 停止充电结果：状态非法时改用刷新取回真实状态
    connect(&api_,&IChargingApi::chargingStopCompleted,this,[this](const ChargingStopResult&r){
        if(r.response.requestId!=requestId_||r.response.type!=OrderStop)return;
        if(r.response.code==protocol::ErrorCode::IllegalOrderState){finish();refresh();return;}
        if(!accept(r.response,OrderStop))return;
        if(!r.payload){finish();return;}auto order=r.payload->order;finish();apply(order);
    });
    // 兜底流程：从订单列表里找回本单的最终结果
    connect(&api_,&IChargingApi::orderListCompleted,this,[this](const OrderListResult&r){
        if(!accept(r.response,OrderList))return;
        if(r.payload&&order_){for(const auto&o:r.payload->items)if(o.orderId==order_->orderId){finish();apply(o);return;}}
        finish();page_.showMessage(QStringLiteral("暂未取得结束结果，请稍后重试"),true);
    });
}
// 校验响应是否属于本次请求，会话失效则要求重新登录
bool ChargingController::accept(const ApiResponse&r,const char*type){
    if(requestId_.isEmpty()||r.requestId!=requestId_||r.type!=QString::fromLatin1(type))return false;
    if(r.code==protocol::ErrorCode::InvalidSession){reset();emit authenticationRequired(QStringLiteral("登录状态已失效，请重新登录"));return false;}
    if(!r.ok()){finish();page_.showMessage(apiErrorMessage(r,QStringLiteral("操作失败，请重试")),true);return false;}
    return true;
}
// 进入页面：启动轮询并加载参考价
void ChargingController::activate(){active_=true;timer_->start();refresh();if(quoteRequestId_.isEmpty())requestQuote();}
// prepare 按扫码或预约的桩号准备页面与报价
void ChargingController::prepare(const QString &code){
    if(action_!=Action::None&&action_!=Action::Refresh)return;
    if(order_&&(order_->status==S::Charging||order_->status==S::PendingPayment)){page_.showOrder(*order_);return;}
    if (order_ && order_->status == S::Reserved && order_->pileCode == code.trimmed()) {
        page_.showOrder(*order_);requestQuote();refresh();return;
    }
    clearQuoteRequest();candidate_=code.trimmed();order_.reset();page_.prepare(candidate_);requestQuote();refresh();
}
// 仅在空闲时发起当前订单查询，避免请求叠加
void ChargingController::refresh(){if(!active_||action_!=Action::None)return;action_=Action::Refresh;requestId_=api_.getCurrentOrder();}
// begin 先校验桩号与参考价，再进入开始前确认
void ChargingController::begin(const QString &code){
    if(!active_||(action_!=Action::None&&action_!=Action::Refresh))return;
    const bool refreshing=action_==Action::Refresh;
    const auto requested = code.trimmed();
    if (requested.isEmpty()) { page_.showMessage(QStringLiteral("请先选择充电桩"), true); return; }
    if (requested != candidate_) { prepare(requested); return; }
    if (!quote_) { requestQuote(); return; }
    page_.setBusy(true);page_.showMessage(QStringLiteral("正在确认充电条件…"));action_=Action::StartCheck;if(!refreshing)requestId_=api_.getCurrentOrder();
}
// 真正发起开始充电，可带上预约订单号
void ChargingController::start(std::optional<qint64> reservation){action_=Action::Start;requestId_=api_.startCharging(candidate_,reservation);}
// 结束充电前弹确认框，期间暂停定时刷新
void ChargingController::stop(){
    if(action_==Action::Refresh)finish();
    if(action_!=Action::None||!order_||order_->status!=S::Charging)return;
    // Pause refresh while the confirmation dialog runs its nested event loop.
    const qint64 id=order_->orderId;timer_->stop();const bool confirmed=confirmChargingStop(&page_);if(active_)timer_->start();
    if(!confirmed||!active_)return;
    page_.setBusy(true);page_.showMessage(QStringLiteral("正在停止并结算…"));action_=Action::Stop;requestId_=api_.stopCharging(id);
}
// apply 比较新旧订单状态，发出开始、结束、预约取消等信号
void ChargingController::apply(const protocol::OrderDto &order){
    const bool started=order.status==S::Charging&&(!order_||order_->status!=S::Charging||order_->orderId!=order.orderId);
    const bool ended=order_&&order_->status==S::Charging&&order.status!=S::Charging;
    const bool released = order_ && order_->status == S::Reserved
        && order_->orderId == order.orderId && order.status == S::Cancelled;
    const bool newReservation = order.status == S::Reserved
        && (!order_ || order_->orderId != order.orderId || candidate_ != order.pileCode);
    if (order.status != S::Reserved || newReservation) clearQuoteRequest();
    order_=order;candidate_=order.pileCode;page_.showOrder(order);
    if (newReservation) requestQuote();
    emit orderChanged(order);
    if (released) {
        page_.showMessage(QStringLiteral("预约已取消，未产生费用，请重新选桩"));
        emit reservationReleased();
    }
    if(started)emit sessionStarted();
    if(ended){page_.showMessage(order.status==S::PendingPayment?QStringLiteral("充电已结束，余额不足，请前往充值结算"):QStringLiteral("充电已结束并完成结算"));emit sessionFinished();}
}
// 一次请求收尾：清请求号并解除页面忙状态
void ChargingController::finish(){requestId_.clear();action_=Action::None;page_.setBusy(false);}
void ChargingController::reset(){active_=false;timer_->stop();finish();clearQuoteRequest();candidate_.clear();order_.reset();page_.reset();}

void ChargingController::clearQuoteRequest()
{
    quoteRequestId_.clear();
    quoteStations_.clear();
    quote_.reset();
}

// 取参考价：预约单可直接查站点，扫码需先列站点
void ChargingController::requestQuote()
{
    if (candidate_.isEmpty() || (order_ && order_->status != S::Reserved)) return;
    clearQuoteRequest();
    page_.clearQuote();
    // Reserved orders identify the station. A scanned code needs the existing
    // list/detail APIs to resolve it; the Demo has only a few stations.
    quoteRequestId_ = order_ ? api_.getStation(order_->stationId) : api_.listStations({});
}

// 逐个试下一个站点，全部落空则提示未找到参考价
void ChargingController::requestNextStation()
{
    if (quoteStations_.isEmpty()) {
        clearQuoteRequest();
        page_.showQuoteError(QStringLiteral("未找到该桩的参考价，请重新选桩或重试加载"));
        return;
    }
    quoteRequestId_ = api_.getStation(quoteStations_.takeFirst());
}

// 校验参考价响应；会话失效时重新登录，其他错误显示在价格区
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
