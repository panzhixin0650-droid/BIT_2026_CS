#include "ui/charging_page.h"
#include "ui/client_theme.h"
#include "common/charging_session_state.h"
#include "ui/pricing_hint.h"
#include "ui/pricing_info_button.h"
#include "ui/reservation_hint.h"
#include "charging/protocol/protocol_constants.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace charging::client {

class ChargingRing final : public QWidget {
public:
    explicit ChargingRing(QWidget *parent) : QWidget(parent)
    {
        setObjectName("chargingProgressRing");
        setMinimumSize(220, 220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    int percent = 0;
    QString caption = QStringLiteral("等待开始");

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal side = qMin<qreal>(280, qMin(width() - 36, height() - 28));
        const QRectF arc((width() - side) / 2, (height() - side) / 2, side, side);

        painter.setPen(QPen(QColor("#eef2e8"), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(arc.adjusted(-12, -12, 12, 12));
        painter.setPen(QPen(QColor("#e7eddf"), 13, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(arc);
        painter.setPen(QPen(QColor("#567b52"), 13, Qt::SolidLine, Qt::RoundCap));
        if (percent > 0) painter.drawArc(arc, 90 * 16, -qRound(percent * 3.6 * 16));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#f5f8f0"));
        painter.drawEllipse(arc.adjusted(15, 15, -15, -15));

        QFont textFont = font();
        textFont.setPixelSize(11);
        painter.setFont(textFont);
        painter.setPen(QColor("#71826c"));
        painter.drawText(QRectF(arc.left(), arc.center().y() - 58, side, 24),
                         Qt::AlignCenter, QStringLiteral("本次充电进度"));
        textFont.setPixelSize(qRound(side * .22));
        textFont.setBold(true);
        painter.setFont(textFont);
        painter.setPen(QColor("#245c45"));
        painter.drawText(arc.adjusted(0, -4, 0, -4), Qt::AlignCenter,
                         QString::number(percent) + "%");
        textFont.setPixelSize(11);
        textFont.setBold(false);
        painter.setFont(textFont);
        painter.setPen(QColor("#65796c"));
        painter.drawText(QRectF(arc.left(), arc.center().y() + 34, side, 26),
                         Qt::AlignCenter, caption);
    }
};

ChargingPage::ChargingPage(QWidget *parent) : QWidget(parent)
{
    setObjectName("chargingPage");
    // Reuse the application card and primary-button roles. Local rules only
    // refine the session layout and the hierarchy of its actions.
    setStyleSheet(QStringLiteral(R"QSS(
        #chargingState { color:#36583c; background:#edf4e5; border-radius:10px;
                         padding:5px 12px; font-size:12px; font-weight:600; }
        #chargingStation { color:#65796c; font-size:12px; }
        #chargingPage QLabel[metricLabel="true"] { color:#71806e; font-size:11px; }
        #chargingPage QLabel[metricValue="true"] { color:#203d33; font-size:21px; font-weight:600; }
        #chargingPage QPushButton { min-height:44px; border-radius:13px; font-size:13px; }
        #chargingPage QPushButton[role="primary"] { background:#245c45; color:white; border:1px solid #245c45; }
        #chargingPage QPushButton[role="primary"]:hover { background:#1c4d39; border-color:#1c4d39; }
        #chargingPage QPushButton[role="primary"]:pressed { background:#163f31; }
        #chargingPage QPushButton[role="primary"]:disabled { background:#95ae9e; border-color:#95ae9e; color:#f2f5ef; }
        #chargingEndButton { color:#36583c; background:#edf4e5; border-color:#c8d8bd; }
        #chargingEndButton:hover { background:#e1ecd6; border-color:#92ad7e; }
        #chargingEndButton:pressed { background:#d4e3c6; }
        #chargingEndButton:disabled { color:#96a18e; background:#edf0e8; border-color:#e1e6da; }
        #chargingRepairButton { min-height:30px; font-size:11px; font-weight:400; color:#71806e; }
    )QSS"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    body->setObjectName("chargingContent");
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 24, 20, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("充电"), body);
    auto titleFont = title->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *session = new QFrame(body);
    session->setProperty("role", "card");
    auto *sessionLayout = new QVBoxLayout(session);
    sessionLayout->setContentsMargins(16, 16, 16, 10);
    sessionLayout->setSpacing(8);
    state_ = new QLabel(session);
    state_->setObjectName("chargingState");
    state_->setWordWrap(true);
    sessionLayout->addWidget(state_, 0, Qt::AlignHCenter);
    station_ = new QLabel(session);
    station_->setObjectName("chargingStation");
    station_->setWordWrap(true);
    station_->setTextFormat(Qt::PlainText);
    station_->setAlignment(Qt::AlignCenter);
    sessionLayout->addWidget(station_);
    reservationHint_ = new QLabel(session);
    reservationHint_->setObjectName("chargingReservationHint");
    reservationHint_->setWordWrap(true);
    reservationHint_->setAlignment(Qt::AlignCenter);
    sessionLayout->addWidget(reservationHint_);
    ring_ = new ChargingRing(session);
    sessionLayout->addWidget(ring_, 1);
    layout->addWidget(session, 1);

    auto *grid = new QGridLayout;
    grid->setSpacing(10);
    auto metric = [&](const QString &name, const char *object, int row, int column) {
        auto *card = new QFrame(body);
        card->setProperty("role", "card");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);
        cardLayout->setSpacing(5);
        auto *label = new QLabel(name, card);
        label->setProperty("metricLabel", true);
        cardLayout->addWidget(label);
        auto *value = new QLabel(card);
        value->setObjectName(object);
        value->setProperty("metricValue", true);
        cardLayout->addWidget(value);
        grid->addWidget(card, row, column);
        return value;
    };
    power_ = metric(QStringLiteral("当前功率"), "chargingPower", 0, 0);
    energy_ = metric(QStringLiteral("已充电量"), "chargingEnergy", 0, 1);
    duration_ = metric(QStringLiteral("充电时长"), "chargingDuration", 1, 0);
    amount_ = metric(QStringLiteral("本次费用"), "chargingAmount", 1, 1);
    layout->addLayout(grid);

    price_ = new QLabel(body);
    price_->setObjectName("chargingPrice");
    price_->setWordWrap(true);
    price_->setTextFormat(Qt::PlainText);
    pricingInfo_ = new PricingInfoButton(body);
    pricingInfo_->setObjectName("chargingPricingInfoButton");
    auto *priceRow = new QHBoxLayout;
    priceRow->setSpacing(6);
    priceRow->addWidget(price_, 0, Qt::AlignVCenter);
    priceRow->addWidget(pricingInfo_, 0, Qt::AlignVCenter);
    priceRow->addStretch();
    layout->addLayout(priceRow);

    auto *note = new QLabel(QStringLiteral("模拟充电 · 每次约 3 分钟，满进度自动结束"), body);
    note->setWordWrap(true);
    note->setAlignment(Qt::AlignCenter);
    note->setStyleSheet("color:#71806e;font-size:11px;");
    layout->addWidget(note);
    message_ = new QLabel(body);
    message_->setObjectName("chargingMessage");
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    layout->addWidget(message_);

    auto button = [&](const QString &text, const char *object) {
        auto *result = new QPushButton(text, body);
        result->setObjectName(object);
        result->setCursor(Qt::PointingHandCursor);
        return result;
    };
    start_ = button(QStringLiteral("开始充电"), "chargingStartButton");
    start_->setProperty("role", "primary");
    stop_ = button(QStringLiteral("结束充电"), "chargingEndButton");
    recharge_ = button(QStringLiteral("充值并结算"), "chargingRechargeButton");
    recharge_->setProperty("role", "primary");
    orders_ = button(QStringLiteral("查看订单  ›"), "chargingOrdersButton");
    for (auto *action : {start_, stop_, recharge_, orders_}) layout->addWidget(action);

    auto *secondary = new QHBoxLayout;
    secondary->setSpacing(10);
    home_ = button(QStringLiteral("首页找桩"), "chargingFindButton");
    home_->setIcon(clientNavigationIcon(NavigationIcon::Route));
    scan_ = button(QStringLiteral("扫码选桩"), "chargingScanButton");
    scan_->setIcon(clientNavigationIcon(NavigationIcon::Scan));
    for (auto *action : {home_, scan_}) {
        action->setIconSize(QSize(18, 18));
        secondary->addWidget(action, 1);
    }
    layout->addLayout(secondary);
    repair_ = button(QStringLiteral("充电桩报修  ›"), "chargingRepairButton");
    repair_->setFlat(true);
    layout->addWidget(repair_, 0, Qt::AlignHCenter);

    connect(start_, &QPushButton::clicked, this, [this] {
        if (!quote_ && !quoteError_.isEmpty()) emit quoteRetryRequested();
        else emit startRequested(pileCode_);
    });
    connect(stop_, &QPushButton::clicked, this, &ChargingPage::stopRequested);
    connect(home_, &QPushButton::clicked, this, &ChargingPage::homeRequested);
    connect(scan_, &QPushButton::clicked, this, &ChargingPage::scanRequested);
    connect(recharge_, &QPushButton::clicked, this, &ChargingPage::rechargeRequested);
    connect(orders_, &QPushButton::clicked, this, &ChargingPage::ordersRequested);
    connect(repair_, &QPushButton::clicked, this, [this] { emit repairRequested(pileCode_); });
    scroll->setWidget(body);
    root->addWidget(scroll);
    render();
}
void ChargingPage::prepare(const QString &code){pileCode_=code.trimmed();order_.reset();message_->clear();clearQuote();}
void ChargingPage::showOrder(const protocol::OrderDto &order){order_=order;pileCode_=order.pileCode;render();}
void ChargingPage::clearQuote()
{
    quote_.reset();
    quoteError_.clear();
    quoteLoading_ = !pileCode_.isEmpty();
    render();
}
void ChargingPage::showQuote(const protocol::StationDto &station)
{
    quote_ = station;
    quoteError_.clear();
    quoteLoading_ = false;
    render();
}
void ChargingPage::showQuoteError(const QString &message)
{
    quote_.reset();
    quoteError_ = message;
    quoteLoading_ = false;
    render();
}
void ChargingPage::setBusy(bool busy){busy_=busy;render();}
void ChargingPage::showMessage(const QString &message,bool error){message_->setText(message);message_->setVisible(!message.isEmpty());message_->setStyleSheet(error?"color:#b54b38;":"color:#386a3c;");}
void ChargingPage::reset(){pileCode_.clear();order_.reset();busy_=false;message_->clear();clearQuote();}
void ChargingPage::render(){
    message_->setVisible(!message_->text().isEmpty());
    using S=protocol::OrderStatus;
    const bool charging=order_&&order_->status==S::Charging;
    const bool reserved=order_&&order_->status==S::Reserved;
    const bool debt=order_&&order_->status==S::PendingPayment;
    const bool finished=order_&&(order_->status==S::Completed||debt);
    const bool ready=!pileCode_.isEmpty()&&(!order_||reserved);
    const bool cancelled = order_ && order_->status == S::Cancelled;
    reservationHint_->setText(order_ ? reservationHint(*order_) : QString());
    reservationHint_->setVisible(reserved);
    state_->setText(charging?QStringLiteral("● 正在充电"):reserved?QStringLiteral("已预约 · 等待开始"):debt?QStringLiteral("充电已结束 · 待结算"):finished?QStringLiteral("充电已结束"):ready?QStringLiteral("已选定充电桩"):QStringLiteral("准备好，为下一程充电"));
    if (cancelled) state_->setText(QStringLiteral("预约已取消"));
    station_->setText(pileCode_.isEmpty()?QStringLiteral("在首页选桩，或扫一扫桩身二维码"):(order_?order_->stationName+" · ":quote_?quote_->name+" · ":QString())+pileCode_);
    qint64 secs=order_?order_->durationSeconds:0;
    ring_->percent=session::demoProgressPercent(secs);
    ring_->caption=charging?QStringLiteral("预计剩余 %1 秒").arg(qMax<qint64>(0,protocol::DemoChargingDurationSeconds-secs)):finished?QStringLiteral("本次充电结束"):QStringLiteral("连接充电枪后开始");ring_->update();
    if (cancelled) ring_->caption = QStringLiteral("请重新选桩");
    power_->setText(charging?QStringLiteral("7.2 kW"):QStringLiteral("—"));energy_->setText(QStringLiteral("%1 kWh").arg((order_?order_->energyWh:0)/1000.0,0,'f',3));
    duration_->setText(QStringLiteral("%1:%2").arg(secs/60,2,10,QChar('0')).arg(secs%60,2,10,QChar('0')));
    amount_->setText(QStringLiteral("¥ %1").arg((order_?order_->amountCents:0)/100.0,0,'f',2));
    const bool locked = order_ && order_->unitPriceCentsPerKwh.has_value();
    price_->setVisible(ready || locked);
    if (locked) {
        price_->setText(QStringLiteral("本单锁定单价：%1").arg(chargingPriceText(*order_->unitPriceCentsPerKwh)));
        pricingInfo_->setRules(QStringLiteral("本单按开始充电时的单价结算。\n跨时段结束或稍后补付款均不变价。"));
    } else if (ready && quote_) {
        price_->setText(QStringLiteral("当前参考单价：%1").arg(chargingPriceText(quote_->priceCentsPerKwh)));
        pricingInfo_->setRules(pricingHint(*quote_));
    } else {
        price_->setText(quoteLoading_ ? QStringLiteral("正在获取充电参考价…") : quoteError_);
        pricingInfo_->setRules({});
    }
    const bool retry = ready && !quote_ && !quoteLoading_ && !quoteError_.isEmpty();
    start_->setText(retry ? QStringLiteral("重试加载") : QStringLiteral("开始充电"));
    start_->setVisible(ready);start_->setEnabled(!busy_ && (quote_.has_value() || retry));stop_->setVisible(charging);stop_->setEnabled(!busy_);
    recharge_->setVisible(debt);orders_->setVisible(finished||reserved||cancelled);home_->setVisible(!charging&&!debt);scan_->setVisible(!charging&&!debt);repair_->setVisible(!pileCode_.isEmpty()&&!charging);
}
}
