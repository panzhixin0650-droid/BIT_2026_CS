// 文件用途：通过TCP按协议帧与服务端通信的客户端API实现
#include "api/tcp_charging_api.h"

#include "charging/protocol/dto.h"
#include "charging/protocol/envelope.h"
#include "charging/protocol/protocol_constants.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QTimer>
#include <QVector>
#include <QDebug>

#include <cmath>

namespace charging::client {
namespace {

// 手机号须11位数字，另限制JSON整数的安全范围
const QRegularExpression kPhonePattern(QStringLiteral("^\\d{11}$"));
constexpr double kMaxSafeJsonInteger = 9007199254740991.0;
constexpr qint64 kMaxSafeJsonIntegerValue = 9007199254740991LL;

// 统一写回错误说明并返回失败
bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

// 以下读取函数逐个校验响应字段类型
bool readObject(const QJsonObject &json,
                const char *field,
                QJsonObject *value,
                QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isObject()) {
        return fail(error, key + QStringLiteral(" must be an object"));
    }
    *value = item.toObject();
    return true;
}

// 读字符串，可要求非空
bool readString(const QJsonObject &json,
                const char *field,
                QString *value,
                bool requireNonEmpty,
                QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isString() || (requireNonEmpty && item.toString().isEmpty())) {
        return fail(error, key + QStringLiteral(" must be a string"));
    }
    *value = item.toString();
    return true;
}

bool readBool(const QJsonObject &json,
              const char *field,
              bool *value,
              QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isBool()) {
        return fail(error, key + QStringLiteral(" must be a boolean"));
    }
    *value = item.toBool();
    return true;
}

// 整数字段须为有限整数且不超出安全范围
bool readInteger(const QJsonObject &json,
                 const char *field,
                 qint64 *value,
                 QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isDouble()) {
        return fail(error, key + QStringLiteral(" must be an integer"));
    }
    const double number = item.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || std::abs(number) > kMaxSafeJsonInteger) {
        return fail(error, key + QStringLiteral(" must be a safe JSON integer"));
    }
    *value = static_cast<qint64>(number);
    return true;
}

// 把JSON对象或数组解析成DTO，出错时附上字段名
template<typename Dto>
bool readDto(const QJsonObject &json,
             const char *field,
             Dto *value,
             QString *error)
{
    QJsonObject object;
    if (!readObject(json, field, &object, error)) {
        return false;
    }
    QString dtoError;
    if (!protocol::fromJson(object, value, &dtoError)) {
        return fail(error,
                    QString::fromLatin1(field) + QStringLiteral(".") + dtoError);
    }
    return true;
}

template<typename Dto>
bool readDtoList(const QJsonObject &json,
                 const char *field,
                 QList<Dto> *values,
                 QString *error)
{
    const QString key = QString::fromLatin1(field);
    const QJsonValue item = json.value(key);
    if (!item.isArray()) {
        return fail(error, key + QStringLiteral(" must be an array"));
    }

    QList<Dto> parsed;
    const QJsonArray array = item.toArray();
    parsed.reserve(array.size());
    for (qsizetype index = 0; index < array.size(); ++index) {
        if (!array.at(index).isObject()) {
            return fail(error,
                        QStringLiteral("%1[%2] must be an object").arg(key).arg(index));
        }
        Dto dto;
        QString dtoError;
        if (!protocol::fromJson(array.at(index).toObject(), &dto, &dtoError)) {
            return fail(error,
                        QStringLiteral("%1[%2].%3").arg(key).arg(index).arg(dtoError));
        }
        parsed.append(dto);
    }
    *values = parsed;
    return true;
}

// 从响应信封提取请求ID、类型与错误码等元信息
ApiResponse apiResponse(const protocol::ResponseEnvelope &response)
{
    ApiResponse result;
    result.requestId = response.requestId;
    result.type = response.type;
    result.code = response.code;
    result.message = response.message;
    return result;
}

// 构造本地失败响应，不经过网络
ApiResponse apiFailure(const QString &requestId,
                       const QString &type,
                       int code,
                       const QString &message)
{
    ApiResponse result;
    result.requestId = requestId;
    result.type = type;
    result.code = code;
    result.message = message;
    return result;
}

QJsonValue jsonInteger(qint64 value)
{
    return QJsonValue(static_cast<double>(value));
}

bool isPositiveSafeJsonInteger(qint64 value)
{
    return value > 0 && value <= kMaxSafeJsonIntegerValue;
}

}  // namespace

// 构造函数接好socket各信号，随后尝试建立连接
TcpChargingApi::TcpChargingApi(QString host,
                               quint16 port,
                               int requestTimeoutMs,
                               QObject *parent)
    : IChargingApi(parent)
    , host_(host.trimmed())
    , port_(port)
    , requestTimeoutMs_(qMax(1, requestTimeoutMs))
{
    connect(&socket_, &QTcpSocket::connected,
            this, &TcpChargingApi::sendQueuedRequests);
    connect(&socket_, &QTcpSocket::readyRead,
            this, &TcpChargingApi::handleReadyRead);
    connect(&socket_, &QTcpSocket::disconnected,
            this, &TcpChargingApi::handleDisconnected);
    connect(&socket_, &QTcpSocket::errorOccurred,
            this, &TcpChargingApi::handleSocketError);

    QTimer::singleShot(0, this, [this]() { ensureConnected(); });
}

// 析构时断开信号并清理未完成请求的计时器
TcpChargingApi::~TcpChargingApi()
{
    disconnect(&socket_, nullptr, this, nullptr);
    socket_.abort();
    for (auto pending = pending_.begin(); pending != pending_.end(); ++pending) {
        delete pending->timer;
        pending->timer = nullptr;
    }
    pending_.clear();
    sendQueue_.clear();
}

// 登录前先本地校验手机号格式
QString TcpChargingApi::loginUser(const QString &phone)
{
    if (!kPhonePattern.match(phone).hasMatch()) {
        return rejectInvalid(protocol::MessageType::AuthUserLogin,
                             QStringLiteral("手机号必须为11位数字"));
    }
    return submit(protocol::MessageType::AuthUserLogin,
                  {{QStringLiteral("phone"), phone}}, false);
}

QString TcpChargingApi::logout()
{
    return submit(protocol::MessageType::AuthLogout, {}, true);
}

QString TcpChargingApi::getProfile()
{
    return submit(protocol::MessageType::UserProfileGet, {}, true);
}

// 昵称长度先本地检查，避免发无效请求
QString TcpChargingApi::updateNickname(const QString &nickname)
{
    if (nickname.isEmpty() || nickname.size() > 32) {
        return rejectInvalid(protocol::MessageType::UserProfileUpdate,
                             QStringLiteral("昵称长度必须为1到32个字符"));
    }
    return submit(protocol::MessageType::UserProfileUpdate,
                  {{QStringLiteral("nickname"), nickname}}, true);
}

QString TcpChargingApi::recharge(qint64 amountCents)
{
    if (amountCents < 1 || amountCents > 1000000) {
        return rejectInvalid(protocol::MessageType::WalletRecharge,
                             QStringLiteral("充值金额必须在1到1000000分之间"));
    }
    return submit(protocol::MessageType::WalletRecharge,
                  {{QStringLiteral("amountCents"), jsonInteger(amountCents)}}, true);
}

// 站点列表：经纬度须成对出现且在合法范围内
QString TcpChargingApi::listStations(const StationQuery &query)
{
    const bool hasLongitude = query.longitude.has_value();
    const bool hasLatitude = query.latitude.has_value();
    if (hasLongitude != hasLatitude
        || (hasLongitude
            && (!std::isfinite(*query.longitude)
                || !std::isfinite(*query.latitude)
                || *query.longitude < -180.0 || *query.longitude > 180.0
                || *query.latitude < -90.0 || *query.latitude > 90.0))) {
        return rejectInvalid(protocol::MessageType::StationList,
                             QStringLiteral("经纬度参数无效"));
    }

    QJsonObject data;
    if (hasLongitude) {
        data.insert(QStringLiteral("longitude"), *query.longitude);
        data.insert(QStringLiteral("latitude"), *query.latitude);
    }
    const QString region = query.region.trimmed();
    const QString keyword = query.keyword.trimmed();
    if (!region.isEmpty()) {
        data.insert(QStringLiteral("region"), region);
    }
    if (!keyword.isEmpty()) {
        data.insert(QStringLiteral("keyword"), keyword);
    }
    return submit(protocol::MessageType::StationList, data, true);
}

QString TcpChargingApi::getStation(qint64 stationId)
{
    if (!isPositiveSafeJsonInteger(stationId)) {
        return rejectInvalid(protocol::MessageType::StationDetail,
                             QStringLiteral("充电站标识无效"));
    }
    return submit(protocol::MessageType::StationDetail,
                  {{QStringLiteral("stationId"), jsonInteger(stationId)}}, true);
}

QString TcpChargingApi::getCurrentOrder()
{
    return submit(protocol::MessageType::OrderCurrent, {}, true);
}

QString TcpChargingApi::listOrders()
{
    return submit(protocol::MessageType::OrderList, {}, true);
}

// 预约请求：桩编号去空格并限制长度
QString TcpChargingApi::reserve(const QString &pileCode)
{
    const QString normalized = pileCode.trimmed();
    if (normalized.isEmpty() || normalized.size() > 64) {
        return rejectInvalid(protocol::MessageType::OrderReserve,
                             QStringLiteral("充电桩编号无效"));
    }
    return submit(protocol::MessageType::OrderReserve,
                  {{QStringLiteral("pileCode"), normalized}}, true);
}

QString TcpChargingApi::cancel(qint64 orderId)
{
    if (!isPositiveSafeJsonInteger(orderId)) {
        return rejectInvalid(protocol::MessageType::OrderCancel,
                             QStringLiteral("订单标识无效"));
    }
    return submit(protocol::MessageType::OrderCancel,
                  {{QStringLiteral("orderId"), jsonInteger(orderId)}}, true);
}

// 启动充电可带预约单号，参数非法时本地直接拒绝
QString TcpChargingApi::startCharging(
    const QString &pileCode,
    std::optional<qint64> reservationOrderId)
{
    const QString normalized = pileCode.trimmed();
    if (normalized.isEmpty() || normalized.size() > 64
        || (reservationOrderId.has_value()
            && !isPositiveSafeJsonInteger(*reservationOrderId))) {
        return rejectInvalid(protocol::MessageType::OrderStart,
                             QStringLiteral("充电启动参数无效"));
    }

    QJsonObject data{{QStringLiteral("pileCode"), normalized}};
    if (reservationOrderId.has_value()) {
        data.insert(QStringLiteral("reservationOrderId"),
                    jsonInteger(*reservationOrderId));
    }
    return submit(protocol::MessageType::OrderStart, data, true);
}

QString TcpChargingApi::getChargingProgress(qint64 orderId)
{
    if (!isPositiveSafeJsonInteger(orderId)) {
        return rejectInvalid(protocol::MessageType::OrderProgress,
                             QStringLiteral("订单标识无效"));
    }
    return submit(protocol::MessageType::OrderProgress,
                  {{QStringLiteral("orderId"), jsonInteger(orderId)}}, true);
}

QString TcpChargingApi::stopCharging(qint64 orderId)
{
    if (!isPositiveSafeJsonInteger(orderId)) {
        return rejectInvalid(protocol::MessageType::OrderStop,
                             QStringLiteral("订单标识无效"));
    }
    return submit(protocol::MessageType::OrderStop,
                  {{QStringLiteral("orderId"), jsonInteger(orderId)}}, true);
}

QString TcpChargingApi::payOrder(qint64 orderId)
{
    if (!isPositiveSafeJsonInteger(orderId)) {
        return rejectInvalid(protocol::MessageType::OrderPay,
                             QStringLiteral("订单标识无效"));
    }
    return submit(protocol::MessageType::OrderPay,
                  {{QStringLiteral("orderId"), jsonInteger(orderId)}}, true);
}

QString TcpChargingApi::nextRequestId()
{
    return QStringLiteral("tcp-%1").arg(++requestSequence_);
}

// submit 负责组帧、登记待响应请求并启动超时计时
QString TcpChargingApi::submit(const char *type,
                               const QJsonObject &data,
                               bool requiresToken)
{
    const QString requestId = nextRequestId();
    protocol::RequestEnvelope request;
    request.version = protocol::kProtocolVersion;
    request.type = QString::fromLatin1(type);
    request.requestId = requestId;
    request.data = data;
    if (requiresToken && !token_.isEmpty()) {
        request.token = token_;
    }

    const QByteArray frame = protocol::encodeFrame(request.toJson());
    if (frame.isEmpty()) {
        QTimer::singleShot(0, this, [this, requestId, request]() {
            emitFailure(requestId,
                        request.type,
                        protocol::ErrorCode::InvalidRequest,
                        QStringLiteral("请求数据无效"));
        });
        return requestId;
    }

    // 超时仍未响应就按传输失败处理
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, requestId]() {
        if (pending_.contains(requestId)) {
            failTransport(QStringLiteral("请求超时，请稍后重试"));
        }
    });

    pending_.insert(requestId, PendingRequest{request.type, frame, timer});
    sendQueue_.append(requestId);
    timer->start(requestTimeoutMs_);
    ensureConnected();
    if (socket_.state() == QAbstractSocket::ConnectedState) {
        sendQueuedRequests();
    }
    return requestId;
}

// 参数非法不发网络请求，下一轮事件循环回失败
QString TcpChargingApi::rejectInvalid(const char *type, const QString &message)
{
    const QString requestId = nextRequestId();
    const QString messageType = QString::fromLatin1(type);
    QTimer::singleShot(0, this, [this, requestId, messageType, message]() {
        emitFailure(requestId,
                    messageType,
                    protocol::ErrorCode::InvalidRequest,
                    message);
    });
    return requestId;
}

// 仅在未连接时发起连接，地址无效则报错
void TcpChargingApi::ensureConnected()
{
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    if (host_.isEmpty() || port_ == 0) {
        QTimer::singleShot(0, this, [this]() {
            if (!pending_.isEmpty()) {
                failTransport(QStringLiteral("服务地址或端口无效"));
            }
        });
        return;
    }

    decoder_.reset();
    socket_.connectToHost(host_, port_);
}

// 连接建立后把排队的请求依次写出
void TcpChargingApi::sendQueuedRequests()
{
    while (socket_.state() == QAbstractSocket::ConnectedState
           && !sendQueue_.isEmpty()) {
        const QString requestId = sendQueue_.takeFirst();
        const auto request = pending_.constFind(requestId);
        if (request == pending_.cend()) {
            continue;
        }
        if (socket_.write(request->frame) < 0) {
            failTransport(QStringLiteral("请求发送失败，请稍后重试"));
            return;
        }
    }
    socket_.flush();
}

// 收到数据后解帧，逐条与待响应请求配对
void TcpChargingApi::handleReadyRead()
{
    const protocol::DecodeResult decoded = decoder_.append(socket_.readAll());
    if (!decoded.ok()) {
        failTransport(QStringLiteral("服务响应格式无效"));
        return;
    }

    for (const QJsonObject &json : decoded.messages) {
        protocol::ResponseEnvelope response;
        QString parseError;
        if (!protocol::ResponseEnvelope::fromJson(json, &response, &parseError)) {
            failTransport(QStringLiteral("服务响应格式无效"));
            return;
        }

        // 无法匹配或重复的响应只告警并忽略
        auto pending = pending_.find(response.requestId);
        if (pending == pending_.end() || pending->responseReceived) {
            qWarning().noquote()
                << QStringLiteral("Ignoring unmatched response %1/%2")
                       .arg(response.type, response.requestId);
            continue;
        }
        if (pending->type != response.type) {
            failTransport(QStringLiteral("服务响应与请求不匹配"));
            return;
        }

        if (pending->timer != nullptr) {
            pending->timer->stop();
            pending->timer->deleteLater();
            pending->timer = nullptr;
        }
        pending->responseReceived = true;
        sendQueue_.removeAll(response.requestId);

        // Completion slots can open a modal dialog. readyRead is not emitted
        // recursively, so finish draining this batch (and stop its timers)
        // before notifying controllers from a separate event-loop turn.
        QTimer::singleShot(0, this, [this, response]() {
            // A transport failure may have completed this request while the
            // notification was queued. Do not emit twice or restore a stale token.
            if (pending_.remove(response.requestId) == 0) {
                return;
            }
            handleResponse(response);
        });
    }
}

// 按消息类型解析data，再发出对应的完成信号
void TcpChargingApi::handleResponse(const protocol::ResponseEnvelope &response)
{
    const ApiResponse metadata = apiResponse(response);
    // 会话失效时清掉本地token
    if (response.code == protocol::ErrorCode::InvalidSession) {
        token_.clear();
    }
    if (response.code != protocol::ErrorCode::Ok) {
        emitFailure(response.requestId,
                    response.type,
                    response.code,
                    response.message);
        return;
    }

    QString error;
    if (response.type == QString::fromLatin1(protocol::MessageType::AuthUserLogin)) {
        LoginPayload payload;
        if (!readString(response.data, "token", &payload.token, true, &error)
            || !readBool(response.data, "isNewUser", &payload.isNewUser, &error)
            || !readDto(response.data, "user", &payload.user, &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        token_ = payload.token;
        emit loginCompleted(LoginResult{metadata, payload});
        return;
    }

    // 登出无论结果先清token，避免残留会话
    if (response.type == QString::fromLatin1(protocol::MessageType::AuthLogout)) {
        token_.clear();
        bool success = false;
        if (!readBool(response.data, "success", &success, &error) || !success) {
            emitMalformedPayload(response,
                                 error.isEmpty()
                                     ? QStringLiteral("success must be true")
                                     : error);
            return;
        }
        emit logoutCompleted(LogoutResult{metadata, LogoutPayload{true}});
        return;
    }

    if (response.type == QString::fromLatin1(protocol::MessageType::UserProfileGet)
        || response.type
            == QString::fromLatin1(protocol::MessageType::UserProfileUpdate)) {
        UserPayload payload;
        if (!readDto(response.data, "user", &payload.user, &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        const UserResult result{metadata, payload};
        if (response.type
            == QString::fromLatin1(protocol::MessageType::UserProfileGet)) {
            emit profileCompleted(result);
        } else {
            emit profileUpdateCompleted(result);
        }
        return;
    }

    if (response.type == QString::fromLatin1(protocol::MessageType::WalletRecharge)) {
        RechargePayload payload;
        if (!readInteger(response.data,
                         "balanceCents",
                         &payload.balanceCents,
                         &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        emit rechargeCompleted(RechargeResult{metadata, payload});
        return;
    }

    if (response.type == QString::fromLatin1(protocol::MessageType::StationList)) {
        StationListPayload payload;
        if (!readDtoList(response.data, "items", &payload.items, &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        emit stationListCompleted(StationListResult{metadata, payload});
        return;
    }

    if (response.type == QString::fromLatin1(protocol::MessageType::StationDetail)) {
        StationDetailPayload payload;
        if (!readDto(response.data, "station", &payload.station, &error)
            || !readDtoList(response.data, "piles", &payload.piles, &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        emit stationDetailCompleted(StationDetailResult{metadata, payload});
        return;
    }

    // 当前订单允许为null，表示没有进行中订单
    if (response.type == QString::fromLatin1(protocol::MessageType::OrderCurrent)) {
        CurrentOrderPayload payload;
        const QJsonValue item = response.data.value(QStringLiteral("order"));
        if (item.isNull()) {
            payload.order.reset();
        } else if (item.isObject()) {
            protocol::OrderDto order;
            if (!protocol::fromJson(item.toObject(), &order, &error)) {
                emitMalformedPayload(response,
                                     QStringLiteral("order.") + error);
                return;
            }
            payload.order = order;
        } else {
            emitMalformedPayload(response,
                                 QStringLiteral("order must be an object or null"));
            return;
        }
        emit currentOrderCompleted(CurrentOrderResult{metadata, payload});
        return;
    }

    if (response.type == QString::fromLatin1(protocol::MessageType::OrderList)) {
        OrderListPayload payload;
        if (!readDtoList(response.data, "items", &payload.items, &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        emit orderListCompleted(OrderListResult{metadata, payload});
        return;
    }

    // 预约、取消、启动共用订单载荷，按类型分派信号
    if (response.type == QString::fromLatin1(protocol::MessageType::OrderReserve)
        || response.type == QString::fromLatin1(protocol::MessageType::OrderCancel)
        || response.type == QString::fromLatin1(protocol::MessageType::OrderStart)) {
        OrderPayload payload;
        if (!readDto(response.data, "order", &payload.order, &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        const OrderResult result{metadata, payload};
        if (response.type
            == QString::fromLatin1(protocol::MessageType::OrderReserve)) {
            emit reservationCompleted(result);
        } else if (response.type
                   == QString::fromLatin1(protocol::MessageType::OrderCancel)) {
            emit cancellationCompleted(result);
        } else {
            emit chargingStartCompleted(result);
        }
        return;
    }

    if (response.type == QString::fromLatin1(protocol::MessageType::OrderProgress)) {
        ChargingProgressPayload payload;
        if (!readDto(response.data, "order", &payload.order, &error)
            || !readString(response.data,
                           "measuredAt",
                           &payload.measuredAt,
                           true,
                           &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        emit chargingProgressCompleted(ChargingProgressResult{metadata, payload});
        return;
    }

    // 停止充电还需读取是否已支付与差额
    if (response.type == QString::fromLatin1(protocol::MessageType::OrderStop)) {
        ChargingStopPayload payload;
        if (!readDto(response.data, "order", &payload.order, &error)
            || !readBool(response.data, "paid", &payload.paid, &error)
            || !readInteger(response.data,
                            "balanceCents",
                            &payload.balanceCents,
                            &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        if (response.data.contains(QStringLiteral("shortfallCents"))) {
            qint64 shortfall = 0;
            if (!readInteger(response.data,
                             "shortfallCents",
                             &shortfall,
                             &error)) {
                emitMalformedPayload(response, error);
                return;
            }
            payload.shortfallCents = shortfall;
        }
        emit chargingStopCompleted(ChargingStopResult{metadata, payload});
        return;
    }

    if (response.type == QString::fromLatin1(protocol::MessageType::OrderPay)) {
        PaymentPayload payload;
        if (!readDto(response.data, "order", &payload.order, &error)
            || !readInteger(response.data,
                            "balanceCents",
                            &payload.balanceCents,
                            &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        emit paymentCompleted(PaymentResult{metadata, payload});
        return;
    }

    // 工单创建与详情都从ticket字段解析
    if (response.type == protocol::MessageType::SupportTicketCreate
        || response.type == protocol::MessageType::SupportTicketDetail) {
        TicketPayload payload;
        if (!readDto(response.data, "ticket", &payload.ticket, &error)) {
            emitMalformedPayload(response, error);
            return;
        }
        if (response.type == protocol::MessageType::SupportTicketCreate)
            emit supportTicketCreated(TicketResult{metadata, payload});
        else emit supportTicketDetailed(TicketResult{metadata, payload});
        return;
    }
    // 工单分页校验条数与hasMore是否自相一致
    if (response.type == protocol::MessageType::SupportTicketList) {
        TicketListPayload payload;
        if (!readDtoList(response.data, "items", &payload.items, &error)
            || !readBool(response.data, "hasMore", &payload.hasMore, &error)
            || payload.items.size() > 10 || (payload.hasMore && payload.items.size() != 10)) {
            emitMalformedPayload(response, QStringLiteral("invalid ticket page"));
            return;
        }
        emit supportTicketsListed(TicketListResult{metadata, payload});
        return;
    }
    emitMalformedPayload(response, QStringLiteral("unsupported response type"));
}

// socket出错：没有待处理请求时只清理状态
void TcpChargingApi::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (handlingTransportFailure_) {
        return;
    }
    if (pending_.isEmpty()) {
        token_.clear();
        decoder_.reset();
        return;
    }
    failTransport(QStringLiteral("暂时无法连接服务，请检查网络后重试"));
}

// 断线清空token，未完成的请求统一置为失败
void TcpChargingApi::handleDisconnected()
{
    if (handlingTransportFailure_) {
        return;
    }
    token_.clear();
    decoder_.reset();
    if (!pending_.isEmpty()) {
        failTransport(QStringLiteral("与服务的连接已断开，请重新登录"));
    }
}

// 传输失败时收敛所有待响应请求并复位连接
void TcpChargingApi::failTransport(const QString &message)
{
    if (handlingTransportFailure_) {
        return;
    }
    handlingTransportFailure_ = true;

    struct Failure {
        QString requestId;
        QString type;
    };
    QVector<Failure> failures;
    failures.reserve(pending_.size());
    for (auto pending = pending_.begin(); pending != pending_.end(); ++pending) {
        failures.append({pending.key(), pending->type});
        if (pending->timer != nullptr) {
            pending->timer->stop();
            pending->timer->deleteLater();
        }
    }

    pending_.clear();
    sendQueue_.clear();
    token_.clear();
    decoder_.reset();
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.abort();
    }
    handlingTransportFailure_ = false;

    for (const Failure &failure : failures) {
        emitFailure(failure.requestId,
                    failure.type,
                    protocol::ErrorCode::ServiceUnavailable,
                    message);
    }
}

// 按消息类型把失败结果转成对应的完成信号
void TcpChargingApi::emitFailure(const QString &requestId,
                                 const QString &type,
                                 int code,
                                 const QString &message)
{
    const ApiResponse response = apiFailure(requestId, type, code, message);
    if (type == QString::fromLatin1(protocol::MessageType::AuthUserLogin)) {
        emit loginCompleted(LoginResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::AuthLogout)) {
        emit logoutCompleted(LogoutResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::UserProfileGet)) {
        emit profileCompleted(UserResult{response, std::nullopt});
    } else if (type
               == QString::fromLatin1(protocol::MessageType::UserProfileUpdate)) {
        emit profileUpdateCompleted(UserResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::WalletRecharge)) {
        emit rechargeCompleted(RechargeResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::StationList)) {
        emit stationListCompleted(StationListResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::StationDetail)) {
        emit stationDetailCompleted(StationDetailResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderCurrent)) {
        emit currentOrderCompleted(CurrentOrderResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderList)) {
        emit orderListCompleted(OrderListResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderReserve)) {
        emit reservationCompleted(OrderResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderCancel)) {
        emit cancellationCompleted(OrderResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderStart)) {
        emit chargingStartCompleted(OrderResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderProgress)) {
        emit chargingProgressCompleted(
            ChargingProgressResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderStop)) {
        emit chargingStopCompleted(ChargingStopResult{response, std::nullopt});
    } else if (type == QString::fromLatin1(protocol::MessageType::OrderPay)) {
        emit paymentCompleted(PaymentResult{response, std::nullopt});
    } else if (type == protocol::MessageType::SupportTicketCreate) {
        emit supportTicketCreated(TicketResult{response, std::nullopt});
    // 工单列表与详情响应各自转成对应结果信号发出
    } else if (type == protocol::MessageType::SupportTicketList) {
        emit supportTicketsListed(TicketListResult{response, std::nullopt});
    } else if (type == protocol::MessageType::SupportTicketDetail) {
        emit supportTicketDetailed(TicketResult{response, std::nullopt});
    }
}

// 载荷解析失败时记日志，并统一报服务响应数据无效
void TcpChargingApi::emitMalformedPayload(
    const protocol::ResponseEnvelope &response,
    const QString &detail)
{
    qWarning().noquote()
        << QStringLiteral("Invalid TCP response payload for %1/%2: %3")
               .arg(response.type, response.requestId, detail);
    emitFailure(response.requestId,
                response.type,
                protocol::ErrorCode::ServiceUnavailable,
                QStringLiteral("服务响应数据无效，请稍后重试"));
}

// 建单前用一次序列化回读校验草稿字段是否合法
QString TcpChargingApi::createSupportTicket(const protocol::SupportTicketDraft &draft)
{
    protocol::SupportTicketDraft validated;
    if (!protocol::fromJson(protocol::toJson(draft), &validated))
        return rejectInvalid(protocol::MessageType::SupportTicketCreate, QStringLiteral("请检查工单标题和摘要"));
    return submit(protocol::MessageType::SupportTicketCreate, protocol::toJson(draft), true);
}

// 列表可带分页游标，游标编号非法则本地直接拒绝
QString TcpChargingApi::listSupportTickets(std::optional<qint64> beforeId)
{
    QJsonObject data;
    if (beforeId) {
        qint64 id = 0;
        if (!protocol::positiveTicketId(QJsonValue(*beforeId), &id))
            return rejectInvalid(protocol::MessageType::SupportTicketList, QStringLiteral("工单分页编号无效"));
        data.insert("beforeTicketId", *beforeId);
    }
    return submit(protocol::MessageType::SupportTicketList, data, true);
}

// 查详情前先校验工单编号为正整数
QString TcpChargingApi::getSupportTicket(qint64 ticketId)
{
    qint64 id = 0;
    if (!protocol::positiveTicketId(QJsonValue(ticketId), &id))
        return rejectInvalid(protocol::MessageType::SupportTicketDetail, QStringLiteral("工单编号无效"));
    return submit(protocol::MessageType::SupportTicketDetail, {{"ticketId", ticketId}}, true);
}

}  // namespace charging::client
