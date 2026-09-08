#include "api/mock_charging_api.h"

#include "charging/protocol/protocol_constants.h"

#include <QSignalSpy>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace charging;

class MockChargingApiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void existingFixtureUserCanLogin();
    void unknownPhoneIsAutomaticallyRegisteredOnce();
    void invalidPhoneCompletesWithInvalidRequest();
    void profileRequiresAndUsesAdapterSession();
    void rechargeRequiresSessionAndReturnsAuthoritativeBalance();
    void stationListRequiresSessionAndAppliesQuery();
    void stationDetailReturnsPilesAndNotFound();
    void orderListRequiresSessionAndReturnsNewestFirst();
    void reservationCreatesCurrentOrderAndUpdatesPile();
    void reservationDeadline_data();
    void reservationDeadline();
    void reservationTimerWorksAfterLogout();
    void chargingStartsDirectlyOrFromMatchingReservation();
    void chargingProgressAndStopUseAuthoritativeSettlement();
    void pendingPaymentCanBePaidOnlyAfterRecharge();
    void cancellationReleasesPileAndRejectsIllegalState();
    void peakQuotesAndStart_data();
    void peakQuotesAndStart();
    void peakReservationAndSettlementKeepSnapshot();
    void peakAutomaticStopKeepsSnapshot();
};

void MockChargingApiTests::initTestCase()
{
    qRegisterMetaType<client::LoginResult>();
    qRegisterMetaType<client::LogoutResult>();
    qRegisterMetaType<client::UserResult>();
    qRegisterMetaType<client::RechargeResult>();
    qRegisterMetaType<client::StationListResult>();
    qRegisterMetaType<client::StationDetailResult>();
    qRegisterMetaType<client::CurrentOrderResult>();
    qRegisterMetaType<client::OrderResult>();
    qRegisterMetaType<client::OrderListResult>();
    qRegisterMetaType<client::ChargingProgressResult>();
    qRegisterMetaType<client::ChargingStopResult>();
    qRegisterMetaType<client::PaymentResult>();
}

void MockChargingApiTests::existingFixtureUserCanLogin()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);

    const QString requestId = api.loginUser(QStringLiteral("13800000001"));

    QCOMPARE(loginSpy.count(), 0);
    QTRY_COMPARE(loginSpy.count(), 1);
    const auto result = qvariant_cast<client::LoginResult>(loginSpy.takeFirst().at(0));
    QVERIFY(result.ok());
    QCOMPARE(result.response.requestId, requestId);
    QVERIFY(result.payload.has_value());
    QVERIFY(!result.payload->isNewUser);
    QCOMPARE(result.payload->user.nickname, QStringLiteral("演示用户0001"));
    QCOMPARE(result.payload->user.balanceCents, 20000);
    QVERIFY(!result.payload->token.isEmpty());
}

void MockChargingApiTests::unknownPhoneIsAutomaticallyRegisteredOnce()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);

    const QString firstRequestId = api.loginUser(QStringLiteral("13912345678"));
    QVERIFY(!firstRequestId.isEmpty());
    QTRY_COMPARE(loginSpy.count(), 1);
    const auto firstResult =
        qvariant_cast<client::LoginResult>(loginSpy.takeFirst().at(0));
    QVERIFY(firstResult.ok());
    QVERIFY(firstResult.payload->isNewUser);
    QCOMPARE(firstResult.payload->user.nickname, QStringLiteral("用户5678"));
    QCOMPARE(firstResult.payload->user.balanceCents, 0);

    const QString secondRequestId = api.loginUser(QStringLiteral("13912345678"));
    QVERIFY(!secondRequestId.isEmpty());
    QTRY_COMPARE(loginSpy.count(), 1);
    const auto secondResult =
        qvariant_cast<client::LoginResult>(loginSpy.takeFirst().at(0));
    QVERIFY(secondResult.ok());
    QVERIFY(!secondResult.payload->isNewUser);
    QCOMPARE(secondResult.payload->user.userId, firstResult.payload->user.userId);
}

void MockChargingApiTests::invalidPhoneCompletesWithInvalidRequest()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);

    const QString requestId = api.loginUser(QStringLiteral("123"));

    QTRY_COMPARE(loginSpy.count(), 1);
    const auto result = qvariant_cast<client::LoginResult>(loginSpy.takeFirst().at(0));
    QCOMPARE(result.response.requestId, requestId);
    QCOMPARE(result.response.code, protocol::ErrorCode::InvalidRequest);
    QVERIFY(!result.payload.has_value());
}

void MockChargingApiTests::profileRequiresAndUsesAdapterSession()
{
    client::MockChargingApi api;
    QSignalSpy profileSpy(&api, &client::IChargingApi::profileCompleted);
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy logoutSpy(&api, &client::IChargingApi::logoutCompleted);

    const QString unauthenticatedProfileRequestId = api.getProfile();
    QVERIFY(!unauthenticatedProfileRequestId.isEmpty());
    QTRY_COMPARE(profileSpy.count(), 1);
    auto profileResult =
        qvariant_cast<client::UserResult>(profileSpy.takeFirst().at(0));
    QCOMPARE(profileResult.response.code, protocol::ErrorCode::InvalidSession);

    const QString loginRequestId = api.loginUser(QStringLiteral("13800000001"));
    QVERIFY(!loginRequestId.isEmpty());
    QTRY_COMPARE(loginSpy.count(), 1);
    const QString profileRequestId = api.getProfile();
    QVERIFY(!profileRequestId.isEmpty());
    QTRY_COMPARE(profileSpy.count(), 1);
    profileResult = qvariant_cast<client::UserResult>(profileSpy.takeFirst().at(0));
    QVERIFY(profileResult.ok());
    QCOMPARE(profileResult.payload->user.phone, QStringLiteral("13800000001"));

    const QString logoutRequestId = api.logout();
    QVERIFY(!logoutRequestId.isEmpty());
    QTRY_COMPARE(logoutSpy.count(), 1);
    const auto logoutResult =
        qvariant_cast<client::LogoutResult>(logoutSpy.takeFirst().at(0));
    QVERIFY(logoutResult.ok());
    QVERIFY(logoutResult.payload->success);

    const QString expiredProfileRequestId = api.getProfile();
    QVERIFY(!expiredProfileRequestId.isEmpty());
    QTRY_COMPARE(profileSpy.count(), 1);
    profileResult = qvariant_cast<client::UserResult>(profileSpy.takeFirst().at(0));
    QCOMPARE(profileResult.response.code, protocol::ErrorCode::InvalidSession);
}

void MockChargingApiTests::rechargeRequiresSessionAndReturnsAuthoritativeBalance()
{
    client::MockChargingApi api;
    QSignalSpy rechargeSpy(&api, &client::IChargingApi::rechargeCompleted);
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy profileSpy(&api, &client::IChargingApi::profileCompleted);

    const QString noSessionRequestId = api.recharge(1000);
    QVERIFY(!noSessionRequestId.isEmpty());
    QTRY_COMPARE(rechargeSpy.count(), 1);
    auto result =
        qvariant_cast<client::RechargeResult>(rechargeSpy.takeFirst().at(0));
    QCOMPARE(result.response.code, protocol::ErrorCode::InvalidSession);
    QVERIFY(!result.payload.has_value());

    const QString loginRequestId = api.loginUser(QStringLiteral("13800000001"));
    QVERIFY(!loginRequestId.isEmpty());
    QTRY_COMPARE(loginSpy.count(), 1);

    const QString invalidAmountRequestId = api.recharge(0);
    QVERIFY(!invalidAmountRequestId.isEmpty());
    QTRY_COMPARE(rechargeSpy.count(), 1);
    result = qvariant_cast<client::RechargeResult>(rechargeSpy.takeFirst().at(0));
    QCOMPARE(result.response.code, protocol::ErrorCode::InvalidRequest);

    const QString rechargeRequestId = api.recharge(1000);
    QTRY_COMPARE(rechargeSpy.count(), 1);
    result = qvariant_cast<client::RechargeResult>(rechargeSpy.takeFirst().at(0));
    QVERIFY(result.ok());
    QCOMPARE(result.response.requestId, rechargeRequestId);
    QVERIFY(result.payload.has_value());
    QCOMPARE(result.payload->balanceCents, 21000);

    const QString profileRequestId = api.getProfile();
    QVERIFY(!profileRequestId.isEmpty());
    QTRY_COMPARE(profileSpy.count(), 1);
    const auto profileResult =
        qvariant_cast<client::UserResult>(profileSpy.takeFirst().at(0));
    QVERIFY(profileResult.ok());
    QCOMPARE(profileResult.payload->user.balanceCents, 21000);
}

void MockChargingApiTests::stationListRequiresSessionAndAppliesQuery()
{
    client::MockChargingApi api;
    QSignalSpy stationSpy(&api, &client::IChargingApi::stationListCompleted);
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);

    const QString noSessionRequestId = api.listStations({});
    QVERIFY(!noSessionRequestId.isEmpty());
    QTRY_COMPARE(stationSpy.count(), 1);
    auto result =
        qvariant_cast<client::StationListResult>(stationSpy.takeFirst().at(0));
    QCOMPARE(result.response.code, protocol::ErrorCode::InvalidSession);

    (void)api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(loginSpy.count(), 1);

    client::StationQuery query;
    query.longitude = 123.42;
    query.latitude = 41.70;
    const QString requestId = api.listStations(query);
    QTRY_COMPARE(stationSpy.count(), 1);
    result = qvariant_cast<client::StationListResult>(stationSpy.takeFirst().at(0));
    QVERIFY(result.ok());
    QCOMPARE(result.response.requestId, requestId);
    QCOMPARE(result.response.type,
             QString::fromLatin1(protocol::MessageType::StationList));
    QVERIFY(result.payload.has_value());
    QCOMPARE(result.payload->items.size(), 2);
    QCOMPARE(result.payload->items.first().stationId, 1);
    QVERIFY(result.payload->items.first().distanceKm.has_value());
    QVERIFY(result.payload->items.first().recommended);

    client::StationQuery queryWithoutLocation;
    (void)api.listStations(queryWithoutLocation);
    QTRY_COMPARE(stationSpy.count(), 1);
    result = qvariant_cast<client::StationListResult>(stationSpy.takeFirst().at(0));
    QVERIFY(result.ok());
    QCOMPARE(result.payload->items.size(), 2);
    QVERIFY(!result.payload->items.first().distanceKm.has_value());

    query.region = QStringLiteral("和平区");
    (void)api.listStations(query);
    QTRY_COMPARE(stationSpy.count(), 1);
    result = qvariant_cast<client::StationListResult>(stationSpy.takeFirst().at(0));
    QVERIFY(result.ok());
    QCOMPARE(result.payload->items.size(), 1);
    QCOMPARE(result.payload->items.first().stationId, 2);

    query.region.clear();
    query.keyword = QStringLiteral("不存在");
    (void)api.listStations(query);
    QTRY_COMPARE(stationSpy.count(), 1);
    result = qvariant_cast<client::StationListResult>(stationSpy.takeFirst().at(0));
    QVERIFY(result.ok());
    QVERIFY(result.payload->items.isEmpty());

    query.keyword.clear();
    query.latitude.reset();
    (void)api.listStations(query);
    QTRY_COMPARE(stationSpy.count(), 1);
    result = qvariant_cast<client::StationListResult>(stationSpy.takeFirst().at(0));
    QCOMPARE(result.response.code, protocol::ErrorCode::InvalidRequest);
}

void MockChargingApiTests::stationDetailReturnsPilesAndNotFound()
{
    client::MockChargingApi api;
    QSignalSpy detailSpy(&api, &client::IChargingApi::stationDetailCompleted);
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);

    (void)api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(loginSpy.count(), 1);

    const QString requestId = api.getStation(1);
    QTRY_COMPARE(detailSpy.count(), 1);
    auto result =
        qvariant_cast<client::StationDetailResult>(detailSpy.takeFirst().at(0));
    QVERIFY(result.ok());
    QCOMPARE(result.response.requestId, requestId);
    QCOMPARE(result.response.type,
             QString::fromLatin1(protocol::MessageType::StationDetail));
    QVERIFY(result.payload.has_value());
    QCOMPARE(result.payload->station.name, QStringLiteral("浑南演示充电站"));
    QCOMPARE(result.payload->piles.size(), 2);
    QCOMPARE(result.payload->piles.first().pileCode, QStringLiteral("PILE-A-01"));
    QVERIFY(result.payload->piles.first().status == protocol::PileStatus::Idle);

    (void)api.getStation(999);
    QTRY_COMPARE(detailSpy.count(), 1);
    result = qvariant_cast<client::StationDetailResult>(detailSpy.takeFirst().at(0));
    QCOMPARE(result.response.code, protocol::ErrorCode::NotFound);
    QVERIFY(!result.payload.has_value());
}

void MockChargingApiTests::orderListRequiresSessionAndReturnsNewestFirst()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy orderListSpy(&api, &client::IChargingApi::orderListCompleted);
    QSignalSpy reserveSpy(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy cancelSpy(&api, &client::IChargingApi::cancellationCompleted);

    const QString noSessionRequestId = api.listOrders();
    QTRY_COMPARE(orderListSpy.count(), 1);
    auto listResult =
        qvariant_cast<client::OrderListResult>(orderListSpy.takeFirst().at(0));
    QCOMPARE(listResult.response.requestId, noSessionRequestId);
    QCOMPARE(listResult.response.code, protocol::ErrorCode::InvalidSession);

    (void)api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(loginSpy.count(), 1);
    const QString historyRequestId = api.listOrders();
    QTRY_COMPARE(orderListSpy.count(), 1);
    listResult = qvariant_cast<client::OrderListResult>(orderListSpy.takeFirst().at(0));
    QVERIFY(listResult.ok());
    QCOMPARE(listResult.response.requestId, historyRequestId);
    QCOMPARE(listResult.response.type,
             QString::fromLatin1(protocol::MessageType::OrderList));
    QVERIFY(listResult.payload.has_value());
    QCOMPARE(listResult.payload->items.size(), 4);
    QCOMPARE(listResult.payload->items.first().orderId, 101);
    QVERIFY(listResult.payload->items.first().status
            == protocol::OrderStatus::Completed);

    (void)api.reserve(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(reserveSpy.count(), 1);
    const auto reserveResult =
        qvariant_cast<client::OrderResult>(reserveSpy.takeFirst().at(0));
    QVERIFY(reserveResult.ok());
    const qint64 reservedOrderId = reserveResult.payload->order.orderId;

    (void)api.listOrders();
    QTRY_COMPARE(orderListSpy.count(), 1);
    listResult = qvariant_cast<client::OrderListResult>(orderListSpy.takeFirst().at(0));
    QCOMPARE(listResult.payload->items.size(), 5);
    QCOMPARE(listResult.payload->items.first().orderId, reservedOrderId);
    QVERIFY(listResult.payload->items.first().status
            == protocol::OrderStatus::Reserved);

    (void)api.cancel(reservedOrderId);
    QTRY_COMPARE(cancelSpy.count(), 1);
    (void)api.listOrders();
    QTRY_COMPARE(orderListSpy.count(), 1);
    listResult = qvariant_cast<client::OrderListResult>(orderListSpy.takeFirst().at(0));
    QCOMPARE(listResult.payload->items.first().orderId, reservedOrderId);
    QVERIFY(listResult.payload->items.first().status
            == protocol::OrderStatus::Cancelled);
}

void MockChargingApiTests::reservationCreatesCurrentOrderAndUpdatesPile()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy currentSpy(&api, &client::IChargingApi::currentOrderCompleted);
    QSignalSpy reserveSpy(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy detailSpy(&api, &client::IChargingApi::stationDetailCompleted);

    (void)api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(loginSpy.count(), 1);

    const QString currentRequestId = api.getCurrentOrder();
    QTRY_COMPARE(currentSpy.count(), 1);
    auto currentResult =
        qvariant_cast<client::CurrentOrderResult>(currentSpy.takeFirst().at(0));
    QVERIFY(currentResult.ok());
    QCOMPARE(currentResult.response.requestId, currentRequestId);
    QVERIFY(currentResult.payload.has_value());
    QVERIFY(!currentResult.payload->order.has_value());

    (void)api.reserve(QStringLiteral("PILE-A-02"));
    QTRY_COMPARE(reserveSpy.count(), 1);
    auto reserveResult =
        qvariant_cast<client::OrderResult>(reserveSpy.takeFirst().at(0));
    QCOMPARE(reserveResult.response.code, protocol::ErrorCode::PileNotAvailable);
    QVERIFY(!reserveResult.payload.has_value());

    const QString reserveRequestId = api.reserve(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(reserveSpy.count(), 1);
    reserveResult = qvariant_cast<client::OrderResult>(reserveSpy.takeFirst().at(0));
    QVERIFY(reserveResult.ok());
    QCOMPARE(reserveResult.response.requestId, reserveRequestId);
    QVERIFY(reserveResult.payload.has_value());
    const auto reservedOrder = reserveResult.payload->order;
    QVERIFY(reservedOrder.status == protocol::OrderStatus::Reserved);
    QVERIFY(reservedOrder.mode == protocol::OrderMode::Reservation);
    QCOMPARE(reservedOrder.pileCode, QStringLiteral("PILE-A-01"));
    QVERIFY(reservedOrder.reservedAt.has_value());
    QVERIFY(!reservedOrder.unitPriceCentsPerKwh.has_value());

    (void)api.getCurrentOrder();
    QTRY_COMPARE(currentSpy.count(), 1);
    currentResult =
        qvariant_cast<client::CurrentOrderResult>(currentSpy.takeFirst().at(0));
    QVERIFY(currentResult.ok());
    QVERIFY(currentResult.payload->order.has_value());
    QCOMPARE(currentResult.payload->order->orderId, reservedOrder.orderId);

    (void)api.getStation(1);
    QTRY_COMPARE(detailSpy.count(), 1);
    const auto detailResult =
        qvariant_cast<client::StationDetailResult>(detailSpy.takeFirst().at(0));
    QVERIFY(detailResult.ok());
    QCOMPARE(detailResult.payload->station.availablePileCount, 0);
    QVERIFY(detailResult.payload->piles.first().status
            == protocol::PileStatus::Reserved);

    (void)api.reserve(QStringLiteral("PILE-B-02"));
    QTRY_COMPARE(reserveSpy.count(), 1);
    reserveResult = qvariant_cast<client::OrderResult>(reserveSpy.takeFirst().at(0));
    QCOMPARE(reserveResult.response.code, protocol::ErrorCode::CurrentOrderExists);
}

void MockChargingApiTests::chargingStartsDirectlyOrFromMatchingReservation()
{
    client::MockChargingApi directApi(nullptr, [] {
        return QDateTime::fromString(QStringLiteral("2026-09-08T04:00:00Z"), Qt::ISODate);
    });
    QSignalSpy directLoginSpy(&directApi, &client::IChargingApi::loginCompleted);
    QSignalSpy directStartSpy(&directApi, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy directDetailSpy(&directApi, &client::IChargingApi::stationDetailCompleted);

    (void)directApi.startCharging(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(directStartSpy.count(), 1);
    auto startResult =
        qvariant_cast<client::OrderResult>(directStartSpy.takeFirst().at(0));
    QCOMPARE(startResult.response.code, protocol::ErrorCode::InvalidSession);

    (void)directApi.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(directLoginSpy.count(), 1);
    const QString directRequestId =
        directApi.startCharging(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(directStartSpy.count(), 1);
    startResult = qvariant_cast<client::OrderResult>(directStartSpy.takeFirst().at(0));
    QVERIFY(startResult.ok());
    QCOMPARE(startResult.response.requestId, directRequestId);
    QVERIFY(startResult.payload->order.mode == protocol::OrderMode::Direct);
    QVERIFY(startResult.payload->order.status == protocol::OrderStatus::Charging);
    QVERIFY(startResult.payload->order.startedAt.has_value());
    QCOMPARE(startResult.payload->order.unitPriceCentsPerKwh, 135);

    (void)directApi.getStation(1);
    QTRY_COMPARE(directDetailSpy.count(), 1);
    const auto directDetail = qvariant_cast<client::StationDetailResult>(
        directDetailSpy.takeFirst().at(0));
    QVERIFY(directDetail.payload->piles.first().status
            == protocol::PileStatus::Charging);

    (void)directApi.startCharging(QStringLiteral("PILE-B-02"));
    QTRY_COMPARE(directStartSpy.count(), 1);
    startResult = qvariant_cast<client::OrderResult>(directStartSpy.takeFirst().at(0));
    QCOMPARE(startResult.response.code, protocol::ErrorCode::CurrentOrderExists);

    client::MockChargingApi reservationApi;
    QSignalSpy reservationLoginSpy(&reservationApi,
                                   &client::IChargingApi::loginCompleted);
    QSignalSpy reserveSpy(&reservationApi, &client::IChargingApi::reservationCompleted);
    QSignalSpy reservationStartSpy(&reservationApi,
                                   &client::IChargingApi::chargingStartCompleted);
    (void)reservationApi.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(reservationLoginSpy.count(), 1);
    (void)reservationApi.reserve(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(reserveSpy.count(), 1);
    const auto reserveResult =
        qvariant_cast<client::OrderResult>(reserveSpy.takeFirst().at(0));
    const qint64 reservationOrderId = reserveResult.payload->order.orderId;

    (void)reservationApi.startCharging(QStringLiteral("PILE-B-02"),
                                       reservationOrderId);
    QTRY_COMPARE(reservationStartSpy.count(), 1);
    startResult = qvariant_cast<client::OrderResult>(
        reservationStartSpy.takeFirst().at(0));
    QCOMPARE(startResult.response.code, protocol::ErrorCode::IllegalOrderState);

    (void)reservationApi.startCharging(QStringLiteral("PILE-A-01"),
                                       reservationOrderId);
    QTRY_COMPARE(reservationStartSpy.count(), 1);
    startResult = qvariant_cast<client::OrderResult>(
        reservationStartSpy.takeFirst().at(0));
    QVERIFY(startResult.ok());
    QCOMPARE(startResult.payload->order.orderId, reservationOrderId);
    QVERIFY(startResult.payload->order.mode == protocol::OrderMode::Reservation);
    QVERIFY(startResult.payload->order.status == protocol::OrderStatus::Charging);
}

void MockChargingApiTests::chargingProgressAndStopUseAuthoritativeSettlement()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy startSpy(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy progressSpy(&api, &client::IChargingApi::chargingProgressCompleted);
    QSignalSpy stopSpy(&api, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy profileSpy(&api, &client::IChargingApi::profileCompleted);
    QSignalSpy detailSpy(&api, &client::IChargingApi::stationDetailCompleted);

    (void)api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(loginSpy.count(), 1);
    (void)api.startCharging(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(startSpy.count(), 1);
    const auto startResult =
        qvariant_cast<client::OrderResult>(startSpy.takeFirst().at(0));
    const qint64 orderId = startResult.payload->order.orderId;

    (void)api.getChargingProgress(orderId);
    QTRY_COMPARE(progressSpy.count(), 1);
    auto progressResult = qvariant_cast<client::ChargingProgressResult>(
        progressSpy.takeFirst().at(0));
    QVERIFY(progressResult.ok());
    QCOMPARE(progressResult.payload->order.durationSeconds, 60);
    QVERIFY(progressResult.payload->order.energyWh > 0);
    QVERIFY(!progressResult.payload->measuredAt.isEmpty());
    const qint64 firstEnergyWh = progressResult.payload->order.energyWh;

    (void)api.getChargingProgress(orderId);
    QTRY_COMPARE(progressSpy.count(), 1);
    progressResult = qvariant_cast<client::ChargingProgressResult>(
        progressSpy.takeFirst().at(0));
    QCOMPARE(progressResult.payload->order.durationSeconds, 120);
    QVERIFY(progressResult.payload->order.energyWh >= firstEnergyWh);

    (void)api.stopCharging(orderId);
    QTRY_COMPARE(stopSpy.count(), 1);
    auto stopResult = qvariant_cast<client::ChargingStopResult>(
        stopSpy.takeFirst().at(0));
    QVERIFY(stopResult.ok());
    QVERIFY(stopResult.payload->paid);
    QVERIFY(stopResult.payload->order.status == protocol::OrderStatus::Completed);
    QVERIFY(stopResult.payload->order.endedAt.has_value());
    QVERIFY(stopResult.payload->order.paidAt.has_value());
    QVERIFY(!stopResult.payload->shortfallCents.has_value());
    QCOMPARE(stopResult.payload->balanceCents,
             20000 - stopResult.payload->order.amountCents);

    (void)api.getProfile();
    QTRY_COMPARE(profileSpy.count(), 1);
    const auto profileResult =
        qvariant_cast<client::UserResult>(profileSpy.takeFirst().at(0));
    QCOMPARE(profileResult.payload->user.balanceCents,
             stopResult.payload->balanceCents);
    (void)api.getStation(1);
    QTRY_COMPARE(detailSpy.count(), 1);
    const auto detailResult = qvariant_cast<client::StationDetailResult>(
        detailSpy.takeFirst().at(0));
    QVERIFY(detailResult.payload->piles.first().status
            == protocol::PileStatus::Idle);

    (void)api.stopCharging(orderId);
    QTRY_COMPARE(stopSpy.count(), 1);
    stopResult = qvariant_cast<client::ChargingStopResult>(
        stopSpy.takeFirst().at(0));
    QCOMPARE(stopResult.response.code, protocol::ErrorCode::IllegalOrderState);

    client::MockChargingApi insufficientApi;
    QSignalSpy insufficientLoginSpy(&insufficientApi,
                                    &client::IChargingApi::loginCompleted);
    QSignalSpy insufficientStartSpy(&insufficientApi,
                                    &client::IChargingApi::chargingStartCompleted);
    QSignalSpy insufficientProgressSpy(
        &insufficientApi, &client::IChargingApi::chargingProgressCompleted);
    QSignalSpy insufficientStopSpy(&insufficientApi,
                                   &client::IChargingApi::chargingStopCompleted);
    (void)insufficientApi.loginUser(QStringLiteral("13912345678"));
    QTRY_COMPARE(insufficientLoginSpy.count(), 1);
    (void)insufficientApi.startCharging(QStringLiteral("PILE-B-02"));
    QTRY_COMPARE(insufficientStartSpy.count(), 1);
    const auto insufficientStart = qvariant_cast<client::OrderResult>(
        insufficientStartSpy.takeFirst().at(0));
    (void)insufficientApi.getChargingProgress(
        insufficientStart.payload->order.orderId);
    QTRY_COMPARE(insufficientProgressSpy.count(), 1);
    (void)insufficientApi.stopCharging(insufficientStart.payload->order.orderId);
    QTRY_COMPARE(insufficientStopSpy.count(), 1);
    const auto insufficientStop = qvariant_cast<client::ChargingStopResult>(
        insufficientStopSpy.takeFirst().at(0));
    QVERIFY(insufficientStop.ok());
    QVERIFY(!insufficientStop.payload->paid);
    QVERIFY(insufficientStop.payload->order.status
            == protocol::OrderStatus::PendingPayment);
    QCOMPARE(insufficientStop.payload->balanceCents, 0);
    QCOMPARE(insufficientStop.payload->shortfallCents,
             std::optional<qint64>{insufficientStop.payload->order.amountCents});
}

void MockChargingApiTests::pendingPaymentCanBePaidOnlyAfterRecharge()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy startSpy(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy progressSpy(&api, &client::IChargingApi::chargingProgressCompleted);
    QSignalSpy stopSpy(&api, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy paySpy(&api, &client::IChargingApi::paymentCompleted);
    QSignalSpy rechargeSpy(&api, &client::IChargingApi::rechargeCompleted);

    (void)api.loginUser(QStringLiteral("13912345678"));
    QTRY_COMPARE(loginSpy.count(), 1);
    (void)api.startCharging(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(startSpy.count(), 1);
    const auto start = qvariant_cast<client::OrderResult>(startSpy.takeFirst().at(0));
    const qint64 orderId = start.payload->order.orderId;
    (void)api.getChargingProgress(orderId);
    QTRY_COMPARE(progressSpy.count(), 1);
    (void)api.stopCharging(orderId);
    QTRY_COMPARE(stopSpy.count(), 1);
    const auto stop = qvariant_cast<client::ChargingStopResult>(
        stopSpy.takeFirst().at(0));
    QVERIFY(stop.payload->order.status == protocol::OrderStatus::PendingPayment);

    (void)api.payOrder(orderId);
    QTRY_COMPARE(paySpy.count(), 1);
    auto payment = qvariant_cast<client::PaymentResult>(paySpy.takeFirst().at(0));
    QCOMPARE(payment.response.code, protocol::ErrorCode::InsufficientBalance);
    QVERIFY(!payment.payload.has_value());

    (void)api.recharge(stop.payload->order.amountCents);
    QTRY_COMPARE(rechargeSpy.count(), 1);
    (void)api.payOrder(orderId);
    QTRY_COMPARE(paySpy.count(), 1);
    payment = qvariant_cast<client::PaymentResult>(paySpy.takeFirst().at(0));
    QVERIFY(payment.ok());
    QCOMPARE(payment.payload->order.amountCents, stop.payload->order.amountCents);
    QCOMPARE(payment.payload->balanceCents, 0);
    QVERIFY(payment.payload->order.status == protocol::OrderStatus::Completed);
    QVERIFY(payment.payload->order.paidAt.has_value());

    (void)api.payOrder(orderId);
    QTRY_COMPARE(paySpy.count(), 1);
    payment = qvariant_cast<client::PaymentResult>(paySpy.takeFirst().at(0));
    QCOMPARE(payment.response.code, protocol::ErrorCode::IllegalOrderState);
}

void MockChargingApiTests::cancellationReleasesPileAndRejectsIllegalState()
{
    client::MockChargingApi api;
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy currentSpy(&api, &client::IChargingApi::currentOrderCompleted);
    QSignalSpy reserveSpy(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy cancelSpy(&api, &client::IChargingApi::cancellationCompleted);
    QSignalSpy detailSpy(&api, &client::IChargingApi::stationDetailCompleted);

    (void)api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE(loginSpy.count(), 1);
    (void)api.reserve(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE(reserveSpy.count(), 1);
    const auto reserveResult =
        qvariant_cast<client::OrderResult>(reserveSpy.takeFirst().at(0));
    const qint64 orderId = reserveResult.payload->order.orderId;

    const QString cancelRequestId = api.cancel(orderId);
    QTRY_COMPARE(cancelSpy.count(), 1);
    auto cancelResult =
        qvariant_cast<client::OrderResult>(cancelSpy.takeFirst().at(0));
    QVERIFY(cancelResult.ok());
    QCOMPARE(cancelResult.response.requestId, cancelRequestId);
    QVERIFY(cancelResult.payload->order.status == protocol::OrderStatus::Cancelled);

    (void)api.getCurrentOrder();
    QTRY_COMPARE(currentSpy.count(), 1);
    const auto currentResult =
        qvariant_cast<client::CurrentOrderResult>(currentSpy.takeFirst().at(0));
    QVERIFY(currentResult.ok());
    QVERIFY(!currentResult.payload->order.has_value());

    (void)api.getStation(1);
    QTRY_COMPARE(detailSpy.count(), 1);
    const auto detailResult =
        qvariant_cast<client::StationDetailResult>(detailSpy.takeFirst().at(0));
    QVERIFY(detailResult.ok());
    QCOMPARE(detailResult.payload->station.availablePileCount, 1);
    QVERIFY(detailResult.payload->piles.first().status == protocol::PileStatus::Idle);

    (void)api.cancel(orderId);
    QTRY_COMPARE(cancelSpy.count(), 1);
    cancelResult = qvariant_cast<client::OrderResult>(cancelSpy.takeFirst().at(0));
    QCOMPARE(cancelResult.response.code, protocol::ErrorCode::IllegalOrderState);
}

void MockChargingApiTests::peakQuotesAndStart_data()
{
    QTest::addColumn<QDateTime>("now");
    QTest::addColumn<qint64>("price135");
    QTest::addColumn<qint64>("price120");
    QFile file(QString::fromUtf8(CHARGING_PEAK_FIXTURE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto cases = QJsonDocument::fromJson(file.readAll()).object().value("cases").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &value : cases) {
        const auto row = value.toObject();
        QTest::newRow(qPrintable(row.value("name").toString()))
            << QDateTime::fromString(row.value("now").toString(), Qt::ISODate)
            << row.value("price135").toInteger() << row.value("price120").toInteger();
    }
}

void MockChargingApiTests::peakQuotesAndStart()
{
    QFETCH(QDateTime, now);
    QFETCH(qint64, price135);
    QFETCH(qint64, price120);
    client::MockChargingApi api(nullptr, [&now] { return now; });
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy list(&api, &client::IChargingApi::stationListCompleted);
    QSignalSpy detail(&api, &client::IChargingApi::stationDetailCompleted);
    QSignalSpy start(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy stop(&api, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy history(&api, &client::IChargingApi::orderListCompleted);
    (void)api.loginUser("13800000001");
    QTRY_COMPARE(login.count(), 1);
    (void)api.listOrders();
    QTRY_COMPARE(history.count(), 1);
    // Existing seed bills retain their original price, even when constructed at peak.
    for (const auto &order : qvariant_cast<client::OrderListResult>(history.first().first()).payload->items) {
        if (order.unitPriceCentsPerKwh) {
            QCOMPARE(*order.unitPriceCentsPerKwh, qint64{135});
            QCOMPARE(order.amountCents, (order.energyWh * *order.unitPriceCentsPerKwh + 500) / 1000);
        }
    }
    (void)api.listStations({});
    QTRY_COMPARE(list.count(), 1);
    const auto result = qvariant_cast<client::StationListResult>(list.first().first());
    QVERIFY(result.ok() && result.payload);
    QCOMPARE(result.payload->items.size(), 2);
    for (const auto &station : result.payload->items) {
        const qint64 expected = station.stationId == 1 ? price135 : price120;
        QCOMPARE(station.priceCentsPerKwh, expected);
        QCOMPARE(station.pricingRule, QString::fromLatin1(protocol::DemoPeakPricingRule));
        (void)api.getStation(station.stationId);
        QTRY_COMPARE(detail.count(), 1);
        const auto quoted = qvariant_cast<client::StationDetailResult>(detail.takeFirst().first());
        QVERIFY(quoted.ok() && quoted.payload);
        QCOMPARE(quoted.payload->station.priceCentsPerKwh, expected);
        (void)api.startCharging(station.stationId == 1 ? "PILE-A-01" : "PILE-B-02");
        QTRY_COMPARE(start.count(), 1);
        const auto started = qvariant_cast<client::OrderResult>(start.takeFirst().first());
        QVERIFY(started.ok() && started.payload);
        QCOMPARE(started.payload->order.unitPriceCentsPerKwh.value(), expected);
        QCOMPARE(*started.payload->order.startedAt, now.toUTC().toString(Qt::ISODate));
        (void)api.stopCharging(started.payload->order.orderId);
        QTRY_COMPARE(stop.count(), 1);
        QVERIFY(qvariant_cast<client::ChargingStopResult>(stop.takeFirst().first()).ok());
    }
}

void MockChargingApiTests::peakReservationAndSettlementKeepSnapshot()
{
    // Reservation must now be used within 30 minutes; retain the peak-to-normal
    // settlement boundary without the former three-hour reservation wait.
    auto now = QDateTime::fromString(QStringLiteral("2026-09-08T02:58:00Z"), Qt::ISODate);
    client::MockChargingApi api(nullptr, [&now] { return now; });
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy reserve(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy start(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy progress(&api, &client::IChargingApi::chargingProgressCompleted);
    QSignalSpy stop(&api, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy recharge(&api, &client::IChargingApi::rechargeCompleted);
    QSignalSpy pay(&api, &client::IChargingApi::paymentCompleted);
    (void)api.loginUser("13900000999");
    QTRY_COMPARE(login.count(), 1);
    (void)api.reserve("PILE-A-01");
    QTRY_COMPARE(reserve.count(), 1);
    const auto reserved = qvariant_cast<client::OrderResult>(reserve.first().first());
    QVERIFY(reserved.ok() && reserved.payload);
    QVERIFY(!reserved.payload->order.unitPriceCentsPerKwh);
    const auto id = reserved.payload->order.orderId;
    now = QDateTime::fromString(QStringLiteral("2026-09-08T02:59:00Z"), Qt::ISODate);
    (void)api.startCharging("PILE-A-01", id);
    QTRY_COMPARE(start.count(), 1);
    const auto started = qvariant_cast<client::OrderResult>(start.first().first());
    QVERIFY(started.ok() && started.payload);
    QCOMPARE(started.payload->order.orderId, id);
    QCOMPARE(started.payload->order.unitPriceCentsPerKwh.value(), qint64{162});
    now = now.addSecs(120); // now off-peak, 240 Wh at 7.2 kW
    (void)api.getChargingProgress(id);
    QTRY_COMPARE(progress.count(), 1);
    const auto measured = qvariant_cast<client::ChargingProgressResult>(progress.first().first());
    QCOMPARE(measured.payload->order.unitPriceCentsPerKwh.value(), qint64{162});
    QCOMPARE(measured.payload->order.amountCents, qint64{39});
    (void)api.stopCharging(id);
    QTRY_COMPARE(stop.count(), 1);
    const auto stopped = qvariant_cast<client::ChargingStopResult>(stop.first().first());
    QVERIFY(stopped.ok() && stopped.payload);
    QVERIFY(stopped.payload->order.status == protocol::OrderStatus::PendingPayment);
    QCOMPARE(stopped.payload->shortfallCents.value(), qint64{39});
    (void)api.payOrder(id);
    QTRY_COMPARE(pay.count(), 1);
    QCOMPARE(qvariant_cast<client::PaymentResult>(pay.takeFirst().first()).response.code,
             protocol::ErrorCode::InsufficientBalance);
    now = now.addDays(1);
    (void)api.recharge(1000);
    QTRY_COMPARE(recharge.count(), 1);
    (void)api.payOrder(id);
    QTRY_COMPARE(pay.count(), 1);
    const auto paid = qvariant_cast<client::PaymentResult>(pay.takeFirst().first());
    QVERIFY(paid.ok() && paid.payload);
    QCOMPARE(paid.payload->balanceCents, qint64{961});
    QCOMPARE(paid.payload->order.unitPriceCentsPerKwh.value(), qint64{162});
    QCOMPARE(paid.payload->order.amountCents, qint64{39});
    (void)api.payOrder(id);
    QTRY_COMPARE(pay.count(), 1);
    QCOMPARE(qvariant_cast<client::PaymentResult>(pay.first().first()).response.code,
             protocol::ErrorCode::IllegalOrderState);
}

void MockChargingApiTests::peakAutomaticStopKeepsSnapshot()
{
    auto now = QDateTime::fromString(QStringLiteral("2026-09-08T12:59:00Z"), Qt::ISODate);
    client::MockChargingApi api(nullptr, [&now] { return now; });
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy start(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy history(&api, &client::IChargingApi::orderListCompleted);
    (void)api.loginUser("13800000001");
    QTRY_COMPARE(login.count(), 1);
    (void)api.startCharging("PILE-A-01");
    QTRY_COMPARE(start.count(), 1);
    const auto order = qvariant_cast<client::OrderResult>(start.first().first()).payload->order;
    now = now.addSecs(200);
    QTest::qWait(1100); // allow the existing automatic-stop timer to fire
    (void)api.listOrders();
    QTRY_COMPARE(history.count(), 1);
    const auto result = qvariant_cast<client::OrderListResult>(history.first().first());
    bool found = false;
    for (const auto &item : result.payload->items) {
        if (item.orderId != order.orderId) continue;
        found = true;
        QVERIFY(item.status == protocol::OrderStatus::Completed);
        QCOMPARE(item.unitPriceCentsPerKwh.value(), qint64{162});
        QCOMPARE(item.energyWh, qint64{360});
        QCOMPARE(item.amountCents, qint64{58});
        QCOMPARE(*item.endedAt, QStringLiteral("2026-09-08T13:02:00Z"));
    }
    QVERIFY(found);
}

void MockChargingApiTests::reservationDeadline_data()
{
    QTest::addColumn<QDateTime>("reservedAt");
    QTest::addColumn<QDateTime>("now");
    QTest::addColumn<bool>("expired");
    QFile file(QString::fromUtf8(CHARGING_RESERVATION_FIXTURE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(fixture.value("durationSeconds").toInt(), protocol::DemoReservationDurationSeconds);
    const auto cases = fixture.value("cases").toArray();
    QVERIFY(!cases.isEmpty());
    for (const auto &value : cases) {
        const auto row = value.toObject();
        QTest::newRow(qPrintable(row.value("name").toString()))
            << QDateTime::fromString(row.value("reservedAt").toString(), Qt::ISODate)
            << QDateTime::fromString(row.value("now").toString(), Qt::ISODate)
            << row.value("expired").toBool();
    }
}

void MockChargingApiTests::reservationDeadline()
{
    QFETCH(QDateTime, reservedAt);
    QFETCH(QDateTime, now);
    QFETCH(bool, expired);
    auto clock = reservedAt;
    client::MockChargingApi api(nullptr, [&clock] { return clock; });
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy reserve(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy start(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy current(&api, &client::IChargingApi::currentOrderCompleted);
    QSignalSpy history(&api, &client::IChargingApi::orderListCompleted);
    QSignalSpy detail(&api, &client::IChargingApi::stationDetailCompleted);
    QSignalSpy cancel(&api, &client::IChargingApi::cancellationCompleted);
    QSignalSpy profile(&api, &client::IChargingApi::profileCompleted);
    (void)api.loginUser("13900000888"); QTRY_COMPARE(login.count(), 1);
    (void)api.reserve("PILE-A-01"); QTRY_COMPARE(reserve.count(), 1);
    const auto reserved = qvariant_cast<client::OrderResult>(reserve.takeFirst().first());
    QVERIFY(reserved.ok() && reserved.payload);
    const auto id = reserved.payload->order.orderId;
    clock = now;
    (void)api.startCharging("PILE-A-01", id); QTRY_COMPARE(start.count(), 1);
    const auto started = qvariant_cast<client::OrderResult>(start.takeFirst().first());
    QCOMPARE(started.response.code, expired ? protocol::ErrorCode::IllegalOrderState : protocol::ErrorCode::Ok);
    (void)api.getCurrentOrder(); QTRY_COMPARE(current.count(), 1);
    const auto active = qvariant_cast<client::CurrentOrderResult>(current.first().first());
    QVERIFY(active.ok() && active.payload);
    QCOMPARE(active.payload->order.has_value(), !expired);
    (void)api.listOrders(); QTRY_COMPARE(history.count(), 1);
    const auto listed = qvariant_cast<client::OrderListResult>(history.first().first());
    QVERIFY(listed.ok() && listed.payload);
    QCOMPARE(listed.payload->items.size(), 1);
    const auto order = listed.payload->items.first();
    QVERIFY(order.status == (expired ? protocol::OrderStatus::Cancelled : protocol::OrderStatus::Charging));
    (void)api.getStation(1); QTRY_COMPARE(detail.count(), 1);
    const auto station = qvariant_cast<client::StationDetailResult>(detail.first().first());
    QVERIFY(station.ok() && station.payload);
    QCOMPARE(station.payload->station.availablePileCount, expired ? 1 : 0);
    if (!expired) {
        QCOMPARE(order.orderId, id);
        QCOMPARE(order.startedAt.value(), now.toString(Qt::ISODate));
        return;
    }
    QVERIFY(!order.startedAt && !order.endedAt && !order.paidAt && !order.unitPriceCentsPerKwh);
    QCOMPARE(order.amountCents, qint64{0});
    QCOMPARE(order.energyWh, qint64{0});
    QCOMPARE(order.durationSeconds, qint64{0});
    (void)api.getProfile(); QTRY_COMPARE(profile.count(), 1);
    QCOMPARE(qvariant_cast<client::UserResult>(profile.first().first()).payload->user.balanceCents, qint64{0});
    (void)api.cancel(id); QTRY_COMPARE(cancel.count(), 1);
    QCOMPARE(qvariant_cast<client::OrderResult>(cancel.first().first()).response.code, protocol::ErrorCode::IllegalOrderState);
    (void)api.reserve("PILE-A-01"); QTRY_COMPARE(reserve.count(), 1);
    const auto next = qvariant_cast<client::OrderResult>(reserve.first().first());
    QVERIFY(next.ok() && next.payload);
    QVERIFY(next.payload->order.orderId != id);
    (void)api.startCharging("PILE-A-01", id); QTRY_COMPARE(start.count(), 1);
    QCOMPARE(qvariant_cast<client::OrderResult>(start.first().first()).response.code, protocol::ErrorCode::IllegalOrderState);
    current.clear();
    (void)api.getCurrentOrder(); QTRY_COMPARE(current.count(), 1);
    QCOMPARE(qvariant_cast<client::CurrentOrderResult>(current.first().first()).payload->order->orderId,
             next.payload->order.orderId);
}

void MockChargingApiTests::reservationTimerWorksAfterLogout()
{
    auto now = QDateTime::fromString(QStringLiteral("2026-09-08T01:00:00Z"), Qt::ISODate);
    const auto reservedAt = now;
    client::MockChargingApi api(nullptr, [&now] { return now; });
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy reserve(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy logout(&api, &client::IChargingApi::logoutCompleted);
    QSignalSpy history(&api, &client::IChargingApi::orderListCompleted);
    (void)api.loginUser("13900000888"); QTRY_COMPARE(login.count(), 1);
    (void)api.reserve("PILE-A-01"); QTRY_COMPARE(reserve.count(), 1);
    QVERIFY(qvariant_cast<client::OrderResult>(reserve.first().first()).ok());
    (void)api.logout(); QTRY_COMPARE(logout.count(), 1);
    QVERIFY(qvariant_cast<client::LogoutResult>(logout.first().first()).ok());
    now = now.addSecs(protocol::DemoReservationDurationSeconds);
    QTest::qWait(1200); // Allow one maintenance tick, without authenticated API calls.
    // Roll the injected clock back so a later query cannot itself cause expiry:
    // the stored cancellation must have occurred in the logged-out timer tick.
    now = reservedAt.addSecs(60);
    login.clear(); (void)api.loginUser("13900000888"); QTRY_COMPARE(login.count(), 1);
    (void)api.listOrders(); QTRY_COMPARE(history.count(), 1);
    const auto result = qvariant_cast<client::OrderListResult>(history.first().first());
    QVERIFY(result.ok() && result.payload);
    QVERIFY(result.payload->items.first().status == protocol::OrderStatus::Cancelled);
    QCOMPARE(result.payload->items.first().amountCents, qint64{0});
}

QTEST_GUILESS_MAIN(MockChargingApiTests)

#include "mock_charging_api_tests.moc"
