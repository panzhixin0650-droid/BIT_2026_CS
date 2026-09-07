#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "admin_ui/admin_facade.h"
#include "application/application_service.h"
#include "application/session_store.h"
#include "persistence/in_memory_repository.h"
#include "persistence/repository.h"
#include "transport/request_router.h"
#include "transport/tcp_gateway.h"
#include "api/tcp_charging_api.h"

#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <memory>

using namespace charging;
using namespace charging::server;
using namespace charging::protocol;

namespace {
SupportTicketDraft draft()
{
    return {QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("结算页面问题"),
        QStringLiteral("用户反馈结算后提示异常；实际订单结果待核实。"), QStringLiteral("gpt-5.6-sol")};
}

struct Fixture {
    QTemporaryDir temp;
    std::unique_ptr<IRepository> repository;
    SessionStore sessions;
    MockPile pile;
    MockPredictionProvider predictions;
    std::unique_ptr<ApplicationService> service;
    QString error;
    QString token;
    QString path() const { return temp.filePath(QStringLiteral("tickets.db")); }
    bool sql(const QByteArray &input)
    {
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QStringLiteral(CHARGING_SQLITE3_EXECUTABLE), {"-batch", "-bail", path()});
        if (!process.waitForStarted()) return false;
        process.write(input); process.closeWriteChannel();
        if (!process.waitForFinished(10000)) { process.kill(); process.waitForFinished(); return false; }
        error = QString::fromUtf8(process.readAll());
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
    bool initialize(bool sqlite, bool migrate = true, bool adminAccounts = true, bool repairs = false)
    {
        if (sqlite) {
            QStringList paths{QStringLiteral(CHARGING_DATABASE_MIGRATION_PATH), QStringLiteral(CHARGING_DATABASE_SEED_PATH)};
            if (migrate) paths.append(QStringLiteral(CHARGING_TICKET_MIGRATION_PATH));
            if (migrate && adminAccounts) paths.append(QStringLiteral(CHARGING_ADMIN_MIGRATION_PATH));
            if (repairs) paths.append(QStringLiteral(CHARGING_REPAIR_MIGRATION_PATH));
            for (const auto &path : paths) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly) || !sql(file.readAll())) return false;
            }
            auto storage = std::make_unique<Repository>(QUuid::createUuid().toString());
            if (!storage->open(path(), &error)) return false;
            repository = std::move(storage);
        } else repository = std::make_unique<InMemoryRepository>();
        service = std::make_unique<ApplicationService>(repository.get(), &sessions, &pile, &predictions);
        token = service->loginUser({{"phone", "13800000001"}}).data.value("token").toString();
        return !token.isEmpty();
    }
    QJsonObject businessSnapshot() const
    {
        QJsonArray users, stations, piles, orders;
        for (const auto &row : repository->listUsers()) users.append(toJson(row));
        for (const auto &row : repository->listStations()) stations.append(toJson(row));
        for (const auto &row : repository->listPiles()) piles.append(toJson(row));
        for (const auto &row : repository->listOrders()) orders.append(toJson(row));
        return {{"users", users}, {"stations", stations}, {"piles", piles}, {"orders", orders}};
    }
};
void backends()
{
    QTest::addColumn<bool>("sqlite");
    QTest::newRow("sqlite") << true;
    QTest::newRow("in-memory") << false;
}
}

class SupportTicketFlowTests final : public QObject {
    Q_OBJECT
private slots:
    void lifecycleAndIsolation_data() { backends(); }
    void lifecycleAndIsolation();
    void validationAndPagination_data() { backends(); }
    void validationAndPagination();
    void rollback_data() { backends(); }
    void rollback();
    void legacySchemaKeepsBusinessWorking();
    void persistentAndStorageFailure();
    void realClientTcpToSqliteAndAdmin();
    void realClientTcpToSqliteAndAdmin_data() {
        QTest::addColumn<bool>("repair");
        QTest::newRow("support") << false;
        QTest::newRow("repair") << true;
    }
    void repairLifecycle_data() { backends(); }
    void repairLifecycle();
    void repairMigrationPreservesExistingTickets();
    void legacyAdminLoginKeepsSchema_data();
    void legacyAdminLoginKeepsSchema();
    void adminAuthorizationIsRechecked_data() { backends(); }
    void adminAuthorizationIsRechecked();
    void mixedAdminAndTicketRollback_data() { backends(); }
    void mixedAdminAndTicketRollback();
    void upgradePreservesTickets();
    void mismatchedSchemaVersionsAreRejected();
};

void SupportTicketFlowTests::lifecycleAndIsolation()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto baseline = f.businessSnapshot();
    const auto input = toJson(draft());
    const auto created = f.service->createSupportTicket(f.token, input);
    QVERIFY(created.ok());
    const auto ticket = created.data.value("ticket").toObject();
    const qint64 id = ticket.value("ticketId").toInteger();
    QCOMPARE(ticket.value("status").toString(), QStringLiteral("OPEN"));
    QCOMPARE(f.service->createSupportTicket(f.token, input).data, created.data);
    auto changed = input; changed.insert("summary", "different");
    QCOMPARE(f.service->createSupportTicket(f.token, changed).code, ErrorCode::InvalidRequest);
    const auto secondToken = f.service->loginUser({{"phone", "13800000005"}}).data.value("token").toString();
    QVERIFY(!secondToken.isEmpty());
    QVERIFY(f.service->listSupportTickets(secondToken, {}).data.value("items").toArray().isEmpty());
    QCOMPARE(f.service->getSupportTicket(secondToken, {{"ticketId", id}}).code, ErrorCode::NotFound);
    QCOMPARE(f.service->createSupportTicket("bad", input).code, ErrorCode::InvalidSession);
    auto user = f.repository->findUserById(1).value();
    user.status = UserStatus::Frozen; QVERIFY(f.repository->updateUser(user));
    QCOMPARE(f.service->getSupportTicket(f.token, {{"ticketId", id}}).code, ErrorCode::Forbidden);
    QCOMPARE(f.service->createSupportTicket(f.token, input).code, ErrorCode::Forbidden);
    QCOMPARE(f.service->listSupportTickets(f.token, {}).code, ErrorCode::Forbidden);
    user.status = UserStatus::Active; QVERIFY(f.repository->updateUser(user));
    AdminFacade admin(f.service.get());
    const QJsonObject update{{"ticketId", id}, {"status", "RESOLVED"}, {"reply", ""}};
    QCOMPARE(admin.listSupportTickets().code, ErrorCode::Forbidden);
    QCOMPARE(admin.updateSupportTicket(update).code, ErrorCode::Forbidden);
    QVERIFY(admin.login("admin", "123456").ok());
    QCOMPARE(admin.updateSupportTicket(update).code, ErrorCode::InvalidRequest);
    auto valid = update; valid.insert("reply", QStringLiteral("已核实，请刷新订单查看。"));
    QVERIFY(admin.updateSupportTicket(valid).ok());
    const auto current = f.service->getSupportTicket(f.token, {{"ticketId", id}});
    QCOMPARE(current.data.value("ticket").toObject().value("status").toString(), QStringLiteral("RESOLVED"));
    QCOMPARE(f.service->createSupportTicket(f.token, input).data, current.data);
    admin.logout();
    QCOMPARE(admin.updateSupportTicket(valid).code, ErrorCode::Forbidden);
    QVERIFY(!admin.login("admin", "wrong").ok());
    QCOMPARE(admin.listSupportTickets().code, ErrorCode::Forbidden);
    QCOMPARE(f.businessSnapshot(), baseline); // Ticket actions never touch charging or money.
    RequestRouter router(f.service.get());
    RequestEnvelope request;
    request.type = "support.ticket.admin.update"; request.requestId = "forbidden-route";
    request.token = f.token; request.data = valid;
    QCOMPARE(router.route(request).code, ErrorCode::InvalidRequest);
}

void SupportTicketFlowTests::validationAndPagination()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    auto invalid = toJson(draft()); invalid.insert("userId", 5);
    QCOMPARE(f.service->createSupportTicket(f.token, invalid).code, ErrorCode::InvalidRequest);
    invalid = toJson(draft()); invalid.insert("summary", QString(4001, 'x'));
    QCOMPARE(f.service->createSupportTicket(f.token, invalid).code, ErrorCode::InvalidRequest);
    for (int i = 0; i < 12; ++i) QVERIFY(f.service->createSupportTicket(f.token, toJson(draft())).ok());
    const auto page = f.service->listSupportTickets(f.token, {});
    QVERIFY(page.ok());
    const auto items = page.data.value("items").toArray();
    QCOMPARE(items.size(), 10); QVERIFY(page.data.value("hasMore").toBool());
    QCOMPARE(items.first().toObject().value("ticketId").toInteger(), qint64(12));
    const auto rest = f.service->listSupportTickets(f.token, {{"beforeTicketId", items.last().toObject().value("ticketId")}});
    QCOMPARE(rest.data.value("items").toArray().size(), 2);
    QVERIFY(!rest.data.value("hasMore").toBool());
    QCOMPARE(f.service->listSupportTickets(f.token, {{"userId", 5}}).code, ErrorCode::InvalidRequest);
    QCOMPARE(f.service->listSupportTickets(f.token, {{"beforeTicketId", 1.5}}).code, ErrorCode::InvalidRequest);
    QCOMPARE(f.service->getSupportTicket(f.token, {{"ticketId", 999}}).code, ErrorCode::NotFound);
}

void SupportTicketFlowTests::rollback()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    SupportTicketDto ticket;
    static_cast<SupportTicketDraft &>(ticket) = draft();
    ticket.userId = 1;
    ticket.createdAt = ticket.updatedAt = QStringLiteral("2026-09-07T08:00:00Z");
    QVERIFY(f.repository->beginTransaction());
    QVERIFY(f.repository->createSupportTicket(ticket).ticketId > 0);
    f.repository->rollbackTransaction();
    QVERIFY(f.repository->listSupportTickets({}, {}, 10).isEmpty());
    QVERIFY(!f.repository->findSupportSubmission(1, ticket.submissionId));
    const auto saved = f.repository->createSupportTicket(ticket);
    QCOMPARE(saved.ticketId, qint64(1));
}

void SupportTicketFlowTests::legacySchemaKeepsBusinessWorking()
{
    Fixture f; QVERIFY2(f.initialize(true, false), qPrintable(f.error));
    QVERIFY(!f.repository->supportsSupportTickets());
    const auto baseline = f.businessSnapshot();
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(draft())).code, ErrorCode::ServiceUnavailable);
    QVERIFY(f.service->getProfile(f.token).ok());
    QVERIFY(f.service->listUserOrders(f.token).ok());
    QCOMPARE(f.businessSnapshot(), baseline);
    // The application doesn't silently create tables or upgrade the database.
    QVERIFY(f.sql("PRAGMA user_version;")); QCOMPARE(f.error.trimmed(), QStringLiteral("1"));
}

void SupportTicketFlowTests::persistentAndStorageFailure()
{
    Fixture f; QVERIFY2(f.initialize(true), qPrintable(f.error));
    const auto created = f.service->createSupportTicket(f.token, toJson(draft()));
    QVERIFY(created.ok());
    const auto id = created.data.value("ticket").toObject().value("ticketId");
    AdminFacade admin(f.service.get());
    QVERIFY(admin.login("admin", "123456").ok());
    QVERIFY(admin.updateSupportTicket({{"ticketId", id}, {"status", "IN_PROGRESS"}, {"reply", "checking"}}).ok());
    auto *repository = static_cast<Repository *>(f.repository.get());
    repository->close(); QVERIFY(repository->open(f.path()));
    QCOMPARE(f.service->getSupportTicket(f.token, {{"ticketId", id}}).data.value("ticket").toObject().value("reply").toString(), QStringLiteral("checking"));
    QVERIFY(f.sql("CREATE TRIGGER fail_support_insert BEFORE INSERT ON support_tickets BEGIN SELECT RAISE(ABORT,'test failure'); END;"));
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(draft())).code, ErrorCode::InternalError);
    QCOMPARE(f.repository->listSupportTickets({}, {}, 10).size(), 1);
    QVERIFY(f.service->getProfile(f.token).ok());
    QVERIFY(f.sql("DROP TRIGGER fail_support_insert;"));
    QVERIFY(f.service->createSupportTicket(f.token, toJson(draft())).ok());
}

void SupportTicketFlowTests::realClientTcpToSqliteAndAdmin()
{
    QFETCH(bool, repair);
    Fixture f; QVERIFY2(f.initialize(true, true, true, repair), qPrintable(f.error));
    RequestRouter router(f.service.get());
    TcpGateway gateway(&router);
    QVERIFY(gateway.start(0, QHostAddress::LocalHost));
    client::TcpChargingApi api("127.0.0.1", gateway.serverPort());
    QSignalSpy login(&api, &client::IChargingApi::loginCompleted);
    QSignalSpy create(&api, &client::IChargingApi::supportTicketCreated);
    QSignalSpy list(&api, &client::IChargingApi::supportTicketsListed);
    QSignalSpy detail(&api, &client::IChargingApi::supportTicketDetailed);
    QSignalSpy profile(&api, &client::IChargingApi::profileCompleted);
    QVERIFY(!api.loginUser("13800000001").isEmpty());
    QTRY_COMPARE(login.size(), 1);
    QVERIFY(qvariant_cast<client::LoginResult>(login.takeFirst().first()).ok());
    auto input = draft();
    if (repair) { input.pileCode = "PILE-A-01"; input.faultType = QStringLiteral("无法启动充电"); }
    const auto requestId = api.createSupportTicket(input);
    QTRY_COMPARE(create.size(), 1);
    auto created = qvariant_cast<client::TicketResult>(create.takeFirst().first());
    QVERIFY(created.ok() && created.payload);
    QCOMPARE(created.payload->ticket.pileCode, input.pileCode);
    QCOMPARE(created.response.requestId, requestId);
    const auto id = created.payload->ticket.ticketId;
    QVERIFY(!api.createSupportTicket(input).isEmpty());
    QTRY_COMPARE(create.size(), 1);
    QCOMPARE(qvariant_cast<client::TicketResult>(create.takeFirst().first()).payload->ticket.ticketId, id);
    AdminFacade admin(f.service.get());
    QVERIFY(admin.login("admin", "123456").ok());
    QVERIFY(admin.updateSupportTicket({{"ticketId", id}, {"status", "RESOLVED"}, {"reply", QStringLiteral("管理员回复：已核对")}}).ok());
    QVERIFY(!api.listSupportTickets().isEmpty());
    QTRY_COMPARE(list.size(), 1);
    const auto page = qvariant_cast<client::TicketListResult>(list.takeFirst().first());
    QVERIFY(page.ok() && page.payload);
    QCOMPARE(page.payload->items.size(), 1);
    QVERIFY(page.payload->items.first().status == TicketStatus::Resolved);
    QCOMPARE(page.payload->items.first().faultType, input.faultType);
    QVERIFY(!api.getSupportTicket(id).isEmpty());
    QTRY_COMPARE(detail.size(), 1);
    QVERIFY(qvariant_cast<client::TicketResult>(detail.takeFirst().first()).payload->ticket.reply.contains(QStringLiteral("管理员回复")));
    // Error routing doesn't invalidate a healthy session.
    QVERIFY(!api.getSupportTicket(9999).isEmpty());
    QTRY_COMPARE(detail.size(), 1);
    QCOMPARE(qvariant_cast<client::TicketResult>(detail.takeFirst().first()).response.code, ErrorCode::NotFound);
    QVERIFY(!api.getProfile().isEmpty());
    QTRY_COMPARE(profile.size(), 1);
    QVERIFY(qvariant_cast<client::UserResult>(profile.takeFirst().first()).ok());
    QVERIFY(!api.loginUser("13800000005").isEmpty());
    QTRY_COMPARE(login.size(), 1);
    QVERIFY(!api.getSupportTicket(id).isEmpty());
    QTRY_COMPARE(detail.size(), 1);
    QCOMPARE(qvariant_cast<client::TicketResult>(detail.takeFirst().first()).response.code, ErrorCode::NotFound);
    QVERIFY(!api.listSupportTickets().isEmpty());
    QTRY_COMPARE(list.size(), 1);
    QVERIFY(qvariant_cast<client::TicketListResult>(list.takeFirst().first()).payload->items.isEmpty());
}

void SupportTicketFlowTests::legacyAdminLoginKeepsSchema_data()
{
    QTest::addColumn<bool>("tickets");
    QTest::newRow("schema-1") << false;
    QTest::newRow("schema-2-tickets") << true;
}

void SupportTicketFlowTests::legacyAdminLoginKeepsSchema()
{
    QFETCH(bool, tickets);
    Fixture f; QVERIFY2(f.initialize(true, tickets, false), qPrintable(f.error));
    QVERIFY(!f.repository->supportsAdminAccounts());
    QCOMPARE(f.repository->supportsSupportTickets(), tickets);
    const auto original = f.repository->findAdminById(1).value();
    const auto snapshot = f.businessSnapshot();
    AdminFacade admin(f.service.get());
    const auto login = admin.login("admin", "123456");
    QVERIFY(login.ok());
    QVERIFY(!login.data.value("admin").toObject().value("adminAccountsAvailable").toBool());
    QVERIFY(admin.getDashboard(7).ok());
    QVERIFY(admin.listOrders().ok());
    QVERIFY(admin.listStations().ok());
    QCOMPARE(admin.listAdmins().message, QStringLiteral("ADMIN_ACCOUNTS_MIGRATION_REQUIRED"));
    QCOMPARE(admin.createAdmin({}).code, ErrorCode::ServiceUnavailable);
    QCOMPARE(admin.updateAdmin({}).code, ErrorCode::ServiceUnavailable);
    QCOMPARE(admin.changePassword("123456", "Changed-789").code, ErrorCode::ServiceUnavailable);
    QCOMPARE(f.repository->findAdminById(1)->passwordHash, original.passwordHash);
    QCOMPARE(f.businessSnapshot(), snapshot);
    if (tickets) {
        const auto created = f.service->createSupportTicket(f.token, toJson(draft()));
        QVERIFY(created.ok());
        QVERIFY(admin.listSupportTickets().ok());
        QVERIFY(admin.updateSupportTicket({{"ticketId", created.data.value("ticket").toObject().value("ticketId")},
            {"status", "RESOLVED"}, {"reply", "legacy schema remains supported"}}).ok());
    } else QCOMPARE(admin.listSupportTickets().code, ErrorCode::ServiceUnavailable);
    QVERIFY(!admin.login("admin", "wrong").ok());
    QVERIFY(!admin.listSupportTickets().ok());
    QVERIFY(!admin.listOrders().ok());
    QVERIFY(f.sql("PRAGMA user_version;"));
    QCOMPARE(f.error.trimmed(), tickets ? QStringLiteral("2") : QStringLiteral("1"));
}

void SupportTicketFlowTests::adminAuthorizationIsRechecked()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto ticket = f.service->createSupportTicket(f.token, toJson(draft()));
    QVERIFY(ticket.ok());
    const QJsonObject update{{"ticketId", ticket.data.value("ticket").toObject().value("ticketId")},
        {"status", "RESOLVED"}, {"reply", "checked"}};
    QVERIFY(!f.service->listAdminSupportTickets(0).ok());
    QVERIFY(!f.service->updateAdminSupportTicket(0, update).ok());
    AdminFacade system(f.service.get()); QVERIFY(system.login("admin", "123456").ok());
    for (const auto &role : {QStringLiteral("SYS_ADMIN"), QStringLiteral("STATION_ADMIN"), QStringLiteral("USER_ADMIN")}) {
        const auto created = system.createAdmin({{"username", role}, {"initialPassword", "Initial-123"},
            {"displayName", "test"}, {"role", role},
            {"stationIds", role == "STATION_ADMIN" ? QJsonArray{1} : QJsonArray{}}});
        QVERIFY(created.ok());
        const qint64 id = created.data.value("admin").toObject().value("adminId").toInteger();
        AdminFacade actor(f.service.get()); QVERIFY(actor.login(role, "Initial-123").ok());
        QCOMPARE(actor.listSupportTickets().message, QStringLiteral("PASSWORD_CHANGE_REQUIRED"));
        QCOMPARE(actor.updateSupportTicket(update).message, QStringLiteral("PASSWORD_CHANGE_REQUIRED"));
        QVERIFY(actor.changePassword("Initial-123", "Changed-456").ok());
        QVERIFY(!actor.listSupportTickets().ok());
        QVERIFY(actor.login(role, "Changed-456").ok());
        if (role == "SYS_ADMIN") {
            QVERIFY(actor.listSupportTickets().ok());
            QVERIFY(actor.updateSupportTicket(update).ok());
            QVERIFY(system.updateAdmin({{"adminId", id}, {"displayName", "demoted"},
                {"role", "USER_ADMIN"}, {"status", "ACTIVE"}, {"reason", "test role change"},
                {"stationIds", QJsonArray{}}}).ok());
        }
        QCOMPARE(actor.listSupportTickets().code, ErrorCode::Forbidden);
        QCOMPARE(actor.updateSupportTicket(update).code, ErrorCode::Forbidden);
        auto record = f.repository->findAdminById(id).value();
        record.status = "DISABLED";
        QVERIFY(f.repository->updateAdmin(record));
        QCOMPARE(actor.listSupportTickets().code, ErrorCode::InvalidSession);
        QCOMPARE(actor.updateSupportTicket(update).code, ErrorCode::InvalidSession);
        QVERIFY(!actor.login(role, "Changed-456").ok());
        QVERIFY(!actor.listSupportTickets().ok());
    }
    QVERIFY(system.listSupportTickets().ok());
    QVERIFY(!system.login("admin", "wrong").ok());
    QVERIFY(!system.listSupportTickets().ok());
    QVERIFY(!system.listOrders().ok());
}

void SupportTicketFlowTests::mixedAdminAndTicketRollback()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite), qPrintable(f.error));
    const auto originalAdmin = f.repository->findAdminById(1).value();
    const auto originalTicket = f.service->createSupportTicket(f.token, toJson(draft()));
    QVERIFY(originalTicket.ok());
    const auto id = originalTicket.data.value("ticket").toObject().value("ticketId").toInteger();
    auto ticket = f.repository->findSupportTicket(id).value();
    auto admin = originalAdmin;
    admin.username = "rollback.admin";
    QVERIFY(f.repository->beginTransaction());
    const auto pendingAdmin = f.repository->createAdmin(admin);
    QVERIFY(pendingAdmin.adminId > 1);
    admin = originalAdmin; admin.displayName = "rollback edit";
    QVERIFY(f.repository->updateAdmin(admin));
    ticket.reply = "rollback reply";
    QVERIFY(f.repository->updateSupportTicket(ticket));
    ticket.submissionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto pendingTicket = f.repository->createSupportTicket(ticket);
    QVERIFY(pendingTicket.ticketId > id);
    f.repository->rollbackTransaction();
    QCOMPARE(f.repository->findAdminById(1)->displayName, originalAdmin.displayName);
    QVERIFY(!f.repository->findAdminByUsername("rollback.admin"));
    QVERIFY(f.repository->findSupportTicket(id)->reply.isEmpty());
    QVERIFY(!f.repository->findSupportTicket(pendingTicket.ticketId));
    admin = originalAdmin; admin.username = "after.rollback";
    QCOMPARE(f.repository->createAdmin(admin).adminId, pendingAdmin.adminId);
    QCOMPARE(f.repository->createSupportTicket(ticket).ticketId, pendingTicket.ticketId);
}

void SupportTicketFlowTests::upgradePreservesTickets()
{
    Fixture f; QVERIFY2(f.initialize(true, true, false), qPrintable(f.error));
    const auto business = f.businessSnapshot();
    const auto created = f.service->createSupportTicket(f.token, toJson(draft()));
    QVERIFY(created.ok());
    const auto id = created.data.value("ticket").toObject().value("ticketId").toInteger();
    AdminFacade admin(f.service.get()); QVERIFY(admin.login("admin", "123456").ok());
    const auto updated = admin.updateSupportTicket({{"ticketId", id}, {"status", "RESOLVED"}, {"reply", "keep reply"}});
    QVERIFY(updated.ok());
    const auto credential = f.repository->findAdminById(1)->passwordHash;
    auto *storage = static_cast<Repository *>(f.repository.get());
    admin.logout(); storage->close();
    QFile migration(QStringLiteral(CHARGING_ADMIN_MIGRATION_PATH));
    QVERIFY(migration.open(QIODevice::ReadOnly));
    QVERIFY2(f.sql(migration.readAll()), qPrintable(f.error));
    QVERIFY2(storage->open(f.path(), &f.error), qPrintable(f.error));
    QVERIFY(storage->supportsAdminAccounts() && storage->supportsSupportTickets());
    QCOMPARE(storage->findAdminById(1)->passwordHash, credential);
    QCOMPARE(f.service->getSupportTicket(f.token, {{"ticketId", id}}).data, updated.data);
    QCOMPARE(f.businessSnapshot(), business);
    QVERIFY(admin.login("admin", "123456").ok());
    QCOMPARE(storage->findAdminById(1)->passwordAlgorithm, QStringLiteral("PBKDF2_SHA256"));
    QVERIFY(admin.listSupportTickets().ok());
    QVERIFY(admin.listAdmins().ok());
}

void SupportTicketFlowTests::mismatchedSchemaVersionsAreRejected()
{
    Fixture f; QVERIFY2(f.initialize(true), qPrintable(f.error));
    auto *storage = static_cast<Repository *>(f.repository.get());
    storage->close();
    // Simulate the alternate "version 2" layout from the reverted administrator branch.
    QVERIFY(f.sql("PRAGMA user_version = 2;"));
    QVERIFY(!storage->open(f.path(), &f.error));
    QVERIFY(f.error.contains(QStringLiteral("layout mismatch")));
    QVERIFY(!storage->supportsAdminAccounts() && !storage->supportsSupportTickets());
    QVERIFY(f.sql("PRAGMA user_version = 3; DROP TABLE admin_audit_logs;"));
    QVERIFY(!storage->open(f.path(), &f.error));
    QVERIFY(f.error.contains(QStringLiteral("extension schema")));
    QVERIFY(!storage->supportsAdminAccounts() && !storage->supportsSupportTickets());
}

void SupportTicketFlowTests::repairLifecycle()
{
    QFETCH(bool, sqlite);
    Fixture f; QVERIFY2(f.initialize(sqlite, true, true, true), qPrintable(f.error));
    const auto business = f.businessSnapshot();
    auto input = draft(); input.pileCode = "PILE-A-01"; input.faultType = QStringLiteral("充电中断");
    const auto created = f.service->createSupportTicket(f.token, toJson(input));
    QVERIFY2(created.ok(), qPrintable(created.message));
    const auto id = created.data.value("ticket").toObject().value("ticketId").toInteger();
    QCOMPARE(f.repository->findSupportTicket(id)->pileCode, input.pileCode);
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(input)).data, created.data);
    auto invalid = input; invalid.faultType = QStringLiteral("其他故障");
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(invalid)).code, ErrorCode::InvalidRequest);
    invalid.submissionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    invalid.pileCode = "MISSING";
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(invalid)).code, ErrorCode::NotFound);
    QCOMPARE(f.service->createSupportTicket("", toJson(input)).code, ErrorCode::InvalidSession);
    const auto other = f.service->loginUser({{"phone", "13800000005"}}).data.value("token").toString();
    QCOMPARE(f.service->getSupportTicket(other, {{"ticketId", id}}).code, ErrorCode::NotFound);
    AdminFacade admin(f.service.get()); QVERIFY(admin.login("admin", "123456").ok());
    QVERIFY(admin.updateSupportTicket({{"ticketId", id}, {"status", "IN_PROGRESS"}, {"reply", "checking"}}).ok());
    QCOMPARE(admin.updateSupportTicket({{"ticketId", id}, {"status", "RESOLVED"}, {"reply", ""}}).code,
             ErrorCode::InvalidRequest);
    const auto resolved = admin.updateSupportTicket({{"ticketId", id}, {"status", "RESOLVED"}, {"reply", "已检查设备"}});
    QVERIFY(resolved.ok());
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(input)).data, resolved.data);
    if (sqlite) {
        auto *storage = static_cast<Repository *>(f.repository.get());
        storage->close(); QVERIFY2(storage->open(f.path(), &f.error), qPrintable(f.error));
        QCOMPARE(f.service->getSupportTicket(f.token, {{"ticketId", id}}).data, resolved.data);
    }
    // Reporting and handling change no account, pile, or order state.
    QCOMPARE(f.businessSnapshot(), business);
    auto extraPile = f.repository->listPiles().first();
    extraPile.pileCode = "REPAIR-ONLY"; extraPile.status = PileStatus::Idle;
    extraPile = f.repository->createPile(extraPile);
    QVERIFY(extraPile.pileId > 0);
    auto extraReport = draft(); extraReport.pileCode = extraPile.pileCode; extraReport.faultType = "screen";
    QVERIFY(f.service->createSupportTicket(f.token, toJson(extraReport)).ok());
    QCOMPARE(f.repository->deletePile(extraPile.pileId), DeletePileResult::StorageError);
    extraPile.pileCode = "RENAMED";
    QVERIFY(!f.repository->updatePile(extraPile));
    auto user = f.repository->findUserById(1).value(); user.status = UserStatus::Frozen;
    QVERIFY(f.repository->updateUser(user));
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(input)).code, ErrorCode::Forbidden);
}

void SupportTicketFlowTests::repairMigrationPreservesExistingTickets()
{
    Fixture f; QVERIFY2(f.initialize(true), qPrintable(f.error));
    const auto created = f.service->createSupportTicket(f.token, toJson(draft()));
    QVERIFY(created.ok());
    auto repair = draft(); repair.pileCode = "PILE-A-01"; repair.faultType = "screen";
    QCOMPARE(f.service->createSupportTicket(f.token, toJson(repair)).message,
             QStringLiteral("REPAIR_TICKETS_MIGRATION_REQUIRED"));
    const auto business = f.businessSnapshot();
    auto *storage = static_cast<Repository *>(f.repository.get()); storage->close();
    QFile migration(QStringLiteral(CHARGING_REPAIR_MIGRATION_PATH)); QVERIFY(migration.open(QIODevice::ReadOnly));
    const auto sql = migration.readAll(); QVERIFY2(f.sql(sql), qPrintable(f.error));
    QVERIFY(!f.sql(sql)); // No accidental repeated migration.
    QVERIFY2(storage->open(f.path(), &f.error), qPrintable(f.error));
    QVERIFY(storage->supportsAdminAccounts() && storage->supportsRepairTickets());
    QCOMPARE(f.service->getSupportTicket(f.token, {{"ticketId", 1}}).data, created.data);
    QVERIFY(f.service->createSupportTicket(f.token, toJson(repair)).ok());
    QCOMPARE(f.businessSnapshot(), business);
}

QTEST_GUILESS_MAIN(SupportTicketFlowTests)
#include "support_ticket_flow_tests.moc"
