#include "api/tcp_charging_api.h"
#include "charging/protocol/envelope.h"
#include "charging/protocol/frame_codec.h"
#include "charging/protocol/protocol_constants.h"
#include "local/mock_map_service.h"
#include "ui/station_map_view.h"
#include "vehicle/vehicle_main_window.h"

#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <memory>
#include <vector>

using namespace charging;

namespace {

class SharedOrderServer final : public QObject {
public:
    SharedOrderServer()
    {
        station_.stationId = 1;
        station_.name = QStringLiteral("浑南悦充站");
        station_.region = QStringLiteral("浑南区");
        station_.address = QStringLiteral("沈阳市浑南区示范路1号");
        station_.longitude = 123.42; station_.latitude = 41.70;
        station_.priceCentsPerKwh = 135; station_.totalPileCount = 1;
        station_.availablePileCount = 1; station_.onlineRatePercent = 100;
        station_.distanceKm = 1.0; station_.recommended = true;
        pile_.pileId = 1; pile_.stationId = 1; pile_.pileCode = QStringLiteral("PILE-A-01");
        pile_.pileType = protocol::PileType::Fast; pile_.ratedPowerKw = 60;
        user_.userId = 1; user_.phone = QStringLiteral("13800000001");
        user_.nickname = QStringLiteral("双端用户"); user_.balanceCents = 10000;
        user_.createdAt = QStringLiteral("2026-09-08T00:00:00Z");
        ticket_.ticketId = 7; ticket_.userId = user_.userId;
        ticket_.submissionId = QStringLiteral("00000000-0000-4000-8000-000000000007");
        ticket_.title = QStringLiteral("充电枪连接异常");
        ticket_.summary = QStringLiteral("同一账号提交的演示工单");
        ticket_.sourceModel = QStringLiteral("manual");
        ticket_.status = protocol::TicketStatus::Resolved;
        ticket_.reply = QStringLiteral("管理员已确认并安排检修");
        ticket_.createdAt = QStringLiteral("2026-09-08T00:05:00Z");
        ticket_.updatedAt = QStringLiteral("2026-09-08T00:06:00Z");
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (server_.hasPendingConnections()) {
                auto connection = std::make_unique<Connection>();
                connection->socket = server_.nextPendingConnection();
                Connection *raw = connection.get();
                connect(raw->socket, &QTcpSocket::readyRead, this, [this, raw] { read(raw); });
                connections_.push_back(std::move(connection));
            }
        });
    }

    bool listen() { return server_.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return server_.serverPort(); }
    qint64 orderId() const { return order_ ? order_->orderId : 0; }
    void autoComplete(bool paid)
    {
        if (!order_) return;
        order_->status = paid ? protocol::OrderStatus::Completed
                              : protocol::OrderStatus::PendingPayment;
        order_->durationSeconds = 180; order_->energyWh = 3000; order_->amountCents = 20000;
        order_->endedAt = QStringLiteral("2026-09-08T00:03:01Z");
        if (paid) order_->paidAt = QStringLiteral("2026-09-08T00:03:01Z");
        pile_.status = protocol::PileStatus::Idle; station_.availablePileCount = 1;
    }

private:
    struct Connection { QPointer<QTcpSocket> socket; protocol::FrameDecoder decoder; };

    void reply(Connection *connection, const protocol::RequestEnvelope &request, int code,
               const QJsonObject &data, const QString &message = QStringLiteral("OK"))
    {
        protocol::ResponseEnvelope response;
        response.type = request.type; response.requestId = request.requestId;
        response.code = code; response.message = message; response.data = data;
        connection->socket->write(protocol::encodeFrame(response.toJson()));
    }

    void read(Connection *connection)
    {
        const auto decoded = connection->decoder.append(connection->socket->readAll());
        for (const auto &json : decoded.messages) {
            protocol::RequestEnvelope request; QString error;
            if (!protocol::RequestEnvelope::fromJson(json, &request, &error)) continue;
            handle(connection, request);
        }
    }

    void handle(Connection *connection, const protocol::RequestEnvelope &request)
    {
        using namespace protocol::MessageType;
        if (request.type == AuthUserLogin) {
            reply(connection, request, 0, {{QStringLiteral("token"), QStringLiteral("shared-token")},
                {QStringLiteral("isNewUser"), false}, {QStringLiteral("user"), protocol::toJson(user_)}});
        } else if (request.type == UserProfileGet) {
            reply(connection, request, 0, {{QStringLiteral("user"), protocol::toJson(user_)}});
        } else if (request.type == UserProfileUpdate) {
            user_.nickname = request.data.value(QStringLiteral("nickname")).toString();
            reply(connection, request, 0, {{QStringLiteral("user"), protocol::toJson(user_)}});
        } else if (request.type == WalletRecharge) {
            user_.balanceCents += request.data.value(QStringLiteral("amountCents")).toInteger();
            reply(connection, request, 0, {{QStringLiteral("balanceCents"), user_.balanceCents}});
        } else if (request.type == StationList) {
            reply(connection, request, 0, {{QStringLiteral("items"), QJsonArray{protocol::toJson(station_)}}});
        } else if (request.type == StationDetail) {
            auto station = station_; station.availablePileCount = pile_.status == protocol::PileStatus::Idle ? 1 : 0;
            reply(connection, request, 0, {{QStringLiteral("station"), protocol::toJson(station)},
                {QStringLiteral("piles"), QJsonArray{protocol::toJson(pile_)}}});
        } else if (request.type == OrderCurrent) {
            QJsonValue current = QJsonValue::Null;
            if (order_ && (order_->status == protocol::OrderStatus::Reserved
                           || order_->status == protocol::OrderStatus::Charging
                           || order_->status == protocol::OrderStatus::PendingPayment))
                current = protocol::toJson(*order_);
            reply(connection, request, 0, {{QStringLiteral("order"), current}});
        } else if (request.type == OrderStart) {
            if (order_ && order_->status == protocol::OrderStatus::Charging) {
                reply(connection, request, protocol::ErrorCode::CurrentOrderExists, {}, QStringLiteral("CURRENT_ORDER_EXISTS"));
                return;
            }
            protocol::OrderDto order;
            order.orderId = 91; order.orderNo = QStringLiteral("V-DEMO-91");
            order.createdAt = QStringLiteral("2026-09-08T00:00:00Z"); order.userId = 1;
            order.stationId = 1; order.stationName = station_.name; order.pileId = 1;
            order.pileCode = pile_.pileCode; order.status = protocol::OrderStatus::Charging;
            order.startedAt = QStringLiteral("2026-09-08T00:00:01Z"); order.unitPriceCentsPerKwh = 135;
            order_ = order; pile_.status = protocol::PileStatus::Charging; station_.availablePileCount = 0;
            reply(connection, request, 0, {{QStringLiteral("order"), protocol::toJson(*order_)}});
        } else if (request.type == OrderProgress) {
            if (!order_ || order_->status != protocol::OrderStatus::Charging) {
                reply(connection, request, protocol::ErrorCode::IllegalOrderState, {}, QStringLiteral("ILLEGAL_ORDER_STATE"));
                return;
            }
            order_->durationSeconds += 1; order_->energyWh += 16; order_->amountCents += 2;
            reply(connection, request, 0, {{QStringLiteral("order"), protocol::toJson(*order_)},
                {QStringLiteral("measuredAt"), QStringLiteral("2026-09-08T00:00:02Z")}});
        } else if (request.type == OrderStop) {
            if (!order_ || order_->status != protocol::OrderStatus::Charging) {
                reply(connection, request, protocol::ErrorCode::IllegalOrderState, {}, QStringLiteral("ILLEGAL_ORDER_STATE"));
                return;
            }
            order_->status = protocol::OrderStatus::Completed;
            order_->endedAt = QStringLiteral("2026-09-08T00:00:03Z");
            order_->paidAt = QStringLiteral("2026-09-08T00:00:03Z");
            pile_.status = protocol::PileStatus::Idle; station_.availablePileCount = 1;
            reply(connection, request, 0, {{QStringLiteral("order"), protocol::toJson(*order_)},
                {QStringLiteral("paid"), true}, {QStringLiteral("balanceCents"), 9998}});
        } else if (request.type == OrderList) {
            QJsonArray items; if (order_) items.append(protocol::toJson(*order_));
            reply(connection, request, 0, {{QStringLiteral("items"), items}});
        } else if (request.type == OrderPay) {
            if (!order_ || order_->status != protocol::OrderStatus::PendingPayment) {
                reply(connection, request, protocol::ErrorCode::IllegalOrderState, {}, QStringLiteral("ILLEGAL_ORDER_STATE"));
            } else if (user_.balanceCents < order_->amountCents) {
                reply(connection, request, protocol::ErrorCode::InsufficientBalance, {}, QStringLiteral("INSUFFICIENT_BALANCE"));
            } else {
                user_.balanceCents -= order_->amountCents;
                order_->status = protocol::OrderStatus::Completed;
                order_->paidAt = QStringLiteral("2026-09-08T00:04:00Z");
                reply(connection, request, 0, {{QStringLiteral("order"), protocol::toJson(*order_)},
                    {QStringLiteral("balanceCents"), user_.balanceCents}});
            }
        } else if (request.type == SupportTicketList) {
            reply(connection, request, 0, {{QStringLiteral("items"),
                QJsonArray{protocol::toJson(ticket_)}}, {QStringLiteral("hasMore"), false}});
        } else if (request.type == SupportTicketDetail) {
            reply(connection, request, 0, {{QStringLiteral("ticket"), protocol::toJson(ticket_)}});
        } else if (request.type == AuthLogout) {
            reply(connection, request, 0, {{QStringLiteral("success"), true}});
        } else {
            reply(connection, request, protocol::ErrorCode::InvalidRequest, {}, QStringLiteral("INVALID_REQUEST"));
        }
    }

    QTcpServer server_;
    std::vector<std::unique_ptr<Connection>> connections_;
    protocol::UserDto user_;
    protocol::StationDto station_;
    protocol::PileDto pile_;
    protocol::SupportTicketDto ticket_;
    std::optional<protocol::OrderDto> order_;
};

template<typename T> T *child(QObject &parent, const char *name)
{
    auto *value = parent.findChild<T *>(QString::fromLatin1(name)); Q_ASSERT(value); return value;
}

void login(client::VehicleMainWindow &window)
{
    child<QLineEdit>(window, "phoneInput")->setText(QStringLiteral("13800000001"));
    child<QLineEdit>(window, "verificationCodeInput")->setText(QStringLiteral("123456"));
    child<QPushButton>(window, "loginButton")->click();
    QTRY_VERIFY(child<QTabWidget>(window, "vehicleNavigation")->isVisible());
}

}  // namespace

class VehicleTcpSyncTests final : public QObject {
    Q_OBJECT
private slots:
    void twoClientsShareOrderAndConcurrentStopIsIdempotent();
    void automaticEndProfileAndBalanceRefreshAcrossClients();
};

void VehicleTcpSyncTests::twoClientsShareOrderAndConcurrentStopIsIdempotent()
{
    SharedOrderServer server; QVERIFY(server.listen());
    client::TcpChargingApi api1(QStringLiteral("127.0.0.1"), server.port(), 2000);
    client::TcpChargingApi api2(QStringLiteral("127.0.0.1"), server.port(), 2000);
    client::MockMapService map1, map2;
    client::VehicleMainWindow first(api1, map1), second(api2, map2);
    first.resize(1024, 600); second.resize(1024, 600); first.show(); second.show();
    login(first); login(second);
    auto *stationMap = child<client::StationMapView>(first, "vehicleStationMap");
    QVERIFY(QMetaObject::invokeMethod(stationMap, "stationSelected", Q_ARG(qint64, 1)));
    QTRY_VERIFY(first.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01")) != nullptr);
    first.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01"))->click();
    child<QPushButton>(first, "vehicleStartButton")->click();
    QTRY_VERIFY(child<QPushButton>(first, "vehicleStopButton")->isVisible());

    auto *secondTabs = child<QTabWidget>(second, "vehicleNavigation");
    secondTabs->setCurrentIndex(1);
    QTRY_VERIFY(child<QPushButton>(second, "vehicleStopButton")->isVisible());
    QCOMPARE(server.orderId(), 91);

    QSignalSpy stop1(&api1, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy stop2(&api2, &client::IChargingApi::chargingStopCompleted);
    const QString request1 = api1.stopCharging(server.orderId());
    const QString request2 = api2.stopCharging(server.orderId());
    QVERIFY(!request1.isEmpty() && !request2.isEmpty());
    QTRY_COMPARE(stop1.count(), 1); QTRY_COMPARE(stop2.count(), 1);
    const auto result1 = qvariant_cast<client::ChargingStopResult>(stop1.takeFirst().at(0));
    const auto result2 = qvariant_cast<client::ChargingStopResult>(stop2.takeFirst().at(0));
    const QList<int> codes{result1.response.code, result2.response.code};
    QVERIFY(codes.contains(protocol::ErrorCode::Ok));
    QVERIFY(codes.contains(protocol::ErrorCode::IllegalOrderState));

    child<QPushButton>(first, "vehicleRefreshButton")->click();
    child<QPushButton>(second, "vehicleRefreshButton")->click();
    QTRY_COMPARE(child<QLabel>(first, "vehicleChargingState")->text(), QStringLiteral("已完成"));
    QTRY_COMPARE(child<QLabel>(second, "vehicleChargingState")->text(), QStringLiteral("已完成"));
}

void VehicleTcpSyncTests::automaticEndProfileAndBalanceRefreshAcrossClients()
{
    SharedOrderServer server; QVERIFY(server.listen());
    client::TcpChargingApi api1(QStringLiteral("127.0.0.1"), server.port(), 2000);
    client::TcpChargingApi api2(QStringLiteral("127.0.0.1"), server.port(), 2000);
    client::MockMapService map1, map2;
    client::VehicleMainWindow first(api1, map1), second(api2, map2);
    first.resize(1024, 600); second.resize(1024, 600); first.show(); second.show();
    login(first); login(second);
    auto *stationMap = child<client::StationMapView>(first, "vehicleStationMap");
    QVERIFY(QMetaObject::invokeMethod(stationMap, "stationSelected", Q_ARG(qint64, 1)));
    QTRY_VERIFY(first.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01")) != nullptr);
    first.findChild<QPushButton *>(QStringLiteral("vehicleCharge_PILE-A-01"))->click();
    child<QPushButton>(first, "vehicleStartButton")->click();
    QTRY_VERIFY(child<QPushButton>(first, "vehicleStopButton")->isVisible());
    child<QTabWidget>(second, "vehicleNavigation")->setCurrentIndex(1);
    QTRY_VERIFY(child<QPushButton>(second, "vehicleStopButton")->isVisible());

    server.autoComplete(false);
    child<QPushButton>(first, "vehicleRefreshButton")->click();
    child<QPushButton>(second, "vehicleRefreshButton")->click();
    QTRY_COMPARE(child<QLabel>(first, "vehicleChargingState")->text(), QStringLiteral("待支付"));
    QTRY_COMPARE(child<QLabel>(second, "vehicleChargingState")->text(), QStringLiteral("待支付"));

    auto *firstTabs = child<QTabWidget>(first, "vehicleNavigation");
    auto *secondTabs = child<QTabWidget>(second, "vehicleNavigation");
    firstTabs->setCurrentIndex(2); secondTabs->setCurrentIndex(2);
    QTRY_COMPARE(child<QLabel>(first, "vehicleProfileNickname")->text(), QStringLiteral("双端用户"));
    QTRY_VERIFY(child<QPushButton>(first, "vehicleSaveNicknameButton")->isEnabled());
    child<QLineEdit>(first, "vehicleNicknameInput")->setText(QStringLiteral("车机昵称"));
    child<QPushButton>(first, "vehicleSaveNicknameButton")->click();
    QTRY_COMPARE(child<QLabel>(first, "vehicleProfileNickname")->text(), QStringLiteral("车机昵称"));
    child<QPushButton>(second, "vehicleRefreshButton")->click();
    QTRY_COMPARE(child<QLabel>(second, "vehicleProfileNickname")->text(), QStringLiteral("车机昵称"));

    child<QLineEdit>(first, "vehicleRechargeInput")->setText(QStringLiteral("100.00"));
    QTRY_VERIFY(child<QPushButton>(first, "vehicleRechargeButton")->isEnabled());
    child<QPushButton>(first, "vehicleRechargeButton")->click();
    QTRY_COMPARE(child<QLabel>(first, "vehicleProfileBalance")->text(), QStringLiteral("余额 ¥0.00"));
    secondTabs->setCurrentIndex(1);
    child<QPushButton>(second, "vehicleRefreshButton")->click();
    QTRY_COMPARE(child<QLabel>(second, "vehicleChargingState")->text(), QStringLiteral("已完成"));
    secondTabs->setCurrentIndex(2);
    child<QPushButton>(second, "vehicleRefreshButton")->click();
    QTRY_COMPARE(child<QLabel>(second, "vehicleProfileBalance")->text(), QStringLiteral("余额 ¥0.00"));

    child<QPushButton>(first, "vehicleTicketsButton")->click();
    child<QPushButton>(second, "vehicleTicketsButton")->click();
    QTRY_COMPARE(child<QListWidget>(first, "myTickets")->count(), 1);
    QTRY_COMPARE(child<QListWidget>(second, "myTickets")->count(), 1);
    QCOMPARE(child<QListWidget>(first, "myTickets")->item(0)->text(),
             child<QListWidget>(second, "myTickets")->item(0)->text());
    QTRY_VERIFY(child<QPlainTextEdit>(first, "myTicketDetail")->toPlainText()
                    .contains(QStringLiteral("管理员已确认并安排检修")));
    QTRY_COMPARE(child<QPlainTextEdit>(first, "myTicketDetail")->toPlainText(),
                 child<QPlainTextEdit>(second, "myTicketDetail")->toPlainText());
}

QTEST_MAIN(VehicleTcpSyncTests)
#include "vehicle_tcp_sync_tests.moc"
