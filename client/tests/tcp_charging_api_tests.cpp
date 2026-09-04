#include "api/tcp_charging_api.h"

#include "charging/protocol/dto.h"
#include "charging/protocol/envelope.h"
#include "charging/protocol/frame_codec.h"
#include "charging/protocol/protocol_constants.h"

#include <QJsonArray>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

#include <functional>

using namespace charging;

namespace {

class TestTcpServer final : public QObject {
public:
    explicit TestTcpServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection, this, [this]() {
            while (server_.hasPendingConnections()) {
                QTcpSocket *connection = server_.nextPendingConnection();
                if (connection == nullptr) {
                    continue;
                }
                socket_ = connection;
                decoder_.reset();
                connect(connection, &QTcpSocket::readyRead,
                        this, [this]() { readRequests(); });
                connect(connection, &QTcpSocket::disconnected,
                        connection, &QObject::deleteLater);
            }
        });
    }

    bool listen()
    {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const
    {
        return server_.serverPort();
    }

    void setHandler(std::function<void(const protocol::RequestEnvelope &)> handler)
    {
        handler_ = std::move(handler);
    }

    void reply(const protocol::RequestEnvelope &request,
               int code,
               const QJsonObject &data,
               const QString &message = QStringLiteral("OK"),
               bool splitFrame = false)
    {
        if (socket_ == nullptr) {
            return;
        }
        protocol::ResponseEnvelope response;
        response.version = request.version;
        response.type = request.type;
        response.requestId = request.requestId;
        response.code = code;
        response.message = message;
        response.data = data;
        const QByteArray frame = protocol::encodeFrame(response.toJson());
        if (!splitFrame) {
            socket_->write(frame);
            return;
        }

        socket_->write(frame.left(3));
        const QByteArray remainder = frame.mid(3);
        const QPointer<QTcpSocket> guardedSocket = socket_;
        QTimer::singleShot(5, this, [guardedSocket, remainder]() {
            if (guardedSocket != nullptr) {
                guardedSocket->write(remainder);
            }
        });
    }

    QList<protocol::RequestEnvelope> requests;
    QString error;

private:
    void readRequests()
    {
        if (socket_ == nullptr) {
            return;
        }
        const protocol::DecodeResult decoded = decoder_.append(socket_->readAll());
        if (!decoded.ok()) {
            error = decoded.errorMessage;
            return;
        }
        for (const QJsonObject &json : decoded.messages) {
            protocol::RequestEnvelope request;
            if (!protocol::RequestEnvelope::fromJson(json, &request, &error)) {
                return;
            }
            requests.append(request);
            if (handler_) {
                handler_(request);
            }
        }
    }

    QTcpServer server_;
    QPointer<QTcpSocket> socket_;
    protocol::FrameDecoder decoder_;
    std::function<void(const protocol::RequestEnvelope &)> handler_;
};

protocol::UserDto fixtureUser(const QString &nickname = QStringLiteral("演示用户0001"))
{
    return {
        1,
        QStringLiteral("13800000001"),
        nickname,
        20000,
        protocol::UserStatus::Active,
        QStringLiteral("2026-06-04T11:53:41Z"),
    };
}

protocol::StationDto fixtureStation()
{
    protocol::StationDto station;
    station.stationId = 1;
    station.name = QStringLiteral("浑南演示充电站");
    station.region = QStringLiteral("浑南区");
    station.address = QStringLiteral("浑南区创新路1号");
    station.longitude = 123.43;
    station.latitude = 41.71;
    station.priceCentsPerKwh = 135;
    station.status = protocol::StationStatus::Active;
    station.totalPileCount = 2;
    station.availablePileCount = 1;
    station.onlineRatePercent = 100.0;
    station.distanceKm = 1.39;
    station.predictedCongestion = protocol::CongestionLevel::Low;
    station.recommended = true;
    return station;
}

protocol::PileDto fixturePile()
{
    return {
        1,
        1,
        QStringLiteral("PILE-A-01"),
        protocol::PileType::Fast,
        10.0,
        protocol::PileStatus::Idle,
        4,
        14400,
    };
}

protocol::OrderDto fixtureOrder()
{
    protocol::OrderDto order;
    order.orderId = 1001;
    order.orderNo = QStringLiteral("ORD-20260902-1001");
    order.createdAt = QStringLiteral("2026-09-02T19:00:00Z");
    order.userId = 1;
    order.stationId = 1;
    order.stationName = QStringLiteral("浑南演示充电站");
    order.pileId = 1;
    order.pileCode = QStringLiteral("PILE-A-01");
    order.mode = protocol::OrderMode::Reservation;
    order.status = protocol::OrderStatus::Charging;
    order.reservedAt = QStringLiteral("2026-09-02T19:00:00Z");
    order.startedAt = QStringLiteral("2026-09-02T19:05:00Z");
    order.durationSeconds = 900;
    order.energyWh = 2500;
    order.unitPriceCentsPerKwh = 135;
    order.amountCents = 338;
    return order;
}

QJsonObject loginData(const QString &token = QStringLiteral("tcp-token"))
{
    return {
        {QStringLiteral("token"), token},
        {QStringLiteral("isNewUser"), false},
        {QStringLiteral("user"), protocol::toJson(fixtureUser())},
    };
}

}  // namespace

class TcpChargingApiTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void loginProfileAndStationRoundTrip();
    void mapsEveryOrderResponse();
    void propagatesBusinessAndTransportFailuresExactlyOnce();
    void validatesInputsBeforeSending();
};

void TcpChargingApiTests::initTestCase()
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

void TcpChargingApiTests::loginProfileAndStationRoundTrip()
{
    TestTcpServer server;
    QVERIFY(server.listen());
    server.setHandler([&server](const protocol::RequestEnvelope &request) {
        if (request.type == QString::fromLatin1(protocol::MessageType::AuthUserLogin)) {
            server.reply(request, protocol::ErrorCode::Ok, loginData(),
                         QStringLiteral("OK"), true);
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::UserProfileGet)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("user"), protocol::toJson(fixtureUser())}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::UserProfileUpdate)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("user"),
                           protocol::toJson(fixtureUser(QStringLiteral("新昵称")))}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::WalletRecharge)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("balanceCents"), 20500}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::StationList)) {
            QJsonArray items;
            items.append(protocol::toJson(fixtureStation()));
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("items"), items}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::StationDetail)) {
            QJsonArray piles;
            piles.append(protocol::toJson(fixturePile()));
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("station"), protocol::toJson(fixtureStation())},
                          {QStringLiteral("piles"), piles}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::AuthLogout)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("success"), true}});
        }
    });

    client::TcpChargingApi api(QStringLiteral("127.0.0.1"), server.port(), 2000);
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy profileSpy(&api, &client::IChargingApi::profileCompleted);
    QSignalSpy updateSpy(&api, &client::IChargingApi::profileUpdateCompleted);
    QSignalSpy rechargeSpy(&api, &client::IChargingApi::rechargeCompleted);
    QSignalSpy stationsSpy(&api, &client::IChargingApi::stationListCompleted);
    QSignalSpy detailSpy(&api, &client::IChargingApi::stationDetailCompleted);
    QSignalSpy logoutSpy(&api, &client::IChargingApi::logoutCompleted);

    const QString loginRequest = api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE_WITH_TIMEOUT(loginSpy.count(), 1, 2000);
    const auto login = qvariant_cast<client::LoginResult>(loginSpy.takeFirst().at(0));
    QCOMPARE(login.response.requestId, loginRequest);
    QVERIFY(login.ok());
    QVERIFY(login.payload.has_value());
    QCOMPARE(login.payload->token, QStringLiteral("tcp-token"));

    (void)api.getProfile();
    QTRY_COMPARE_WITH_TIMEOUT(profileSpy.count(), 1, 2000);
    QVERIFY(qvariant_cast<client::UserResult>(profileSpy.takeFirst().at(0)).ok());

    (void)api.updateNickname(QStringLiteral("新昵称"));
    QTRY_COMPARE_WITH_TIMEOUT(updateSpy.count(), 1, 2000);
    const auto update = qvariant_cast<client::UserResult>(updateSpy.takeFirst().at(0));
    QVERIFY(update.payload.has_value());
    QCOMPARE(update.payload->user.nickname, QStringLiteral("新昵称"));

    (void)api.recharge(500);
    QTRY_COMPARE_WITH_TIMEOUT(rechargeSpy.count(), 1, 2000);
    const auto recharge =
        qvariant_cast<client::RechargeResult>(rechargeSpy.takeFirst().at(0));
    QCOMPARE(recharge.payload->balanceCents, qint64{20500});

    client::StationQuery query;
    query.longitude = 123.42;
    query.latitude = 41.70;
    query.region = QStringLiteral(" 浑南区 ");
    (void)api.listStations(query);
    QTRY_COMPARE_WITH_TIMEOUT(stationsSpy.count(), 1, 2000);
    const auto stations =
        qvariant_cast<client::StationListResult>(stationsSpy.takeFirst().at(0));
    QVERIFY(stations.payload.has_value());
    QCOMPARE(stations.payload->items.size(), 1);
    QCOMPARE(stations.payload->items.first().stationId, qint64{1});

    (void)api.getStation(1);
    QTRY_COMPARE_WITH_TIMEOUT(detailSpy.count(), 1, 2000);
    const auto detail =
        qvariant_cast<client::StationDetailResult>(detailSpy.takeFirst().at(0));
    QVERIFY(detail.payload.has_value());
    QCOMPARE(detail.payload->piles.first().pileCode, QStringLiteral("PILE-A-01"));

    (void)api.logout();
    QTRY_COMPARE_WITH_TIMEOUT(logoutSpy.count(), 1, 2000);
    QVERIFY(qvariant_cast<client::LogoutResult>(logoutSpy.takeFirst().at(0)).ok());

    QCOMPARE(server.error, QString{});
    QCOMPARE(server.requests.size(), 7);
    QVERIFY(!server.requests.first().token.has_value());
    for (qsizetype index = 1; index < server.requests.size(); ++index) {
        QVERIFY(server.requests.at(index).token.has_value());
        QCOMPARE(*server.requests.at(index).token, QStringLiteral("tcp-token"));
    }
    QCOMPARE(server.requests.at(4).data.value(QStringLiteral("region")).toString(),
             QStringLiteral("浑南区"));
}

void TcpChargingApiTests::mapsEveryOrderResponse()
{
    TestTcpServer server;
    QVERIFY(server.listen());
    const protocol::OrderDto order = fixtureOrder();
    server.setHandler([&server, order](const protocol::RequestEnvelope &request) {
        if (request.type == QString::fromLatin1(protocol::MessageType::AuthUserLogin)) {
            server.reply(request, protocol::ErrorCode::Ok, loginData());
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::OrderCurrent)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::OrderList)) {
            QJsonArray items;
            items.append(protocol::toJson(order));
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("items"), items}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::OrderProgress)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("order"), protocol::toJson(order)},
                          {QStringLiteral("measuredAt"),
                           QStringLiteral("2026-09-02T19:20:00Z")}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::OrderStop)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("order"), protocol::toJson(order)},
                          {QStringLiteral("paid"), false},
                          {QStringLiteral("balanceCents"), 100},
                          {QStringLiteral("shortfallCents"), 238}});
        } else if (request.type
                   == QString::fromLatin1(protocol::MessageType::OrderPay)) {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("order"), protocol::toJson(order)},
                          {QStringLiteral("balanceCents"), 1000}});
        } else {
            server.reply(request, protocol::ErrorCode::Ok,
                         {{QStringLiteral("order"), protocol::toJson(order)}});
        }
    });

    client::TcpChargingApi api(QStringLiteral("127.0.0.1"), server.port(), 2000);
    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    (void)api.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE_WITH_TIMEOUT(loginSpy.count(), 1, 2000);

    QSignalSpy currentSpy(&api, &client::IChargingApi::currentOrderCompleted);
    (void)api.getCurrentOrder();
    QTRY_COMPARE_WITH_TIMEOUT(currentSpy.count(), 1, 2000);
    const auto current =
        qvariant_cast<client::CurrentOrderResult>(currentSpy.takeFirst().at(0));
    QVERIFY(current.payload.has_value());
    QVERIFY(!current.payload->order.has_value());

    QSignalSpy listSpy(&api, &client::IChargingApi::orderListCompleted);
    (void)api.listOrders();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 2000);
    QCOMPARE(qvariant_cast<client::OrderListResult>(listSpy.takeFirst().at(0))
                 .payload->items.size(),
             1);

    QSignalSpy reserveSpy(&api, &client::IChargingApi::reservationCompleted);
    (void)api.reserve(QStringLiteral("PILE-A-01"));
    QTRY_COMPARE_WITH_TIMEOUT(reserveSpy.count(), 1, 2000);

    QSignalSpy cancelSpy(&api, &client::IChargingApi::cancellationCompleted);
    (void)api.cancel(1001);
    QTRY_COMPARE_WITH_TIMEOUT(cancelSpy.count(), 1, 2000);

    QSignalSpy startSpy(&api, &client::IChargingApi::chargingStartCompleted);
    (void)api.startCharging(QStringLiteral("PILE-A-01"), 1001);
    QTRY_COMPARE_WITH_TIMEOUT(startSpy.count(), 1, 2000);

    QSignalSpy progressSpy(&api, &client::IChargingApi::chargingProgressCompleted);
    (void)api.getChargingProgress(1001);
    QTRY_COMPARE_WITH_TIMEOUT(progressSpy.count(), 1, 2000);
    const auto progress = qvariant_cast<client::ChargingProgressResult>(
        progressSpy.takeFirst().at(0));
    QCOMPARE(progress.payload->measuredAt,
             QStringLiteral("2026-09-02T19:20:00Z"));

    QSignalSpy stopSpy(&api, &client::IChargingApi::chargingStopCompleted);
    (void)api.stopCharging(1001);
    QTRY_COMPARE_WITH_TIMEOUT(stopSpy.count(), 1, 2000);
    const auto stop =
        qvariant_cast<client::ChargingStopResult>(stopSpy.takeFirst().at(0));
    QCOMPARE(stop.payload->shortfallCents, std::optional<qint64>{238});

    QSignalSpy paySpy(&api, &client::IChargingApi::paymentCompleted);
    (void)api.payOrder(1001);
    QTRY_COMPARE_WITH_TIMEOUT(paySpy.count(), 1, 2000);
    QCOMPARE(qvariant_cast<client::PaymentResult>(paySpy.takeFirst().at(0))
                 .payload->balanceCents,
             qint64{1000});

    QCOMPARE(server.error, QString{});
    QCOMPARE(server.requests.size(), 9);
    QCOMPARE(server.requests.at(5).data.value(
                 QStringLiteral("reservationOrderId")).toInteger(),
             qint64{1001});
}

void TcpChargingApiTests::propagatesBusinessAndTransportFailuresExactlyOnce()
{
    TestTcpServer businessServer;
    QVERIFY(businessServer.listen());
    businessServer.setHandler([&businessServer](
                                  const protocol::RequestEnvelope &request) {
        businessServer.reply(request,
                             protocol::ErrorCode::Forbidden,
                             {},
                             QStringLiteral("FORBIDDEN"));
    });
    client::TcpChargingApi businessApi(
        QStringLiteral("127.0.0.1"), businessServer.port(), 1000);
    QSignalSpy businessSpy(&businessApi, &client::IChargingApi::loginCompleted);
    (void)businessApi.loginUser(QStringLiteral("13800000004"));
    QTRY_COMPARE_WITH_TIMEOUT(businessSpy.count(), 1, 2000);
    const auto business =
        qvariant_cast<client::LoginResult>(businessSpy.first().at(0));
    QCOMPARE(business.response.code, protocol::ErrorCode::Forbidden);
    QVERIFY(!business.payload.has_value());
    QTest::qWait(100);
    QCOMPARE(businessSpy.count(), 1);

    TestTcpServer timeoutServer;
    QVERIFY(timeoutServer.listen());
    timeoutServer.setHandler([](const protocol::RequestEnvelope &) {});
    client::TcpChargingApi timeoutApi(
        QStringLiteral("127.0.0.1"), timeoutServer.port(), 80);
    QSignalSpy timeoutSpy(&timeoutApi, &client::IChargingApi::loginCompleted);
    (void)timeoutApi.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE_WITH_TIMEOUT(timeoutSpy.count(), 1, 2000);
    const auto timeout = qvariant_cast<client::LoginResult>(timeoutSpy.first().at(0));
    QCOMPARE(timeout.response.code, protocol::ErrorCode::ServiceUnavailable);
    QTest::qWait(100);
    QCOMPARE(timeoutSpy.count(), 1);

    QTcpServer portProbe;
    QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 closedPort = portProbe.serverPort();
    portProbe.close();
    client::TcpChargingApi refusedApi(QStringLiteral("127.0.0.1"), closedPort, 500);
    QSignalSpy refusedSpy(&refusedApi, &client::IChargingApi::loginCompleted);
    (void)refusedApi.loginUser(QStringLiteral("13800000001"));
    QTRY_COMPARE_WITH_TIMEOUT(refusedSpy.count(), 1, 2000);
    QCOMPARE(qvariant_cast<client::LoginResult>(refusedSpy.first().at(0))
                 .response.code,
             protocol::ErrorCode::ServiceUnavailable);
    QTest::qWait(100);
    QCOMPARE(refusedSpy.count(), 1);
}

void TcpChargingApiTests::validatesInputsBeforeSending()
{
    TestTcpServer server;
    QVERIFY(server.listen());
    client::TcpChargingApi api(QStringLiteral("127.0.0.1"), server.port(), 1000);

    QSignalSpy loginSpy(&api, &client::IChargingApi::loginCompleted);
    const QString loginRequest = api.loginUser(QStringLiteral("123"));
    QCOMPARE(loginSpy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(loginSpy.count(), 1, 1000);
    const auto login = qvariant_cast<client::LoginResult>(loginSpy.first().at(0));
    QCOMPARE(login.response.requestId, loginRequest);
    QCOMPARE(login.response.code, protocol::ErrorCode::InvalidRequest);

    QSignalSpy stationSpy(&api, &client::IChargingApi::stationDetailCompleted);
    (void)api.getStation(0);
    QTRY_COMPARE_WITH_TIMEOUT(stationSpy.count(), 1, 1000);
    QCOMPARE(qvariant_cast<client::StationDetailResult>(stationSpy.first().at(0))
                 .response.code,
             protocol::ErrorCode::InvalidRequest);

    QTest::qWait(50);
    QCOMPARE(server.requests.size(), 0);
}

QTEST_GUILESS_MAIN(TcpChargingApiTests)

#include "tcp_charging_api_tests.moc"
