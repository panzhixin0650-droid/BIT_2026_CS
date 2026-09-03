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
};

void MockChargingApiTests::initTestCase()
{
    qRegisterMetaType<client::LoginResult>();
    qRegisterMetaType<client::LogoutResult>();
    qRegisterMetaType<client::UserResult>();
    qRegisterMetaType<client::RechargeResult>();
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

QTEST_GUILESS_MAIN(MockChargingApiTests)

#include "mock_charging_api_tests.moc"
