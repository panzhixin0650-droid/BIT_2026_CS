#include "api/mock_charging_api.h"

#include "charging/protocol/protocol_constants.h"

#include <QDateTime>
#include <QtMath>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace charging::client {

namespace {

const QRegularExpression kPhonePattern(QStringLiteral("^\\d{11}$"));

bool isCurrentOrderStatus(protocol::OrderStatus status)
{
    return status == protocol::OrderStatus::Reserved
        || status == protocol::OrderStatus::Charging
        || status == protocol::OrderStatus::PendingPayment;
}

double distanceKm(double longitudeA,
                  double latitudeA,
                  double longitudeB,
                  double latitudeB)
{
    constexpr double kEarthRadiusKm = 6371.0;
    const double latitudeRadiansA = qDegreesToRadians(latitudeA);
    const double latitudeRadiansB = qDegreesToRadians(latitudeB);
    const double latitudeDelta = qDegreesToRadians(latitudeB - latitudeA);
    const double longitudeDelta = qDegreesToRadians(longitudeB - longitudeA);
    const double haversine = qPow(qSin(latitudeDelta / 2.0), 2)
        + qCos(latitudeRadiansA) * qCos(latitudeRadiansB)
            * qPow(qSin(longitudeDelta / 2.0), 2);
    const double boundedHaversine = std::clamp(haversine, 0.0, 1.0);
    return kEarthRadiusKm * 2.0
        * qAtan2(qSqrt(boundedHaversine), qSqrt(1.0 - boundedHaversine));
}

}  // namespace

MockChargingApi::MockChargingApi(QObject *parent)
    : IChargingApi(parent)
{
    protocol::UserDto fixtureUser;
    fixtureUser.userId = 1;
    fixtureUser.phone = QStringLiteral("13800000001");
    fixtureUser.nickname = QStringLiteral("演示用户0001");
    fixtureUser.balanceCents = 20000;
    fixtureUser.status = protocol::UserStatus::Active;
    fixtureUser.createdAt = QStringLiteral("2026-06-04T11:53:41Z");
    usersByPhone_.insert(fixtureUser.phone, fixtureUser);

    const QList<protocol::PileDto> fixturePiles{
        {1, 1, QStringLiteral("PILE-A-01"), protocol::PileType::Fast, 10.0,
         protocol::PileStatus::Idle, 4, 14400},
        {2, 1, QStringLiteral("PILE-A-02"), protocol::PileType::Slow, 7.0,
         protocol::PileStatus::Charging, 2, 7200},
        {3, 2, QStringLiteral("PILE-B-01"), protocol::PileType::Fast, 60.0,
         protocol::PileStatus::Reserved, 0, 0},
        {4, 2, QStringLiteral("PILE-B-02"), protocol::PileType::Fast, 60.0,
         protocol::PileStatus::Idle, 0, 0},
        {5, 2, QStringLiteral("PILE-B-03"), protocol::PileType::Slow, 7.0,
         protocol::PileStatus::Fault, 0, 0},
        {6, 2, QStringLiteral("PILE-B-04"), protocol::PileType::Fast, 60.0,
         protocol::PileStatus::Offline, 0, 0},
        {7, 2, QStringLiteral("PILE-B-05"), protocol::PileType::Slow, 7.0,
         protocol::PileStatus::Idle, 0, 0},
    };
    for (const auto &pile : fixturePiles) {
        pilesByCode_.insert(pile.pileCode, pile);
    }

    const auto addCompletedOrder = [this, userId = fixtureUser.userId](qint64 orderId,
                                          const QString &pileCode,
                                          int daysAgo,
                                          qint64 durationSeconds,
                                          qint64 energyWh,
                                          qint64 amountCents,
                                          protocol::OrderMode mode) {
        const protocol::PileDto pile = pilesByCode_.value(pileCode);
        const protocol::StationDto selectedStation = station(pile.stationId);
        QDateTime created = QDateTime::currentDateTimeUtc().addDays(-daysAgo);
        if (daysAgo == 0) {
            created = created.addSecs(-2100);
        }
        const QDateTime started = created.addSecs(300);
        const QDateTime ended = started.addSecs(durationSeconds);

        protocol::OrderDto order;
        order.orderId = orderId;
        order.orderNo = QStringLiteral("DEMO-COMPLETED-%1")
                            .arg(orderId, 3, 10, QChar('0'));
        order.createdAt = created.toString(Qt::ISODate);
        order.userId = userId;
        order.stationId = selectedStation.stationId;
        order.stationName = selectedStation.name;
        order.pileId = pile.pileId;
        order.pileCode = pile.pileCode;
        order.mode = mode;
        order.status = protocol::OrderStatus::Completed;
        if (mode == protocol::OrderMode::Reservation) {
            order.reservedAt = created.toString(Qt::ISODate);
        }
        order.startedAt = started.toString(Qt::ISODate);
        order.endedAt = ended.toString(Qt::ISODate);
        order.paidAt = ended.toString(Qt::ISODate);
        order.durationSeconds = durationSeconds;
        order.energyWh = energyWh;
        order.unitPriceCentsPerKwh = selectedStation.priceCentsPerKwh;
        order.amountCents = amountCents;
        ordersById_.insert(order.orderId, order);
    };

    addCompletedOrder(101, QStringLiteral("PILE-A-01"), 0, 1800, 5000, 675,
                      protocol::OrderMode::Reservation);
    addCompletedOrder(104, QStringLiteral("PILE-A-01"), 4, 3600, 8000, 1080,
                      protocol::OrderMode::Direct);
    addCompletedOrder(107, QStringLiteral("PILE-A-01"), 15, 5400, 12000, 1620,
                      protocol::OrderMode::Reservation);
    addCompletedOrder(109, QStringLiteral("PILE-A-01"), 28, 3600, 9000, 1215,
                      protocol::OrderMode::Direct);
}

QString MockChargingApi::loginUser(const QString &phone)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, phone]() {
        LoginResult result;
        if (!kPhonePattern.match(phone).hasMatch()) {
            result.response = response(requestId,
                                       protocol::MessageType::AuthUserLogin,
                                       protocol::ErrorCode::InvalidRequest,
                                       QStringLiteral("手机号必须为11位数字"));
            emit loginCompleted(result);
            return;
        }

        const bool isNewUser = !usersByPhone_.contains(phone);
        if (isNewUser) {
            protocol::UserDto user;
            user.userId = nextUserId_++;
            user.phone = phone;
            user.nickname = QStringLiteral("用户%1").arg(phone.right(4));
            user.balanceCents = 0;
            user.status = protocol::UserStatus::Active;
            user.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            usersByPhone_.insert(phone, user);
        }

        authenticatedPhone_ = phone;
        token_ = QStringLiteral("mock-token-%1").arg(requestId);
        result.response = response(requestId,
                                   protocol::MessageType::AuthUserLogin,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = LoginPayload{token_, isNewUser, usersByPhone_.value(phone)};
        emit loginCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::logout()
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId]() {
        LogoutResult result;
        if (token_.isEmpty()) {
            result.response = response(requestId,
                                       protocol::MessageType::AuthLogout,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("登录状态已失效"));
            emit logoutCompleted(result);
            return;
        }

        authenticatedPhone_.clear();
        token_.clear();
        result.response = response(requestId,
                                   protocol::MessageType::AuthLogout,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = LogoutPayload{true};
        emit logoutCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::getProfile()
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId]() {
        UserResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            token_.clear();
            authenticatedPhone_.clear();
            result.response = response(requestId,
                                       protocol::MessageType::UserProfileGet,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit profileCompleted(result);
            return;
        }

        result.response = response(requestId,
                                   protocol::MessageType::UserProfileGet,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = UserPayload{*user};
        emit profileCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::updateNickname(const QString &nickname)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, nickname]() {
        UserResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            token_.clear();
            authenticatedPhone_.clear();
            result.response = response(requestId,
                                       protocol::MessageType::UserProfileUpdate,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit profileUpdateCompleted(result);
            return;
        }

        if (nickname.isEmpty() || nickname.size() > 32) {
            result.response = response(requestId,
                                       protocol::MessageType::UserProfileUpdate,
                                       protocol::ErrorCode::InvalidRequest,
                                       QStringLiteral("昵称长度必须为1到32个字符"));
            emit profileUpdateCompleted(result);
            return;
        }

        auto updatedUser = *user;
        updatedUser.nickname = nickname;
        usersByPhone_.insert(updatedUser.phone, updatedUser);
        result.response = response(requestId,
                                   protocol::MessageType::UserProfileUpdate,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = UserPayload{updatedUser};
        emit profileUpdateCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::recharge(qint64 amountCents)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, amountCents]() {
        RechargeResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            token_.clear();
            authenticatedPhone_.clear();
            result.response = response(requestId,
                                       protocol::MessageType::WalletRecharge,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit rechargeCompleted(result);
            return;
        }

        if (amountCents < 1 || amountCents > 1000000) {
            result.response = response(requestId,
                                       protocol::MessageType::WalletRecharge,
                                       protocol::ErrorCode::InvalidRequest,
                                       QStringLiteral("充值金额必须在0.01元到10000元之间"));
            emit rechargeCompleted(result);
            return;
        }

        auto updatedUser = *user;
        updatedUser.balanceCents += amountCents;
        usersByPhone_.insert(updatedUser.phone, updatedUser);
        result.response = response(requestId,
                                   protocol::MessageType::WalletRecharge,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = RechargePayload{updatedUser.balanceCents};
        emit rechargeCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::listStations(const StationQuery &query)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, query]() {
        StationListResult result;
        if (!authenticatedUser().has_value()) {
            result.response = response(requestId,
                                       protocol::MessageType::StationList,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit stationListCompleted(result);
            return;
        }

        const bool hasLongitude = query.longitude.has_value();
        const bool hasLatitude = query.latitude.has_value();
        if (hasLongitude != hasLatitude
            || (hasLongitude
                && (!std::isfinite(*query.longitude)
                    || !std::isfinite(*query.latitude)
                    || *query.longitude < -180.0 || *query.longitude > 180.0
                    || *query.latitude < -90.0 || *query.latitude > 90.0))) {
            result.response = response(requestId,
                                       protocol::MessageType::StationList,
                                       protocol::ErrorCode::InvalidRequest,
                                       QStringLiteral("经纬度参数无效"));
            emit stationListCompleted(result);
            return;
        }

        QList<protocol::StationDto> items;
        for (qint64 stationId : {qint64{1}, qint64{2}}) {
            protocol::StationDto item = station(stationId);
            if (!query.region.trimmed().isEmpty()
                && item.region.compare(query.region.trimmed(), Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString keyword = query.keyword.trimmed();
            if (!keyword.isEmpty()
                && !item.name.contains(keyword, Qt::CaseInsensitive)
                && !item.address.contains(keyword, Qt::CaseInsensitive)) {
                continue;
            }
            if (hasLongitude) {
                item.distanceKm = distanceKm(*query.longitude,
                                             *query.latitude,
                                             item.longitude,
                                             item.latitude);
            }
            items.append(item);
        }

        std::sort(items.begin(), items.end(), [hasLongitude](const auto &left,
                                                             const auto &right) {
            if (hasLongitude) {
                return left.distanceKm.value() < right.distanceKm.value();
            }
            return left.stationId < right.stationId;
        });

        for (auto &item : items) {
            item.recommended = false;
        }
        const auto recommended = std::find_if(items.begin(), items.end(), [](const auto &item) {
            return item.predictedCongestion == protocol::CongestionLevel::Low;
        });
        if (recommended != items.end()) {
            recommended->recommended = true;
        }

        result.response = response(requestId,
                                   protocol::MessageType::StationList,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = StationListPayload{items};
        emit stationListCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::getStation(qint64 stationId)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, stationId]() {
        StationDetailResult result;
        if (!authenticatedUser().has_value()) {
            result.response = response(requestId,
                                       protocol::MessageType::StationDetail,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit stationDetailCompleted(result);
            return;
        }
        if (stationId != 1 && stationId != 2) {
            result.response = response(requestId,
                                       protocol::MessageType::StationDetail,
                                       protocol::ErrorCode::NotFound,
                                       QStringLiteral("充电站不存在"));
            emit stationDetailCompleted(result);
            return;
        }

        protocol::StationDto item = station(stationId);
        item.distanceKm.reset();
        item.recommended = false;
        result.response = response(requestId,
                                   protocol::MessageType::StationDetail,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = StationDetailPayload{item, piles(stationId)};
        emit stationDetailCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::getCurrentOrder()
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId]() {
        CurrentOrderResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderCurrent,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit currentOrderCompleted(result);
            return;
        }

        result.response = response(requestId,
                                   protocol::MessageType::OrderCurrent,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = CurrentOrderPayload{currentOrder(user->userId)};
        emit currentOrderCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::listOrders()
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId]() {
        OrderListResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderList,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit orderListCompleted(result);
            return;
        }

        QList<protocol::OrderDto> orders;
        for (const auto &order : ordersById_) {
            if (order.userId == user->userId) {
                orders.append(order);
            }
        }
        std::sort(orders.begin(), orders.end(), [](const auto &left, const auto &right) {
            if (left.createdAt != right.createdAt) {
                return left.createdAt > right.createdAt;
            }
            return left.orderId > right.orderId;
        });

        result.response = response(requestId,
                                   protocol::MessageType::OrderList,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = OrderListPayload{orders};
        emit orderListCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::reserve(const QString &pileCode)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, pileCode]() {
        OrderResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderReserve,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit reservationCompleted(result);
            return;
        }

        const QString normalizedPileCode = pileCode.trimmed();
        if (normalizedPileCode.isEmpty() || normalizedPileCode.size() > 64) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderReserve,
                                       protocol::ErrorCode::InvalidRequest,
                                       QStringLiteral("充电桩编号无效"));
            emit reservationCompleted(result);
            return;
        }
        if (currentOrder(user->userId).has_value()) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderReserve,
                                       protocol::ErrorCode::CurrentOrderExists,
                                       QStringLiteral("您已有进行中的订单，请先处理"));
            emit reservationCompleted(result);
            return;
        }
        if (!pilesByCode_.contains(normalizedPileCode)) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderReserve,
                                       protocol::ErrorCode::NotFound,
                                       QStringLiteral("充电桩不存在"));
            emit reservationCompleted(result);
            return;
        }

        protocol::PileDto pile = pilesByCode_.value(normalizedPileCode);
        if (pile.status != protocol::PileStatus::Idle) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderReserve,
                                       protocol::ErrorCode::PileNotAvailable,
                                       QStringLiteral("该充电桩当前不可预约"));
            emit reservationCompleted(result);
            return;
        }

        const protocol::StationDto selectedStation = station(pile.stationId);
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        protocol::OrderDto order;
        order.orderId = nextOrderId_++;
        order.orderNo = QStringLiteral("MOCK-RES-%1").arg(order.orderId);
        order.createdAt = now;
        order.userId = user->userId;
        order.stationId = selectedStation.stationId;
        order.stationName = selectedStation.name;
        order.pileId = pile.pileId;
        order.pileCode = pile.pileCode;
        order.mode = protocol::OrderMode::Reservation;
        order.status = protocol::OrderStatus::Reserved;
        order.reservedAt = now;
        order.durationSeconds = 0;
        order.energyWh = 0;
        order.unitPriceCentsPerKwh.reset();
        order.amountCents = 0;

        pile.status = protocol::PileStatus::Reserved;
        pilesByCode_.insert(pile.pileCode, pile);
        ordersById_.insert(order.orderId, order);

        result.response = response(requestId,
                                   protocol::MessageType::OrderReserve,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = OrderPayload{order};
        emit reservationCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::cancel(qint64 orderId)
{
    const QString requestId = nextRequestId();

    QTimer::singleShot(0, this, [this, requestId, orderId]() {
        OrderResult result;
        const auto user = authenticatedUser();
        if (!user.has_value()) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderCancel,
                                       protocol::ErrorCode::InvalidSession,
                                       QStringLiteral("请先登录"));
            emit cancellationCompleted(result);
            return;
        }
        if (!ordersById_.contains(orderId)) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderCancel,
                                       protocol::ErrorCode::NotFound,
                                       QStringLiteral("订单不存在"));
            emit cancellationCompleted(result);
            return;
        }

        protocol::OrderDto order = ordersById_.value(orderId);
        if (order.userId != user->userId) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderCancel,
                                       protocol::ErrorCode::Forbidden,
                                       QStringLiteral("不能操作其他用户的订单"));
            emit cancellationCompleted(result);
            return;
        }
        if (order.status != protocol::OrderStatus::Reserved) {
            result.response = response(requestId,
                                       protocol::MessageType::OrderCancel,
                                       protocol::ErrorCode::IllegalOrderState,
                                       QStringLiteral("当前订单状态不能取消"));
            emit cancellationCompleted(result);
            return;
        }

        order.status = protocol::OrderStatus::Cancelled;
        ordersById_.insert(order.orderId, order);
        protocol::PileDto pile = pilesByCode_.value(order.pileCode);
        pile.status = protocol::PileStatus::Idle;
        pilesByCode_.insert(pile.pileCode, pile);

        result.response = response(requestId,
                                   protocol::MessageType::OrderCancel,
                                   protocol::ErrorCode::Ok,
                                   QStringLiteral("OK"));
        result.payload = OrderPayload{order};
        emit cancellationCompleted(result);
    });

    return requestId;
}

QString MockChargingApi::nextRequestId()
{
    return QStringLiteral("mock-%1").arg(++requestSequence_);
}

ApiResponse MockChargingApi::response(const QString &requestId,
                                      const char *type,
                                      int code,
                                      const QString &message) const
{
    return ApiResponse{requestId, QString::fromLatin1(type), code, message};
}

std::optional<protocol::UserDto> MockChargingApi::authenticatedUser() const
{
    if (token_.isEmpty() || authenticatedPhone_.isEmpty()
        || !usersByPhone_.contains(authenticatedPhone_)) {
        return std::nullopt;
    }

    return usersByPhone_.value(authenticatedPhone_);
}

protocol::StationDto MockChargingApi::station(qint64 stationId) const
{
    protocol::StationDto item;
    item.stationId = stationId;
    item.status = protocol::StationStatus::Active;
    item.distanceKm.reset();
    item.recommended = false;
    if (stationId == 1) {
        item.name = QStringLiteral("浑南演示充电站");
        item.region = QStringLiteral("浑南区");
        item.address = QStringLiteral("浑南区创新路1号");
        item.longitude = 123.43;
        item.latitude = 41.71;
        item.priceCentsPerKwh = 135;
        item.predictedCongestion = protocol::CongestionLevel::Low;
    } else {
        item.name = QStringLiteral("和平演示充电站");
        item.region = QStringLiteral("和平区");
        item.address = QStringLiteral("和平区青年大街2号");
        item.longitude = 123.40;
        item.latitude = 41.79;
        item.priceCentsPerKwh = 120;
        item.predictedCongestion = protocol::CongestionLevel::Medium;
    }
    const QList<protocol::PileDto> stationPiles = piles(stationId);
    item.totalPileCount = stationPiles.size();
    qint64 onlinePileCount = 0;
    for (const auto &pile : stationPiles) {
        if (pile.status == protocol::PileStatus::Idle) {
            ++item.availablePileCount;
        }
        if (pile.status != protocol::PileStatus::Offline) {
            ++onlinePileCount;
        }
    }
    item.onlineRatePercent = item.totalPileCount == 0
        ? 0.0
        : 100.0 * onlinePileCount / item.totalPileCount;
    return item;
}

QList<protocol::PileDto> MockChargingApi::piles(qint64 stationId) const
{
    QList<protocol::PileDto> stationPiles;
    for (const auto &pile : pilesByCode_) {
        if (pile.stationId == stationId) {
            stationPiles.append(pile);
        }
    }
    std::sort(stationPiles.begin(), stationPiles.end(), [](const auto &left,
                                                           const auto &right) {
        return left.pileId < right.pileId;
    });
    return stationPiles;
}

std::optional<protocol::OrderDto> MockChargingApi::currentOrder(qint64 userId) const
{
    for (const auto &order : ordersById_) {
        if (order.userId == userId && isCurrentOrderStatus(order.status)) {
            return order;
        }
    }
    return std::nullopt;
}

}  // namespace charging::client
