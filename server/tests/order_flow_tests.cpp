#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "application/application_service.h"
#include "application/order_billing.h"
#include "application/session_store.h"
#include "persistence/in_memory_repository.h"
#include "persistence/repository.h"
#include "transport/request_router.h"
#include "transport/tcp_gateway.h"

#include "api/tcp_charging_api.h"
#include "charging/protocol/protocol_constants.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <limits>
#include <memory>

using namespace charging;
using namespace charging::server;
using namespace charging::protocol;

namespace {

class TestPile final : public IPileGateway {
public:
    bool failStart = false;
    bool failReading = false;
    mutable PileReading reading;

    bool start(qint64, const QDateTime &, QString *) const override
    {
        reading = {};
        return !failStart;
    }
    PileReading read(qint64, const QDateTime &, const QDateTime &) const override
    {
        return failReading ? PileReading{-1, -1} : reading;
    }
    PileReading stop(qint64 pileId, const QDateTime &startedAt,
                      const QDateTime &now) const override
    {
        return read(pileId, startedAt, now);
    }
    bool restart(qint64, PileStatus, QString *) const override { return true; }
};

QJsonObject pileInput(const QString &code = QStringLiteral("PILE-A-01"))
{
    return {{QStringLiteral("pileCode"), code}};
}

QJsonObject orderInput(qint64 id)
{
    return {{QStringLiteral("orderId"), static_cast<double>(id)}};
}

QJsonObject orderJson(const ResponseEnvelope &response)
{
    return response.data.value(QStringLiteral("order")).toObject();
}

qint64 orderId(const ResponseEnvelope &response)
{
    return orderJson(response).value(QStringLiteral("orderId")).toInteger();
}

struct Fixture {
    QTemporaryDir temporary;
    std::unique_ptr<IRepository> repository;
    SessionStore sessions;
    TestPile pile;
    MockPredictionProvider prediction;
    std::unique_ptr<ApplicationService> service;
    std::unique_ptr<RequestRouter> router;
    QString error;
    QString token;
    int sequence = 0;

    QString databasePath() const { return temporary.filePath(QStringLiteral("orders.db")); }

    bool sql(const QByteArray &statement)
    {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(CHARGING_SQLITE3_EXECUTABLE),
                      {QStringLiteral("-batch"), QStringLiteral("-bail"), databasePath()});
        if (!process.waitForStarted()) { error = process.errorString(); return false; }
        process.write(statement);
        process.closeWriteChannel();
        if (!process.waitForFinished(10000)) {
            process.kill();
            process.waitForFinished();
            error = QStringLiteral("sqlite3 timeout");
            return false;
        }
        error = QString::fromUtf8(process.readAll());
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }

    bool initialize(bool sqlite)
    {
        if (!temporary.isValid()) return false;
        if (sqlite) {
            for (const char *path : {CHARGING_DATABASE_MIGRATION_PATH, CHARGING_DATABASE_SEED_PATH}) {
                QFile input(QString::fromUtf8(path));
                if (!input.open(QIODevice::ReadOnly) || !sql(input.readAll())) return false;
            }
            auto storage = std::make_unique<Repository>(QUuid::createUuid().toString());
            if (!storage->open(databasePath(), &error)) return false;
            repository = std::move(storage);
        } else {
            repository = std::make_unique<InMemoryRepository>();
        }
        service = std::make_unique<ApplicationService>(repository.get(), &sessions, &pile, &prediction);
        router = std::make_unique<RequestRouter>(service.get());
        token = login(QStringLiteral("13900000901"));
        return !token.isEmpty();
    }

    ResponseEnvelope call(const char *type, const QJsonObject &data = {},
                            std::optional<QString> callerToken = std::nullopt)
    {
        RequestEnvelope request;
        request.type = QString::fromLatin1(type);
        request.requestId = QStringLiteral("order-test-%1").arg(++sequence);
        request.token = callerToken.value_or(token);
        request.data = data;
        return router->route(request);
    }

    QString login(const QString &phone)
    {
        return call(MessageType::AuthUserLogin, {{QStringLiteral("phone"), phone}})
            .data.value(QStringLiteral("token")).toString();
    }

    PileDto getPile(const QString &code = QStringLiteral("PILE-A-01")) const
    {
        for (const PileDto &item : repository->listPiles()) {
            if (item.pileCode == code) return item;
        }
        return {};
    }

    QJsonObject snapshot() const
    {
        QJsonArray users, piles, orders;
        for (const auto &item : repository->listUsers()) users.append(toJson(item));
        for (const auto &item : repository->listPiles()) piles.append(toJson(item));
        for (const auto &item : repository->listOrders()) orders.append(toJson(item));
        return {{QStringLiteral("users"), users}, {QStringLiteral("piles"), piles},
                {QStringLiteral("orders"), orders}};
    }
};

void backends()
{
    QTest::addColumn<bool>("sqlite");
    QTest::newRow("sqlite") << true;
    QTest::newRow("in-memory") << false;
}

}  // namespace

class OrderFlowTests final : public QObject {
    Q_OBJECT
private slots:
    void reservationAndCancellation_data() { backends(); }
    void reservationAndCancellation();
    void chargingAndAutomaticSettlement_data() { backends(); }
    void chargingAndAutomaticSettlement();
    void pendingPaymentReleasesPile_data() { backends(); }
    void pendingPaymentReleasesPile();
    void ownershipAndStateGuards_data() { backends(); }
    void ownershipAndStateGuards();
    void validationAndDeviceFailures_data() { backends(); }
    void validationAndDeviceFailures();
    void sqliteFailuresRollBack_data();
    void sqliteFailuresRollBack();
    void sqliteRestartPreservesOrders();
    void mockReadingsNeverRetreat();
    void integerBilling();
    void realClientTcpOrderFlow();
};

void OrderFlowTests::reservationAndCancellation()
{
    QFETCH(bool, sqlite);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    QVERIFY(f.call(MessageType::OrderCurrent).data.value(QStringLiteral("order")).isNull());
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    QVERIFY(orderId(reserved) > 0);
    QCOMPARE(orderJson(reserved).value(QStringLiteral("mode")).toString(), QStringLiteral("RESERVATION"));
    QVERIFY(orderJson(reserved).value(QStringLiteral("unitPriceCentsPerKwh")).isNull());
    QVERIFY(f.getPile().status == PileStatus::Reserved);
    QCOMPARE(orderId(f.call(MessageType::OrderCurrent)), orderId(reserved));
    QCOMPARE(f.call(MessageType::OrderReserve, pileInput(QStringLiteral("PILE-B-01"))).code,
             ErrorCode::CurrentOrderExists);
    const QString other = f.login(QStringLiteral("13900000902"));
    QCOMPARE(f.call(MessageType::OrderReserve, pileInput(), other).code, ErrorCode::PileNotAvailable);
    const auto cancelled = f.call(MessageType::OrderCancel, orderInput(orderId(reserved)));
    QCOMPARE(cancelled.code, ErrorCode::Ok);
    QCOMPARE(orderJson(cancelled).value(QStringLiteral("status")).toString(), QStringLiteral("CANCELLED"));
    QVERIFY(orderJson(cancelled).value(QStringLiteral("endedAt")).isNull());
    QVERIFY(f.getPile().status == PileStatus::Idle);
    QVERIFY(f.call(MessageType::OrderCurrent).data.value(QStringLiteral("order")).isNull());
    QCOMPARE(f.call(MessageType::OrderCancel, orderInput(orderId(reserved))).code,
             ErrorCode::IllegalOrderState);
    const auto second = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(second.code, ErrorCode::Ok);
    const auto items = f.call(MessageType::OrderList).data.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.first().toObject().value(QStringLiteral("orderId")).toInteger(), orderId(second));
    QCOMPARE(f.call(MessageType::OrderList, {}, other).data.value(QStringLiteral("items")).toArray().size(), 0);
}

void OrderFlowTests::chargingAndAutomaticSettlement()
{
    QFETCH(bool, sqlite);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    QCOMPARE(f.call(MessageType::WalletRecharge, {{QStringLiteral("amountCents"), 20000}}).code, ErrorCode::Ok);
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    auto input = pileInput();
    input.insert(QStringLiteral("reservationOrderId"), static_cast<double>(orderId(reserved)));
    const auto started = f.call(MessageType::OrderStart, input);
    QCOMPARE(started.code, ErrorCode::Ok);
    QCOMPARE(orderId(started), orderId(reserved));
    QCOMPARE(orderJson(started).value(QStringLiteral("unitPriceCentsPerKwh")).toInteger(), qint64{135});
    QVERIFY(f.getPile().status == PileStatus::Charging);
    const auto beforeCount = f.getPile().chargeCount;
    const auto beforeSeconds = f.getPile().totalChargeSeconds;
    const auto revenue = f.service->getDashboard(30).data.value(QStringLiteral("totalRevenueCents")).toInteger();
    auto station = f.repository->findStationById(1);
    QVERIFY(station.has_value());
    station->priceCentsPerKwh = 999;
    QVERIFY(f.repository->updateStation(*station));
    f.pile.reading = {1800, 5000};
    const auto progress = f.call(MessageType::OrderProgress, orderInput(orderId(started)));
    QCOMPARE(progress.code, ErrorCode::Ok);
    QCOMPARE(orderJson(progress).value(QStringLiteral("amountCents")).toInteger(), qint64{675});
    QCOMPARE(f.repository->findOrderById(orderId(started))->energyWh, qint64{0});
    QCOMPARE(orderJson(f.call(MessageType::OrderCurrent)).value(QStringLiteral("energyWh")).toInteger(), qint64{5000});
    const auto listed = f.call(MessageType::OrderList).data.value(QStringLiteral("items")).toArray();
    QCOMPARE(listed.first().toObject().value(QStringLiteral("energyWh")).toInteger(), qint64{5000});
    bool adminReadFound = false;
    for (const auto &item : f.service->listAdminOrders().data.value(QStringLiteral("items")).toArray()) {
        if (item.toObject().value(QStringLiteral("orderId")).toInteger() != orderId(started)) continue;
        QCOMPARE(item.toObject().value(QStringLiteral("energyWh")).toInteger(), qint64{5000});
        adminReadFound = true;
    }
    QVERIFY(adminReadFound);
    const auto stopped = f.call(MessageType::OrderStop, orderInput(orderId(started)));
    QCOMPARE(stopped.code, ErrorCode::Ok);
    QVERIFY(stopped.data.value(QStringLiteral("paid")).toBool());
    QVERIFY(!stopped.data.contains(QStringLiteral("shortfallCents")));
    QCOMPARE(stopped.data.value(QStringLiteral("balanceCents")).toInteger(), qint64{19325});
    QCOMPARE(orderJson(stopped).value(QStringLiteral("status")).toString(), QStringLiteral("COMPLETED"));
    QVERIFY(f.getPile().status == PileStatus::Idle);
    QCOMPARE(f.getPile().chargeCount, beforeCount + 1);
    QCOMPARE(f.getPile().totalChargeSeconds, beforeSeconds + 1800);
    QCOMPARE(f.service->getDashboard(30).data.value(QStringLiteral("totalRevenueCents")).toInteger(), revenue + 675);
    const auto state = f.snapshot();
    QCOMPARE(f.call(MessageType::OrderStop, orderInput(orderId(started))).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.call(MessageType::OrderPay, orderInput(orderId(started))).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.snapshot(), state);
}

void OrderFlowTests::pendingPaymentReleasesPile()
{
    QFETCH(bool, sqlite);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto started = f.call(MessageType::OrderStart, pileInput());
    QCOMPARE(started.code, ErrorCode::Ok);
    QCOMPARE(orderJson(started).value(QStringLiteral("mode")).toString(), QStringLiteral("DIRECT"));
    QVERIFY(orderJson(started).value(QStringLiteral("reservedAt")).isNull());
    f.pile.reading = {2000, 4000};
    const auto stopped = f.call(MessageType::OrderStop, orderInput(orderId(started)));
    QCOMPARE(stopped.code, ErrorCode::Ok);
    QVERIFY(!stopped.data.value(QStringLiteral("paid")).toBool());
    QCOMPARE(stopped.data.value(QStringLiteral("shortfallCents")).toInteger(), qint64{540});
    QCOMPARE(stopped.data.value(QStringLiteral("balanceCents")).toInteger(), qint64{0});
    QCOMPARE(orderJson(stopped).value(QStringLiteral("status")).toString(), QStringLiteral("PENDING_PAYMENT"));
    QVERIFY(f.getPile().status == PileStatus::Idle);
    QCOMPARE(orderId(f.call(MessageType::OrderCurrent)), orderId(started));
    QCOMPARE(f.call(MessageType::OrderReserve, pileInput()).code, ErrorCode::CurrentOrderExists);
    QCOMPARE(f.call(MessageType::OrderStart, pileInput()).code, ErrorCode::CurrentOrderExists);
    const auto beforePay = f.snapshot();
    QCOMPARE(f.call(MessageType::OrderPay, orderInput(orderId(started))).code, ErrorCode::InsufficientBalance);
    QCOMPARE(f.snapshot(), beforePay);
    const QString other = f.login(QStringLiteral("13900000902"));
    QCOMPARE(f.call(MessageType::OrderReserve, pileInput(), other).code, ErrorCode::Ok);
    QCOMPARE(f.call(MessageType::WalletRecharge, {{QStringLiteral("amountCents"), 1000}}).code, ErrorCode::Ok);
    const auto chargedCount = f.getPile().chargeCount;
    const auto paid = f.call(MessageType::OrderPay, orderInput(orderId(started)));
    QCOMPARE(paid.code, ErrorCode::Ok);
    QCOMPARE(paid.data.value(QStringLiteral("balanceCents")).toInteger(), qint64{460});
    QCOMPARE(orderJson(paid).value(QStringLiteral("status")).toString(), QStringLiteral("COMPLETED"));
    QVERIFY(f.getPile().status == PileStatus::Reserved);
    QCOMPARE(f.getPile().chargeCount, chargedCount);
    const auto afterPay = f.snapshot();
    QCOMPARE(f.call(MessageType::OrderPay, orderInput(orderId(started))).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.snapshot(), afterPay);
}

void OrderFlowTests::ownershipAndStateGuards()
{
    QFETCH(bool, sqlite);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    const QString other = f.login(QStringLiteral("13900000902"));
    for (const char *type : {MessageType::OrderCancel, MessageType::OrderProgress,
                             MessageType::OrderStop, MessageType::OrderPay}) {
        QCOMPARE(f.call(type, orderInput(orderId(reserved)), other).code, ErrorCode::Forbidden);
        QCOMPARE(f.call(type, orderInput(999999)).code, ErrorCode::NotFound);
    }
    auto start = pileInput();
    start.insert(QStringLiteral("reservationOrderId"), static_cast<double>(orderId(reserved)));
    QCOMPARE(f.call(MessageType::OrderStart, start, other).code, ErrorCode::Forbidden);
    start.insert(QStringLiteral("pileCode"), QStringLiteral("PILE-B-01"));
    QCOMPARE(f.call(MessageType::OrderStart, start).code, ErrorCode::IllegalOrderState);
    for (const char *type : {MessageType::OrderProgress, MessageType::OrderStop, MessageType::OrderPay}) {
        QCOMPARE(f.call(type, orderInput(orderId(reserved))).code, ErrorCode::IllegalOrderState);
    }
    start = pileInput();
    start.insert(QStringLiteral("reservationOrderId"), static_cast<double>(orderId(reserved)));
    QCOMPARE(f.call(MessageType::OrderStart, start).code, ErrorCode::Ok);
    QCOMPARE(f.call(MessageType::OrderCancel, orderInput(orderId(reserved))).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.call(MessageType::OrderStart, start).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.call(MessageType::OrderPay, orderInput(orderId(reserved))).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.call(MessageType::OrderStart, pileInput(), other).code, ErrorCode::PileNotAvailable);
}

void OrderFlowTests::validationAndDeviceFailures()
{
    QFETCH(bool, sqlite);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    for (const char *type : {MessageType::OrderCurrent, MessageType::OrderList, MessageType::OrderReserve,
                             MessageType::OrderCancel, MessageType::OrderStart, MessageType::OrderProgress,
                             MessageType::OrderStop, MessageType::OrderPay}) {
        QCOMPARE(f.call(type, {}, QStringLiteral("invalid-token")).code, ErrorCode::InvalidSession);
    }
    for (const QJsonValue &id : {QJsonValue{}, QJsonValue{-1}, QJsonValue{0}, QJsonValue{1.5},
                                QJsonValue{true}, QJsonValue{QStringLiteral("1")}, QJsonValue{1e30}}) {
        for (const char *type : {MessageType::OrderCancel, MessageType::OrderProgress,
                                 MessageType::OrderStop, MessageType::OrderPay}) {
            QCOMPARE(f.call(type, {{QStringLiteral("orderId"), id}}).code, ErrorCode::InvalidRequest);
        }
        auto input = pileInput();
        input.insert(QStringLiteral("reservationOrderId"), id);
        QCOMPARE(f.call(MessageType::OrderStart, input).code, ErrorCode::InvalidRequest);
    }
    QCOMPARE(f.call(MessageType::OrderReserve, pileInput(QString(65, QLatin1Char('x')))).code,
             ErrorCode::InvalidRequest);
    QCOMPARE(f.call(MessageType::OrderStart, pileInput(QStringLiteral(" "))).code, ErrorCode::InvalidRequest);
    QCOMPARE(f.call(MessageType::OrderStart, pileInput(QStringLiteral("UNKNOWN"))).code, ErrorCode::NotFound);
    for (const char *field : {"userId", "energyWh", "amountCents", "unitPriceCentsPerKwh", "status"}) {
        auto input = pileInput();
        input.insert(QString::fromLatin1(field), 1);
        QCOMPARE(f.call(MessageType::OrderReserve, input).code, ErrorCode::InvalidRequest);
        QCOMPARE(f.call(MessageType::OrderStart, input).code, ErrorCode::InvalidRequest);
        QCOMPARE(f.call(MessageType::OrderCurrent, input).code, ErrorCode::InvalidRequest);
        QCOMPARE(f.call(MessageType::OrderList, input).code, ErrorCode::InvalidRequest);
    }
    for (const auto status : {PileStatus::Offline, PileStatus::Fault}) {
        auto pile = f.getPile();
        pile.status = status;
        QVERIFY(f.repository->updatePile(pile));
        QCOMPARE(f.call(MessageType::OrderReserve, pileInput()).code, ErrorCode::PileNotAvailable);
        QCOMPARE(f.call(MessageType::OrderStart, pileInput()).code, ErrorCode::PileNotAvailable);
    }
    auto pile = f.getPile();
    pile.status = PileStatus::Idle;
    QVERIFY(f.repository->updatePile(pile));
    auto station = f.repository->findStationById(1);
    station->status = StationStatus::Disabled;
    QVERIFY(f.repository->updateStation(*station));
    QCOMPARE(f.call(MessageType::OrderStart, pileInput()).code, ErrorCode::PileNotAvailable);
    QCOMPARE(f.call(MessageType::OrderReserve, pileInput()).code, ErrorCode::PileNotAvailable);
    station->status = StationStatus::Active;
    QVERIFY(f.repository->updateStation(*station));
    const auto before = f.snapshot();
    f.pile.failStart = true;
    QCOMPARE(f.call(MessageType::OrderStart, pileInput()).code, ErrorCode::InternalError);
    QCOMPARE(f.snapshot(), before);
    f.pile.failStart = false;
    const auto started = f.call(MessageType::OrderStart, pileInput());
    QCOMPARE(started.code, ErrorCode::Ok);
    f.pile.failReading = true;
    const auto charging = f.snapshot();
    QCOMPARE(f.call(MessageType::OrderProgress, orderInput(orderId(started))).code, ErrorCode::InternalError);
    QCOMPARE(f.call(MessageType::OrderStop, orderInput(orderId(started))).code, ErrorCode::InternalError);
    QCOMPARE(f.snapshot(), charging);
    auto user = f.repository->findUserByPhone(QStringLiteral("13900000901"));
    user->status = UserStatus::Frozen;
    QVERIFY(f.repository->updateUser(*user));
    for (const char *type : {MessageType::OrderCurrent, MessageType::OrderList, MessageType::OrderReserve,
                             MessageType::OrderCancel, MessageType::OrderStart, MessageType::OrderProgress,
                             MessageType::OrderStop, MessageType::OrderPay}) {
        QCOMPARE(f.call(type, orderInput(orderId(started))).code, ErrorCode::Forbidden);
    }
}

void OrderFlowTests::sqliteFailuresRollBack_data()
{
    QTest::addColumn<QString>("operation");
    for (const char *name : {"reserve", "direct", "start", "cancel", "stop", "pay", "commit"}) {
        QTest::newRow(name) << QString::fromLatin1(name);
    }
}

void OrderFlowTests::sqliteFailuresRollBack()
{
    QFETCH(QString, operation);
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));
    qint64 id = 0;
    if (operation == QStringLiteral("start") || operation == QStringLiteral("cancel")) {
        const auto reserved = f.call(MessageType::OrderReserve, pileInput());
        QCOMPARE(reserved.code, ErrorCode::Ok);
        id = orderId(reserved);
    } else if (operation != QStringLiteral("reserve") && operation != QStringLiteral("direct")) {
        const auto started = f.call(MessageType::OrderStart, pileInput());
        QCOMPARE(started.code, ErrorCode::Ok);
        id = orderId(started);
        f.pile.reading = {2000, 4000};
        if (operation == QStringLiteral("pay")) {
            QCOMPARE(f.call(MessageType::OrderStop, orderInput(id)).code, ErrorCode::Ok);
        }
        QCOMPARE(f.call(MessageType::WalletRecharge, {{QStringLiteral("amountCents"), 1000}}).code, ErrorCode::Ok);
    }
    QByteArray trigger;
    if (operation == QStringLiteral("reserve") || operation == QStringLiteral("direct")) {
        trigger = "CREATE TRIGGER reject_order BEFORE INSERT ON charging_orders "
                  "BEGIN SELECT RAISE(ABORT, 'injected order insert failure'); END;";
    } else if (operation == QStringLiteral("commit")) {
        trigger = "PRAGMA foreign_keys=ON; CREATE TABLE deferred_failure (user_id INTEGER "
                  "REFERENCES users(user_id) DEFERRABLE INITIALLY DEFERRED); "
                  "CREATE TRIGGER reject_commit AFTER UPDATE ON charging_orders "
                  "BEGIN INSERT INTO deferred_failure VALUES (999999); END;";
    } else {
        trigger = "CREATE TRIGGER reject_order BEFORE UPDATE ON charging_orders "
                  "BEGIN SELECT RAISE(ABORT, 'injected order update failure'); END;";
    }
    QVERIFY2(f.sql(trigger), qPrintable(f.error));
    const auto before = f.snapshot();
    ResponseEnvelope result;
    if (operation == QStringLiteral("reserve")) result = f.call(MessageType::OrderReserve, pileInput());
    else if (operation == QStringLiteral("direct")) result = f.call(MessageType::OrderStart, pileInput());
    else if (operation == QStringLiteral("start")) {
        auto input = pileInput();
        input.insert(QStringLiteral("reservationOrderId"), static_cast<double>(id));
        result = f.call(MessageType::OrderStart, input);
    } else if (operation == QStringLiteral("cancel")) result = f.call(MessageType::OrderCancel, orderInput(id));
    else if (operation == QStringLiteral("pay")) result = f.call(MessageType::OrderPay, orderInput(id));
    else result = f.call(MessageType::OrderStop, orderInput(id));
    QCOMPARE(result.code, ErrorCode::InternalError);
    QCOMPARE(result.message, QStringLiteral("INTERNAL_ERROR"));
    QVERIFY(result.data.isEmpty());
    QCOMPARE(f.snapshot(), before);
    QVERIFY(f.repository->beginTransaction());
    f.repository->rollbackTransaction();
}

void OrderFlowTests::sqliteRestartPreservesOrders()
{
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));
    const auto started = f.call(MessageType::OrderStart, pileInput());
    QCOMPARE(started.code, ErrorCode::Ok);
    const QString oldToken = f.token;
    f.router.reset();
    f.service.reset();
    f.repository.reset();
    Repository reopened(QUuid::createUuid().toString());
    QVERIFY2(reopened.open(f.databasePath(), &f.error), qPrintable(f.error));
    SessionStore newSessions;
    MockPile realMock;
    ApplicationService service(&reopened, &newSessions, &realMock, &f.prediction);
    QCOMPARE(service.getCurrentOrder(oldToken).code, ErrorCode::InvalidSession);
    const auto login = service.loginUser({{QStringLiteral("phone"), QStringLiteral("13900000901")}});
    QCOMPARE(login.code, ErrorCode::Ok);
    const QString token = login.data.value(QStringLiteral("token")).toString();
    QCOMPARE(service.getCurrentOrder(token).data.value(QStringLiteral("order")).toObject()
                 .value(QStringLiteral("orderId")).toInteger(), orderId(started));
    QCOMPARE(service.stopOrder(token, orderInput(orderId(started))).code, ErrorCode::Ok);
    const auto stored = reopened.findOrderById(orderId(started));
    QVERIFY(stored.has_value());
    QVERIFY(stored->status == OrderStatus::Completed || stored->status == OrderStatus::PendingPayment);
    QCOMPARE(stored->unitPriceCentsPerKwh.value(), qint64{135});
}

void OrderFlowTests::mockReadingsNeverRetreat()
{
    MockPile pile;
    const auto start = QDateTime::fromString(QStringLiteral("2026-09-05T00:00:00Z"), Qt::ISODate);
    QVERIFY(pile.start(1, start));
    QCOMPARE(pile.read(1, start, start.addSecs(100)).energyWh, qint64{200});
    QCOMPARE(pile.read(1, start, start.addSecs(90)).energyWh, qint64{200});
    QCOMPARE(pile.stop(1, start, start.addSecs(80)).durationSeconds, qint64{100});
    QCOMPARE(pile.stop(1, start, start.addSecs(101)).energyWh, qint64{202});
    QVERIFY(pile.start(1, start.addSecs(200)));
    QCOMPARE(pile.read(1, start.addSecs(200), start.addSecs(201)).energyWh, qint64{2});
    QVERIFY(!pile.restart(1, PileStatus::Fault));
}

void OrderFlowTests::integerBilling()
{
    QCOMPARE(orderAmountCents(2500, 135).value(), qint64{338});
    QCOMPARE(orderAmountCents(5000, 135).value(), qint64{675});
    QCOMPARE(orderAmountCents(0, 135).value(), qint64{0});
    QVERIFY(!orderAmountCents(-1, 135).has_value());
    QVERIFY(!orderAmountCents(100, 0).has_value());
    QVERIFY(!orderAmountCents(std::numeric_limits<qint64>::max(), 135).has_value());
}

void OrderFlowTests::realClientTcpOrderFlow()
{
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));
    TcpGateway gateway(f.router.get());
    QVERIFY2(gateway.start(0, QHostAddress::LocalHost, &f.error), qPrintable(f.error));
    client::TcpChargingApi api(QStringLiteral("127.0.0.1"), gateway.serverPort());
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy current(&api, &client::IChargingApi::currentOrderCompleted);
    QSignalSpy reserve(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy cancel(&api, &client::IChargingApi::cancellationCompleted);
    QSignalSpy start(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy progress(&api, &client::IChargingApi::chargingProgressCompleted);
    QSignalSpy stop(&api, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy recharge(&api, &client::IChargingApi::rechargeCompleted);
    QSignalSpy pay(&api, &client::IChargingApi::paymentCompleted);
    QSignalSpy list(&api, &client::IChargingApi::orderListCompleted);

    const auto loginRequest = api.loginUser(QStringLiteral("13900000903"));
    QTRY_COMPARE(login.size(), 1);
    const auto loggedIn = qvariant_cast<client::LoginResult>(login.takeFirst().at(0));
    QVERIFY(loggedIn.ok() && loggedIn.payload.has_value());
    QCOMPARE(loggedIn.response.requestId, loginRequest);
    QVERIFY(!api.getCurrentOrder().isEmpty());
    QTRY_COMPARE(current.size(), 1);
    const auto empty = qvariant_cast<client::CurrentOrderResult>(current.takeFirst().at(0));
    QVERIFY(empty.ok() && empty.payload.has_value() && !empty.payload->order.has_value());
    QVERIFY(!api.reserve(QStringLiteral("PILE-A-01")).isEmpty());
    QTRY_COMPARE(reserve.size(), 1);
    const auto reserved = qvariant_cast<client::OrderResult>(reserve.takeFirst().at(0));
    QVERIFY(reserved.ok() && reserved.payload.has_value());
    QVERIFY(!api.cancel(reserved.payload->order.orderId).isEmpty());
    QTRY_COMPARE(cancel.size(), 1);
    QVERIFY(qvariant_cast<client::OrderResult>(cancel.takeFirst().at(0)).ok());
    QVERIFY(!api.reserve(QStringLiteral("PILE-A-01")).isEmpty());
    QTRY_COMPARE(reserve.size(), 1);
    const auto next = qvariant_cast<client::OrderResult>(reserve.takeFirst().at(0));
    QVERIFY(next.ok() && next.payload.has_value());
    const qint64 id = next.payload->order.orderId;
    QVERIFY(!api.startCharging(QStringLiteral("PILE-A-01"), id).isEmpty());
    QTRY_COMPARE(start.size(), 1);
    QVERIFY(qvariant_cast<client::OrderResult>(start.takeFirst().at(0)).ok());
    f.pile.reading = {1800, 5000};
    QVERIFY(!api.getChargingProgress(id).isEmpty());
    QTRY_COMPARE(progress.size(), 1);
    const auto measured = qvariant_cast<client::ChargingProgressResult>(progress.takeFirst().at(0));
    QVERIFY(measured.ok() && measured.payload.has_value());
    QCOMPARE(measured.payload->order.amountCents, qint64{675});
    QVERIFY(!api.stopCharging(id).isEmpty());
    QTRY_COMPARE(stop.size(), 1);
    const auto stopped = qvariant_cast<client::ChargingStopResult>(stop.takeFirst().at(0));
    QVERIFY(stopped.ok() && stopped.payload.has_value());
    QVERIFY(!stopped.payload->paid);
    QCOMPARE(stopped.payload->shortfallCents.value(), qint64{675});
    QVERIFY(!api.payOrder(id).isEmpty());
    QTRY_COMPARE(pay.size(), 1);
    QCOMPARE(qvariant_cast<client::PaymentResult>(pay.takeFirst().at(0)).response.code,
             ErrorCode::InsufficientBalance);
    QVERIFY(!api.recharge(1000).isEmpty());
    QTRY_COMPARE(recharge.size(), 1);
    QVERIFY(qvariant_cast<client::RechargeResult>(recharge.takeFirst().at(0)).ok());
    QVERIFY(!api.payOrder(id).isEmpty());
    QTRY_COMPARE(pay.size(), 1);
    const auto paid = qvariant_cast<client::PaymentResult>(pay.takeFirst().at(0));
    QVERIFY(paid.ok() && paid.payload.has_value());
    QCOMPARE(paid.payload->balanceCents, qint64{325});
    QVERIFY(!api.listOrders().isEmpty());
    QTRY_COMPARE(list.size(), 1);
    const auto history = qvariant_cast<client::OrderListResult>(list.takeFirst().at(0));
    QVERIFY(history.ok() && history.payload.has_value());
    QCOMPARE(history.payload->items.size(), 2);
    QCOMPARE(history.payload->items.first().orderId, id);
    QVERIFY(history.payload->items.first().status == OrderStatus::Completed);
}

QTEST_GUILESS_MAIN(OrderFlowTests)
#include "order_flow_tests.moc"
