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

const QRegularExpression kPhonePattern(QStringLiteral("^\\d{11}$"));
constexpr double kMaxSafeJsonInteger = 9007199254740991.0;
constexpr qint64 kMaxSafeJsonIntegerValue = 9007199254740991LL;

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

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

ApiResponse apiResponse(const protocol::ResponseEnvelope &response)
{
    ApiResponse result;
    result.requestId = response.requestId;
    result.type = response.type;
    result.code = response.code;
    result.message = response.message;
    return result;
}

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

        auto pending = pending_.find(response.requestId);
        if (pending == pending_.end()) {
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
        }
        pending_.erase(pending);
        sendQueue_.removeAll(response.requestId);
        handleResponse(response);
    }
}

void TcpChargingApi::handleResponse(const protocol::ResponseEnvelope &response)
{
    const ApiResponse metadata = apiResponse(response);
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

    emitMalformedPayload(response, QStringLiteral("unsupported response type"));
}

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
    }
}

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

}  // namespace charging::client
