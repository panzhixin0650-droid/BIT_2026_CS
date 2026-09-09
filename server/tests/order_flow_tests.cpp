// 服务端订单流程集成测试：预约、充电、结算与故障回滚
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

// 测试用假电桩，可注入启动失败或读数失败
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

// 以下是构造请求参数与解析订单响应的小工具
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

// 测试夹具：临时数据库、应用服务、路由与可控当前时间
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
    // Existing settlement examples are off-peak regardless of the test runner clock.
    QDateTime now{QDateTime::currentDateTimeUtc().date(), QTime(4, 0), Qt::UTC};

    QString databasePath() const { return temporary.filePath(QStringLiteral("orders.db")); }

    // 借助 sqlite3 命令行执行建表或注入触发器的语句
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

    // 按参数选择 SQLite 或内存仓储，并登录测试用户
    bool initialize(bool sqlite)
    {
        if (!temporary.isValid()) return false;
        if (sqlite) {
            for (const char *path : {CHARGING_DATABASE_MIGRATION_PATH,
                                     CHARGING_DATABASE_SEED_PATH,
                                     CHARGING_TICKET_MIGRATION_PATH,
                                     CHARGING_ADMIN_MIGRATION_PATH}) {
                QFile input(QString::fromUtf8(path));
                if (!input.open(QIODevice::ReadOnly) || !sql(input.readAll())) return false;
            }
            auto storage = std::make_unique<Repository>(QUuid::createUuid().toString());
            if (!storage->open(databasePath(), &error)) return false;
            repository = std::move(storage);
        } else {
            repository = std::make_unique<InMemoryRepository>();
        }
        service = std::make_unique<ApplicationService>(repository.get(), &sessions, &pile,
                                                       &prediction, nullptr, [this] { return now; });
        router = std::make_unique<RequestRouter>(service.get());
        token = login(QStringLiteral("13900000901"));
        return !token.isEmpty();
    }

    // 组装请求信封交给路由，模拟一次客户端调用
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

    // 抓取用户、电桩、订单快照，用于比对失败后是否回滚
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

// 数据行：同一用例在两种存储后端各跑一遍
void backends()
{
    QTest::addColumn<bool>("sqlite");
    QTest::newRow("sqlite") << true;
    QTest::newRow("in-memory") << false;
}

}  // namespace

// 订单流程测试类，槽函数即各条用例
class OrderFlowTests final : public QObject {
    Q_OBJECT
private slots:
    void reservationAndCancellation_data() { backends(); }
    void reservationAndCancellation();
    void reservationDeadline_data();
    void reservationDeadline();
    void reservationExpiryBeforeRequests_data();
    void reservationExpiryBeforeRequests();
    void reservationTimerAndRestart();
    void reservationExpiryRollback_data();
    void reservationExpiryRollback();
    void startedReservationNeverExpires_data() { backends(); }
    void startedReservationNeverExpires();
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
    void sqliteAdminAccountsAndPasswordUpgrade();
    void mockReadingsNeverRetreat();
    void integerBilling();
    void peakQuotesAndStart_data();
    void peakQuotesAndStart();
    void peakSnapshotSurvivesSettlement_data();
    void peakSnapshotSurvivesSettlement();
    void demoDeadlineStopsOnce_data() { backends(); }
    void demoDeadlineStopsOnce();
    void demoDeadlineHandlesDebtAfterRestart_data() { backends(); }
    void demoDeadlineHandlesDebtAfterRestart();
    void realClientTcpOrderFlow();
    void realClientTcpReservationExpiry();
};

// 从 JSON 夹具读取预约到期的边界时间用例
void OrderFlowTests::reservationDeadline_data()
{
    QTest::addColumn<bool>("sqlite");
    QTest::addColumn<QDateTime>("reservedAt");
    QTest::addColumn<QDateTime>("now");
    QTest::addColumn<bool>("expired");
    QFile file(QString::fromUtf8(CHARGING_RESERVATION_FIXTURE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto fixture = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(fixture.value("durationSeconds").toInt(), DemoReservationDurationSeconds);
    const auto cases = fixture.value("cases").toArray();
    QVERIFY(!cases.isEmpty());
    for (bool sqlite : {false, true}) for (const auto &value : cases) {
        const auto row = value.toObject();
        QTest::newRow(qPrintable(row.value("name").toString() + (sqlite ? "-sqlite" : "-memory")))
            << sqlite << QDateTime::fromString(row.value("reservedAt").toString(), Qt::ISODate)
            << QDateTime::fromString(row.value("now").toString(), Qt::ISODate)
            << row.value("expired").toBool();
    }
}

// 到期预约调用开始充电应被拒绝并置为已取消
void OrderFlowTests::reservationDeadline()
{
    QFETCH(bool, sqlite);
    QFETCH(QDateTime, reservedAt);
    QFETCH(QDateTime, now);
    QFETCH(bool, expired);
    Fixture f;
    f.now = reservedAt;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    const auto id = orderId(reserved);
    const auto userId = orderJson(reserved).value("userId").toInteger();
    const auto balance = f.repository->findUserById(userId)->balanceCents;
    f.now = now;
    auto input = pileInput();
    input.insert("reservationOrderId", id);
    // Do not run the timer: order.start must enforce the boundary on its own.
    const auto started = f.call(MessageType::OrderStart, input);
    QCOMPARE(started.code, expired ? ErrorCode::IllegalOrderState : ErrorCode::Ok);
    const auto stored = f.repository->findOrderById(id);
    QVERIFY(stored);
    QVERIFY(stored->status == (expired ? OrderStatus::Cancelled : OrderStatus::Charging));
    QVERIFY(f.getPile().status == (expired ? PileStatus::Idle : PileStatus::Charging));
    QCOMPARE(f.repository->findUserById(userId)->balanceCents, balance);
    if (!expired) {
        QCOMPARE(stored->startedAt.value(), now.toString(Qt::ISODate));
        QCOMPARE(stored->orderId, id);
        return;
    }
    QVERIFY(!stored->startedAt && !stored->endedAt && !stored->paidAt);
    QVERIFY(!stored->unitPriceCentsPerKwh);
    QCOMPARE(stored->energyWh, qint64{0});
    QCOMPARE(stored->durationSeconds, qint64{0});
    QCOMPARE(stored->amountCents, qint64{0});
    QCOMPARE(f.call(MessageType::OrderCancel, orderInput(id)).code, ErrorCode::IllegalOrderState);
    const auto current = f.call(MessageType::OrderCurrent);
    QCOMPARE(current.code, ErrorCode::Ok);
    QVERIFY(current.data.value("order").isNull());
    const auto history = f.call(MessageType::OrderList);
    QCOMPARE(history.code, ErrorCode::Ok);
    QCOMPARE(history.data.value("items").toArray().size(), 1);
    QCOMPARE(history.data.value("items").toArray().first().toObject().value("status").toString(), "CANCELLED");
    QCOMPARE(f.service->expireDueReservations(now), 0);
    // Another user can immediately take the pile; repeating the stale start cannot release it.
    const auto other = f.login("13900000902");
    QCOMPARE(f.call(MessageType::OrderReserve, pileInput(), other).code, ErrorCode::Ok);
    QCOMPARE(f.call(MessageType::OrderStart, input).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.call(MessageType::OrderStart, input, other).code, ErrorCode::Forbidden);
    QVERIFY(f.getPile().status == PileStatus::Reserved);
}

// 数据行：列出会先触发过期清理的各类请求
void OrderFlowTests::reservationExpiryBeforeRequests_data()
{
    QTest::addColumn<bool>("sqlite");
    QTest::addColumn<QString>("type");
    for (bool sqlite : {false, true})
        for (const char *type : {MessageType::OrderCurrent, MessageType::OrderList,
             MessageType::StationList, MessageType::StationDetail, MessageType::OrderReserve,
             MessageType::OrderStart, MessageType::OrderCancel})
            QTest::newRow(qPrintable(QString::fromLatin1(type) + (sqlite ? "-sqlite" : "-memory")))
                << sqlite << QString::fromLatin1(type);
}

// 验证请求进入前服务端先处理过期预约再执行业务
void OrderFlowTests::reservationExpiryBeforeRequests()
{
    QFETCH(bool, sqlite);
    QFETCH(QString, type);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    const auto id = orderId(reserved);
    f.now = f.now.addSecs(DemoReservationDurationSeconds);
    auto input = pileInput(); input.insert("orderId", id); input.insert("stationId", 1);
    // Use only the fields belonging to each existing message.
    if (type.startsWith("order.")) input = type == MessageType::OrderCancel ? orderInput(id)
        : type == MessageType::OrderReserve || type == MessageType::OrderStart ? pileInput() : QJsonObject{};
    else input = type == MessageType::StationDetail ? QJsonObject{{"stationId", 1}} : QJsonObject{};
    const auto result = f.call(qPrintable(type), input);
    QCOMPARE(result.code, type == MessageType::OrderCancel ? ErrorCode::IllegalOrderState : ErrorCode::Ok);
    QVERIFY(f.repository->findOrderById(id)->status == OrderStatus::Cancelled);
    if (type == MessageType::OrderReserve) QVERIFY(f.getPile().status == PileStatus::Reserved);
    else if (type == MessageType::OrderStart) QVERIFY(f.getPile().status == PileStatus::Charging);
    else QVERIFY(f.getPile().status == PileStatus::Idle);
    if (type == MessageType::StationList)
        QCOMPARE(result.data.value("items").toArray().first().toObject().value("availablePileCount").toInt(), 1);
    if (type == MessageType::StationDetail)
        QCOMPARE(result.data.value("piles").toArray().first().toObject().value("status").toString(), "IDLE");
}

// 过期定时器可重复开启，服务重启后仍能清理过期预约
void OrderFlowTests::reservationTimerAndRestart()
{
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    const auto id = orderId(reserved);
    f.service->enableReservationExpiry();
    f.service->enableReservationExpiry(); // idempotent
    QCOMPARE(f.call(MessageType::AuthLogout).code, ErrorCode::Ok);
    f.now = f.now.addSecs(DemoReservationDurationSeconds);
    // Only repository reads while logged out: no user request triggers this expiry.
    QTRY_VERIFY(f.repository->findOrderById(id)->status == OrderStatus::Cancelled);
    QVERIFY(f.getPile().status == PileStatus::Idle);
    f.token = f.login("13900000901");
    const auto next = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(next.code, ErrorCode::Ok);
    const auto nextId = orderId(next);
    f.router.reset(); f.service.reset(); f.repository.reset();
    f.now = f.now.addSecs(DemoReservationDurationSeconds + 5);
    auto storage = std::make_unique<Repository>(QUuid::createUuid().toString());
    QVERIFY2(storage->open(f.databasePath(), &f.error), qPrintable(f.error));
    f.repository = std::move(storage);
    SessionStore noSessions;
    ApplicationService restarted(f.repository.get(), &noSessions, &f.pile, &f.prediction,
                                 nullptr, [&f] { return f.now; });
    QVERIFY(f.repository->findOrderById(nextId)->status == OrderStatus::Reserved);
    restarted.enableReservationExpiry();
    QVERIFY(f.repository->findOrderById(nextId)->status == OrderStatus::Cancelled);
    QVERIFY(f.getPile().status == PileStatus::Idle);
    QCOMPARE(restarted.expireDueReservations(f.now), 0);
}

// 数据行：分别注入更新失败与提交失败
void OrderFlowTests::reservationExpiryRollback_data()
{
    QTest::addColumn<bool>("commitFailure");
    QTest::newRow("order-update") << false;
    QTest::newRow("commit") << true;
}

// 过期处理失败时订单与电桩状态整体保持原样
void OrderFlowTests::reservationExpiryRollback()
{
    QFETCH(bool, commitFailure);
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    const QByteArray trigger = commitFailure
        ? "PRAGMA foreign_keys=ON; CREATE TABLE deferred_failure (user_id INTEGER "
          "REFERENCES users(user_id) DEFERRABLE INITIALLY DEFERRED); "
          "CREATE TRIGGER reject_expiry AFTER UPDATE ON charging_orders "
          "BEGIN INSERT INTO deferred_failure VALUES (999999); END;"
        : "CREATE TRIGGER reject_expiry BEFORE UPDATE ON charging_orders "
          "BEGIN SELECT RAISE(ABORT, 'injected expiry failure'); END;";
    QVERIFY2(f.sql(trigger), qPrintable(f.error));
    f.now = f.now.addSecs(DemoReservationDurationSeconds);
    const auto before = f.snapshot();
    QCOMPARE(f.service->expireDueReservations(f.now), -1);
    QCOMPARE(f.snapshot(), before); // pile release rolled back along with the order
    auto input = pileInput(); input.insert("reservationOrderId", orderId(reserved));
    QCOMPARE(f.call(MessageType::OrderStart, input).code, ErrorCode::InternalError);
    QCOMPARE(f.snapshot(), before);
    QVERIFY2(f.sql("DROP TRIGGER reject_expiry;"), qPrintable(f.error));
    QVERIFY(f.service->expireDueReservations(f.now) >= 1);
    QVERIFY(f.repository->findOrderById(orderId(reserved))->status == OrderStatus::Cancelled);
    QVERIFY(f.getPile().status == PileStatus::Idle);
}

// 已开始充电的订单不会再被预约过期影响
void OrderFlowTests::startedReservationNeverExpires()
{
    QFETCH(bool, sqlite);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    f.now = f.now.addSecs(DemoReservationDurationSeconds - 1);
    auto input = pileInput(); input.insert("reservationOrderId", orderId(reserved));
    QCOMPARE(f.call(MessageType::OrderStart, input).code, ErrorCode::Ok);
    const auto started = f.repository->findOrderById(orderId(reserved));
    f.now = f.now.addSecs(60);
    QVERIFY(f.service->expireDueReservations(f.now) >= 0);
    QCOMPARE(toJson(*f.repository->findOrderById(orderId(reserved))), toJson(*started));
    QVERIFY(f.getPile().status == PileStatus::Charging);
}

// 管理员登录时旧口令升级为 PBKDF2，并验证新建账号改密
void OrderFlowTests::sqliteAdminAccountsAndPasswordUpgrade()
{
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));

    const auto legacyLogin = f.service->loginAdmin(QStringLiteral("admin"),
                                                    QStringLiteral("123456"));
    QCOMPARE(legacyLogin.code, ErrorCode::Ok);
    QCOMPARE(legacyLogin.data.value(QStringLiteral("admin")).toObject()
                 .value(QStringLiteral("role")).toString(),
             QStringLiteral("SYS_ADMIN"));
    const auto upgraded = f.repository->findAdminByUsername(QStringLiteral("admin"));
    QVERIFY(upgraded.has_value());
    QCOMPARE(upgraded->passwordAlgorithm, QStringLiteral("PBKDF2_SHA256"));
    QVERIFY(upgraded->passwordHash.startsWith(QStringLiteral("$pbkdf2-sha256$")));

    const QJsonObject input{
        {QStringLiteral("username"), QStringLiteral("station.sqlite")},
        {QStringLiteral("initialPassword"), QStringLiteral("123456")},
        {QStringLiteral("displayName"), QStringLiteral("SQLite 站点管理员")},
        {QStringLiteral("role"), QStringLiteral("STATION_ADMIN")},
        {QStringLiteral("stationIds"), QJsonArray{1}},
    };
    const auto created = f.service->createAdminAccount(1, input);
    QCOMPARE(created.code, ErrorCode::Ok);
    const qint64 adminId = created.data.value(QStringLiteral("admin")).toObject()
                               .value(QStringLiteral("adminId")).toInteger();
    QVERIFY(adminId > 1);

    const auto initialLogin = f.service->loginAdmin(QStringLiteral("station.sqlite"),
                                                     QStringLiteral("123456"));
    QCOMPARE(initialLogin.code, ErrorCode::Ok);
    QCOMPARE(f.service->getDashboard(adminId, 7).message,
             QStringLiteral("PASSWORD_CHANGE_REQUIRED"));
    QCOMPARE(f.service->changeAdminPassword(adminId, QStringLiteral("123456"),
                                            QStringLiteral("654321")).code,
             ErrorCode::Ok);
    QCOMPARE(f.service->loginAdmin(QStringLiteral("station.sqlite"),
                                   QStringLiteral("123456")).code,
             ErrorCode::InvalidCredentials);
    QCOMPARE(f.service->loginAdmin(QStringLiteral("station.sqlite"),
                                   QStringLiteral("654321")).code,
             ErrorCode::Ok);

    QVERIFY(f.sql("SELECT count(*) FROM admin_audit_logs;\n"));
    QCOMPARE(f.error.trimmed(), QStringLiteral("5"));
}

// 预约、重复预约受限与取消后电桩释放
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

// 充电进度与停止自动结算，单价在开始时锁定不随站点改价
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
    const auto revenue = f.service->getDashboard(1, 30).data.value(QStringLiteral("totalRevenueCents")).toInteger();
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
    for (const auto &item : f.service->listAdminOrders(1).data.value(QStringLiteral("items")).toArray()) {
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
    QCOMPARE(f.service->getDashboard(1, 30).data.value(QStringLiteral("totalRevenueCents")).toInteger(), revenue + 675);
    const auto state = f.snapshot();
    QCOMPARE(f.call(MessageType::OrderStop, orderInput(orderId(started))).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.call(MessageType::OrderPay, orderInput(orderId(started))).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.snapshot(), state);
}

// 余额不足时订单转待支付并先释放电桩，补缴后完成
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

// 归属校验与订单状态机保护：越权和非法状态都被拒绝
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

// 参数校验、电桩离线故障及设备读写失败的处理
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

// 数据行：列举各个注入写入失败的操作点
void OrderFlowTests::sqliteFailuresRollBack_data()
{
    QTest::addColumn<QString>("operation");
    for (const char *name : {"reserve", "direct", "start", "cancel", "stop", "pay", "commit"}) {
        QTest::newRow(name) << QString::fromLatin1(name);
    }
}

// 用触发器制造写入失败，检查错误响应与前后数据快照
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

// 重启后订单与锁定单价仍在，旧会话令牌失效
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
    ApplicationService service(&reopened, &newSessions, &realMock, &f.prediction,
                                nullptr, [&f] { return f.now; });
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

// Mock 电桩读数单调不回退，且故障桩不能重启
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

// 整数分计费与高峰单价的边界与溢出校验
void OrderFlowTests::integerBilling()
{
    const auto peak = QDateTime::fromString(QStringLiteral("2026-09-08T00:00:00Z"), Qt::ISODate);
    QCOMPARE(chargingUnitPriceCents(133, peak).value(), qint64{160});
    QCOMPARE(chargingUnitPriceCents(132, peak).value(), qint64{158});
    QCOMPARE(chargingUnitPriceCents(1, peak).value(), qint64{1});
    QVERIFY(!chargingUnitPriceCents(0, peak));
    QVERIFY(!chargingUnitPriceCents(-1, peak));
    QVERIFY(!chargingUnitPriceCents(135, QDateTime()));
    QVERIFY(!chargingUnitPriceCents(std::numeric_limits<qint64>::max(), peak));
    QCOMPARE(orderAmountCents(2500, 135).value(), qint64{338});
    QCOMPARE(orderAmountCents(5000, 135).value(), qint64{675});
    QCOMPARE(orderAmountCents(0, 135).value(), qint64{0});
    QVERIFY(!orderAmountCents(-1, 135).has_value());
    QVERIFY(!orderAmountCents(100, 0).has_value());
    QVERIFY(!orderAmountCents(std::numeric_limits<qint64>::max(), 135).has_value());
}

// 经真实 TCP 客户端验证预约过期后无法开始充电
void OrderFlowTests::realClientTcpReservationExpiry()
{
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));
    TcpGateway gateway(f.router.get());
    QVERIFY2(gateway.start(0, QHostAddress::LocalHost, &f.error), qPrintable(f.error));
    client::TcpChargingApi api(QStringLiteral("127.0.0.1"), gateway.serverPort());
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy reserve(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy start(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy current(&api, &client::IChargingApi::currentOrderCompleted);
    QSignalSpy history(&api, &client::IChargingApi::orderListCompleted);
    (void)api.loginUser("13900000907"); QTRY_COMPARE(login.count(), 1);
    QVERIFY(qvariant_cast<client::LoginResult>(login.first().first()).ok());
    (void)api.reserve("PILE-A-01"); QTRY_COMPARE(reserve.count(), 1);
    const auto reserved = qvariant_cast<client::OrderResult>(reserve.first().first());
    QVERIFY(reserved.ok() && reserved.payload);
    const auto id = reserved.payload->order.orderId;
    const auto count = f.repository->listOrders().size();
    f.now = f.now.addSecs(DemoReservationDurationSeconds);
    (void)api.startCharging("PILE-A-01", id); QTRY_COMPARE(start.count(), 1);
    const auto refused = qvariant_cast<client::OrderResult>(start.first().first());
    QCOMPARE(refused.response.code, ErrorCode::IllegalOrderState);
    QVERIFY(!refused.payload);
    QCOMPARE(f.repository->listOrders().size(), count);
    QVERIFY(f.getPile().status == PileStatus::Idle);
    (void)api.getCurrentOrder(); QTRY_COMPARE(current.count(), 1);
    const auto empty = qvariant_cast<client::CurrentOrderResult>(current.first().first());
    QVERIFY(empty.ok() && empty.payload && !empty.payload->order);
    (void)api.listOrders(); QTRY_COMPARE(history.count(), 1);
    const auto listed = qvariant_cast<client::OrderListResult>(history.first().first());
    QVERIFY(listed.ok() && listed.payload);
    QCOMPARE(listed.payload->items.size(), 1);
    const auto order = listed.payload->items.first();
    QCOMPARE(order.orderId, id);
    QVERIFY(order.status == OrderStatus::Cancelled);
    QCOMPARE(order.reservedAt, reserved.payload->order.reservedAt);
    QVERIFY(!order.startedAt && !order.endedAt && !order.paidAt);
    QCOMPARE(order.amountCents, qint64{0});
}

// 真实客户端经TCP网关走完预约到支付的完整流程
void OrderFlowTests::realClientTcpOrderFlow()
{
    Fixture f;
    QVERIFY2(f.initialize(true), qPrintable(f.error));
    f.now = QDateTime::fromString(QStringLiteral("2026-09-08T02:59:00Z"), Qt::ISODate);
    TcpGateway gateway(f.router.get());
    QVERIFY2(gateway.start(0, QHostAddress::LocalHost, &f.error), qPrintable(f.error));
    client::TcpChargingApi api(QStringLiteral("127.0.0.1"), gateway.serverPort());
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy quote(&api, &client::IChargingApi::stationDetailCompleted);
    QSignalSpy current(&api, &client::IChargingApi::currentOrderCompleted);
    QSignalSpy reserve(&api, &client::IChargingApi::reservationCompleted);
    QSignalSpy cancel(&api, &client::IChargingApi::cancellationCompleted);
    QSignalSpy start(&api, &client::IChargingApi::chargingStartCompleted);
    QSignalSpy progress(&api, &client::IChargingApi::chargingProgressCompleted);
    QSignalSpy stop(&api, &client::IChargingApi::chargingStopCompleted);
    QSignalSpy recharge(&api, &client::IChargingApi::rechargeCompleted);
    QSignalSpy pay(&api, &client::IChargingApi::paymentCompleted);
    QSignalSpy list(&api, &client::IChargingApi::orderListCompleted);

    // 登录后用信号槽等待异步响应，逐步校验各步结果
    const auto loginRequest = api.loginUser(QStringLiteral("13900000903"));
    QTRY_COMPARE(login.size(), 1);
    const auto loggedIn = qvariant_cast<client::LoginResult>(login.takeFirst().at(0));
    QVERIFY(loggedIn.ok() && loggedIn.payload.has_value());
    QCOMPARE(loggedIn.response.requestId, loginRequest);
    QVERIFY(!api.getStation(1).isEmpty());
    QTRY_COMPARE(quote.size(), 1);
    const auto quoted = qvariant_cast<client::StationDetailResult>(quote.first().first());
    QVERIFY(quoted.ok() && quoted.payload);
    QCOMPARE(quoted.payload->station.priceCentsPerKwh, qint64{162});
    QCOMPARE(quoted.payload->station.pricingRule, QString::fromLatin1(DemoPeakPricingRule));
    QVERIFY(!api.getCurrentOrder().isEmpty());
    QTRY_COMPARE(current.size(), 1);
    const auto empty = qvariant_cast<client::CurrentOrderResult>(current.takeFirst().at(0));
    QVERIFY(empty.ok() && empty.payload.has_value() && !empty.payload->order.has_value());
    // 先预约再取消，验证桩释放后还能重新预约
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
    // 时间推到高峰结束之后，验证仍按开始时锁定的单价计费
    f.now = f.now.addSecs(1800); // settle after the morning peak ends
    f.pile.reading = {1800, 5000};
    QVERIFY(!api.getChargingProgress(id).isEmpty());
    QTRY_COMPARE(progress.size(), 1);
    const auto measured = qvariant_cast<client::ChargingProgressResult>(progress.takeFirst().at(0));
    QVERIFY(measured.ok() && measured.payload.has_value());
    QCOMPARE(measured.payload->order.unitPriceCentsPerKwh.value(), qint64{162});
    QCOMPARE(measured.payload->order.amountCents, qint64{810});
    // 停止后余额不足支付失败，充值后再支付成功
    QVERIFY(!api.stopCharging(id).isEmpty());
    QTRY_COMPARE(stop.size(), 1);
    const auto stopped = qvariant_cast<client::ChargingStopResult>(stop.takeFirst().at(0));
    QVERIFY(stopped.ok() && stopped.payload.has_value());
    QVERIFY(!stopped.payload->paid);
    QCOMPARE(stopped.payload->shortfallCents.value(), qint64{810});
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
    QCOMPARE(paid.payload->balanceCents, qint64{190});
    // 历史列表最新一单为本次已完成订单
    QVERIFY(!api.listOrders().isEmpty());
    QTRY_COMPARE(list.size(), 1);
    const auto history = qvariant_cast<client::OrderListResult>(list.takeFirst().at(0));
    QVERIFY(history.ok() && history.payload.has_value());
    QCOMPARE(history.payload->items.size(), 2);
    QCOMPARE(history.payload->items.first().orderId, id);
    QVERIFY(history.payload->items.first().status == OrderStatus::Completed);
}

// 演示会话到期只自动结算一次，重复调用不再扣费
void OrderFlowTests::demoDeadlineStopsOnce()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    f.now = QDateTime::fromString(QStringLiteral("2026-09-08T02:59:00Z"), Qt::ISODate);
    MockPile physical;
    ApplicationService service(f.repository.get(), &f.sessions, &physical, &f.prediction,
                                nullptr, [&f] { return f.now; });
    service.enableDemoAutomaticStop();
    service.completeDueDemoCharges(QDateTime::currentDateTimeUtc()); // finish seed sessions
    QCOMPARE(f.call(MessageType::WalletRecharge, {{QStringLiteral("amountCents"), 1000}}).code, ErrorCode::Ok);
    const auto result=service.startOrder(f.token, pileInput());
    QVERIFY(result.ok());
    OrderDto order; QVERIFY(fromJson(result.data.value("order").toObject(), &order));
    const auto started=QDateTime::fromString(*order.startedAt, Qt::ISODate);
    // 未满180秒不结算，超过时限才结算出一单
    QCOMPARE(service.completeDueDemoCharges(started.addSecs(DemoChargingDurationSeconds-1)),0);
    QCOMPARE(service.completeDueDemoCharges(started.addSecs(DemoChargingDurationSeconds+2)),1);
    const auto stopped=f.repository->findOrderById(order.orderId);
    QVERIFY(stopped); QCOMPARE(stopped->durationSeconds,qint64(DemoChargingDurationSeconds));
    QCOMPARE(stopped->energyWh,qint64(DemoChargingDurationSeconds*2));
    QCOMPARE(stopped->unitPriceCentsPerKwh.value(), qint64{162});
    QCOMPARE(stopped->amountCents, qint64{58});
    QVERIFY(stopped->status == OrderStatus::Completed);
    QVERIFY(f.getPile().status == PileStatus::Idle);
    const auto balance=f.repository->findUserById(order.userId)->balanceCents;
    QCOMPARE(service.completeDueDemoCharges(started.addSecs(999)),0);
    QCOMPARE(f.repository->findUserById(order.userId)->balanceCents,balance);
}

// 服务重启后仍能结算到期会话，余额不足则转为待支付
void OrderFlowTests::demoDeadlineHandlesDebtAfterRestart()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    f.now = QDateTime::fromString(QStringLiteral("2026-09-08T12:59:00Z"), Qt::ISODate);
    f.service->enableDemoAutomaticStop();
    f.service->completeDueDemoCharges(QDateTime::currentDateTimeUtc());
    const QString token=f.login(QStringLiteral("13912345678"));
    const auto result=f.service->startOrder(token,pileInput());
    QVERIFY(result.ok()); OrderDto order;
    QVERIFY(fromJson(result.data.value("order").toObject(),&order));
    QVERIFY(f.service->logout(token).ok());
    MockPile restartedPile;
    SessionStore noSessions;
    ApplicationService restarted(f.repository.get(),&noSessions,&restartedPile,&f.prediction);
    restarted.enableDemoAutomaticStop();
    const auto now=QDateTime::fromString(*order.startedAt,Qt::ISODate).addSecs(DemoChargingDurationSeconds+10);
    QCOMPARE(restarted.completeDueDemoCharges(now),1);
    const auto ended=f.repository->findOrderById(order.orderId);QVERIFY(ended);
    QCOMPARE(ended->unitPriceCentsPerKwh.value(), qint64{162});
    QCOMPARE(ended->amountCents, qint64{58});
    QVERIFY(ended->status == OrderStatus::PendingPayment);
    QVERIFY(f.getPile().status == PileStatus::Idle);
    QCOMPARE(f.repository->findUserById(order.userId)->balanceCents,qint64(0));
    QCOMPARE(restarted.completeDueDemoCharges(now),0);
}

// 从外部fixture文件读取高峰价格用例数据
void OrderFlowTests::peakQuotesAndStart_data()
{
    QTest::addColumn<bool>("sqlite");
    QTest::addColumn<QDateTime>("now");
    QTest::addColumn<qint64>("price135");
    QTest::addColumn<qint64>("price120");
    QFile file(QString::fromUtf8(CHARGING_PEAK_FIXTURE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto cases = QJsonDocument::fromJson(file.readAll()).object().value("cases").toArray();
    QVERIFY(!cases.isEmpty());
    for (bool sqlite : {false, true}) {
        for (const auto &value : cases) {
            const auto row = value.toObject();
            const auto name = row.value("name").toString() + (sqlite ? "-sqlite" : "-memory");
            QTest::newRow(qPrintable(name)) << sqlite
                << QDateTime::fromString(row.value("now").toString(), Qt::ISODate)
                << row.value("price135").toInteger() << row.value("price120").toInteger();
        }
    }
}

// 列表与详情返回高峰折算价，库中基础价保持不变
void OrderFlowTests::peakQuotesAndStart()
{
    QFETCH(bool, sqlite);
    QFETCH(QDateTime, now);
    QFETCH(qint64, price135);
    QFETCH(qint64, price120);
    Fixture f;
    f.now = now;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto listed = f.call(MessageType::StationList);
    QCOMPARE(listed.code, ErrorCode::Ok);
    const auto items = listed.data.value("items").toArray();
    QCOMPARE(items.size(), sqlite ? 2 : 3);
    for (const auto &item : items) {
        const auto station = item.toObject();
        const qint64 id = station.value("stationId").toInteger();
        // The admin in-memory substitute has a third station and a different
        // station 2 base price (128); don't rewrite those fixtures for pricing.
        const auto stored = f.repository->findStationById(id);
        QVERIFY(stored);
        const qint64 basePrice = stored->priceCentsPerKwh;
        const qint64 expected = basePrice == 135 ? price135 : basePrice == 120 ? price120
            : (price135 == 162 ? qint64{154} : qint64{128});
        QCOMPARE(station.value("priceCentsPerKwh").toInteger(), expected);
        QCOMPARE(station.value("pricingRule").toString(), QString::fromLatin1(DemoPeakPricingRule));
        const auto detail = f.call(MessageType::StationDetail, {{"stationId", id}});
        QCOMPARE(detail.code, ErrorCode::Ok);
        QCOMPARE(detail.data.value("station").toObject().value("priceCentsPerKwh").toInteger(), expected);
        QCOMPARE(f.repository->findStationById(id)->priceCentsPerKwh, basePrice);
        QVERIFY(stored->pricingRule.isEmpty());
        const auto admin = f.service->listAdminStations(1, {}, {});
        QCOMPARE(admin.code, ErrorCode::Ok);
        for (const auto &adminItem : admin.data.value("items").toArray()) {
            const auto adminStation = adminItem.toObject();
            if (adminStation.value("stationId").toInteger() != id) continue;
            QCOMPARE(adminStation.value("priceCentsPerKwh").toInteger(), stored->priceCentsPerKwh);
            QVERIFY(!adminStation.contains("pricingRule"));
        }
        // FAST and SLOW piles use the same multiplier, never a pile-type tariff.
        // 快慢桩使用同一倍率，开始充电时写入锁定单价
        const auto code = id == 1 ? "PILE-A-01" : id == 3 ? "PILE-C-01" : "PILE-B-02";
        if (!sqlite && id == 2) {
            // Enable the initially faulted slow pile in this isolated test only.
            auto pile = f.getPile(code);
            pile.status = PileStatus::Idle;
            QVERIFY(f.repository->updatePile(pile));
        }
        const auto started = f.call(MessageType::OrderStart, pileInput(code));
        QCOMPARE(started.code, ErrorCode::Ok);
        QCOMPARE(orderJson(started).value("unitPriceCentsPerKwh").toInteger(), expected);
        QCOMPARE(orderJson(started).value("startedAt").toString(), now.toUTC().toString(Qt::ISODate));
        QCOMPARE(f.call(MessageType::OrderStop, orderInput(orderId(started))).code, ErrorCode::Ok);
    }
}

// 构造跨越高峰边界的预约与开始时间组合
void OrderFlowTests::peakSnapshotSurvivesSettlement_data()
{
    QTest::addColumn<bool>("sqlite");
    QTest::addColumn<QString>("startTime");
    QTest::addColumn<qint64>("expectedPrice");
    for (bool sqlite : {false, true}) {
        const QString suffix = sqlite ? "-sqlite" : "-memory";
        QTest::newRow(qPrintable("normal-to-peak" + suffix))
            << sqlite << QStringLiteral("2026-09-07T23:59:00Z") << qint64{135};
        QTest::newRow(qPrintable("peak-to-normal" + suffix))
            << sqlite << QStringLiteral("2026-09-08T02:59:00Z") << qint64{162};
        QTest::newRow(qPrintable("reserve-normal-start-peak" + suffix))
            << sqlite << QStringLiteral("2026-09-08T00:00:00Z") << qint64{162};
        QTest::newRow(qPrintable("reserve-peak-start-normal" + suffix))
            << sqlite << QStringLiteral("2026-09-08T03:00:00Z") << qint64{135};
    }
}

// 预约时不锁价，开始充电时才锁定单价
void OrderFlowTests::peakSnapshotSurvivesSettlement()
{
    QFETCH(bool, sqlite);
    QFETCH(QString, startTime);
    QFETCH(qint64, expectedPrice);
    Fixture f;
    QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto startedAt = QDateTime::fromString(startTime, Qt::ISODate);
    f.now = startedAt.addSecs(-60);
    const auto reserved = f.call(MessageType::OrderReserve, pileInput());
    QCOMPARE(reserved.code, ErrorCode::Ok);
    QVERIFY(orderJson(reserved).value("unitPriceCentsPerKwh").isNull());
    auto input = pileInput();
    input.insert("reservationOrderId", orderId(reserved));
    f.now = startedAt;
    const auto started = f.call(MessageType::OrderStart, input);
    QCOMPARE(started.code, ErrorCode::Ok);
    const auto id = orderId(started);
    QCOMPARE(orderJson(started).value("unitPriceCentsPerKwh").toInteger(), expectedPrice);
    // 中途改动站点基础价，进度与结算仍用锁定单价
    f.now = startedAt.addSecs(120);
    f.pile.reading = {120, 1000};
    auto station = f.repository->findStationById(1);
    station->priceCentsPerKwh = 999;
    QVERIFY(f.repository->updateStation(*station));
    const auto progress = f.call(MessageType::OrderProgress, orderInput(id));
    QCOMPARE(progress.code, ErrorCode::Ok);
    QCOMPARE(orderJson(progress).value("amountCents").toInteger(), expectedPrice);
    const auto stopped = f.call(MessageType::OrderStop, orderInput(id));
    QCOMPARE(stopped.code, ErrorCode::Ok);
    QCOMPARE(orderJson(stopped).value("status").toString(), QStringLiteral("PENDING_PAYMENT"));
    QCOMPARE(orderJson(stopped).value("unitPriceCentsPerKwh").toInteger(), expectedPrice);
    QCOMPARE(orderJson(stopped).value("amountCents").toInteger(), expectedPrice);
    QVERIFY(f.getPile().status == PileStatus::Idle);
    QCOMPARE(f.call(MessageType::OrderPay, orderInput(id)).code, ErrorCode::InsufficientBalance);

    // Reopen SQLite before paying on another day; only the persisted snapshot matters.
    // 重建仓库与服务，验证只依赖已持久化的价格快照
    f.router.reset();
    f.service.reset();
    if (sqlite) {
        f.repository.reset();
        auto reopened = std::make_unique<Repository>(QUuid::createUuid().toString());
        QVERIFY2(reopened->open(f.databasePath(), &f.error), qPrintable(f.error));
        f.repository = std::move(reopened);
    }
    SessionStore restartedSessions;
    f.service = std::make_unique<ApplicationService>(f.repository.get(), &restartedSessions,
        &f.pile, &f.prediction, nullptr, [&f] { return f.now; });
    f.router = std::make_unique<RequestRouter>(f.service.get());
    f.now = startedAt.addDays(1).addSecs(3600);
    f.token = f.login(QStringLiteral("13900000901"));
    QCOMPARE(f.call(MessageType::WalletRecharge, {{"amountCents", 1000}}).code, ErrorCode::Ok);
    const auto paid = f.call(MessageType::OrderPay, orderInput(id));
    QCOMPARE(paid.code, ErrorCode::Ok);
    QCOMPARE(orderJson(paid).value("unitPriceCentsPerKwh").toInteger(), expectedPrice);
    QCOMPARE(orderJson(paid).value("amountCents").toInteger(), expectedPrice);
    QCOMPARE(paid.data.value("balanceCents").toInteger(), 1000 - expectedPrice);
    const auto snapshot = f.snapshot();
    // 重复支付返回状态非法，数据快照保持不变
    QCOMPARE(f.call(MessageType::OrderPay, orderInput(id)).code, ErrorCode::IllegalOrderState);
    QCOMPARE(f.snapshot(), snapshot);
}

QTEST_GUILESS_MAIN(OrderFlowTests)
#include "order_flow_tests.moc"
