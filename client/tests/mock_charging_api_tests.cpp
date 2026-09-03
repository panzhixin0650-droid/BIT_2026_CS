#include "api/mock_charging_api.h"

#include "charging/protocol/protocol_constants.h"

#include <QSignalSpy>
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
    void cancellationReleasesPileAndRejectsIllegalState();
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

QTEST_GUILESS_MAIN(MockChargingApiTests)

#include "mock_charging_api_tests.moc"
