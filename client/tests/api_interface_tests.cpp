#include "api/i_charging_api.h"

#include "charging/protocol/protocol_constants.h"

#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace charging;

namespace {

class FakeChargingApi final : public client::IChargingApi {
public:
    using IChargingApi::IChargingApi;

    QString loginUser(const QString &phone) override
    {
        const QString requestId = nextRequestId();

        client::LoginResult result;
        result.response.requestId = requestId;
        result.response.type = QString::fromLatin1(protocol::MessageType::AuthUserLogin);
        result.response.code = protocol::ErrorCode::Ok;
        result.response.message = QStringLiteral("OK");
        result.payload = client::LoginPayload{
            QStringLiteral("fixture-token"),
            false,
            protocol::UserDto{
                1,
                phone,
                QStringLiteral("演示用户0001"),
                20000,
                protocol::UserStatus::Active,
                QStringLiteral("2026-06-04T11:53:41Z"),
            },
        };

        QTimer::singleShot(0, this, [this, result]() {
            emit loginCompleted(result);
        });
        return requestId;
    }

    QString logout() override
    {
        return nextRequestId();
    }

    QString getProfile() override
    {
        return nextRequestId();
    }

    QString updateNickname(const QString &nickname) override
    {
        Q_UNUSED(nickname)
        return nextRequestId();
    }

    QString recharge(qint64 amountCents) override
    {
        Q_UNUSED(amountCents)
        return nextRequestId();
    }

private:
    QString nextRequestId()
    {
        return QStringLiteral("fake-%1").arg(++requestSequence_);
    }

    int requestSequence_ = 0;
};

}  // namespace

class ApiInterfaceTests : public QObject {
    Q_OBJECT

private slots:
    void successResultReportsSuccess();
    void defaultResultDoesNotReportSuccess();
    void loginReturnsIdAndEmitsTypedCompletion();
};

void ApiInterfaceTests::successResultReportsSuccess()
{
    client::LoginResult result;
    result.response.code = protocol::ErrorCode::Ok;
    result.payload = client::LoginPayload{};

    QVERIFY(result.ok());
    QVERIFY(result.payload.has_value());
}

void ApiInterfaceTests::defaultResultDoesNotReportSuccess()
{
    const client::LoginResult result;

    QVERIFY(!result.ok());
    QVERIFY(!result.payload.has_value());
}

void ApiInterfaceTests::loginReturnsIdAndEmitsTypedCompletion()
{
    qRegisterMetaType<client::LoginResult>();

    FakeChargingApi api;
    QSignalSpy completionSpy(&api, &client::IChargingApi::loginCompleted);

    const QString requestId = api.loginUser(QStringLiteral("13800000001"));

    QCOMPARE(completionSpy.count(), 0);
    QTRY_COMPARE(completionSpy.count(), 1);
    const auto result = qvariant_cast<client::LoginResult>(completionSpy.takeFirst().at(0));
    QCOMPARE(result.response.requestId, requestId);
    QCOMPARE(result.response.type,
             QString::fromLatin1(protocol::MessageType::AuthUserLogin));
    QCOMPARE(result.response.code, protocol::ErrorCode::Ok);
    QVERIFY(result.payload.has_value());
    QCOMPARE(result.payload->token, QStringLiteral("fixture-token"));
    QCOMPARE(result.payload->user.phone, QStringLiteral("13800000001"));
}

QTEST_GUILESS_MAIN(ApiInterfaceTests)

#include "api_interface_tests.moc"
