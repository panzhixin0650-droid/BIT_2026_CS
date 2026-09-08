#include "application/application_service.h"
#include "application/session_store.h"
#include "adapters/dashboard_exporter.h"
#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "admin_ui/admin_facade.h"
#include "persistence/in_memory_repository.h"
#include "transport/request_router.h"

#include "charging/protocol/protocol_constants.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace charging::server;
using namespace charging::protocol;

class ServerTests final : public QObject {
    Q_OBJECT

private slots:
    void pingReturnsUtcServerTime();
    void pingRejectsNonStringEcho();
    void routerPreservesRequestIdentity();
    void routerRejectsUnimplementedMessage();
    void dashboardExporterWritesAtomically();
    void loginCreatesAndAuthenticatesUser();
    void frozenUserCannotLogin();
    void stationsRequireSessionAndFilterByRegion();
    void profileAndRechargeUpdateRepository();
    void adminLoginAcceptsDemoCredentials();
    void adminLoginRejectsWrongPassword();
    void adminDashboardContainsExactRevenueRange();
    void adminDashboardAcceptsCustomDateRange();
    void adminListsAndCreatesStationsWithPiles();
    void adminDeletesOnlyStationsWithoutOrders();
    void adminManagesPileLifecycleSafely();
    void adminEditsPileMetadataSafely();
    void adminStationDisableEnforcesPileSafety();
    void adminCannotFreezeUserWithCurrentOrder();
    void adminAccountsSupportRolesScopesAndPasswordChanges();
    void adminPasswordsAllowSixDigitsAndEnforceBoundaries();
    void rolePermissionsAreEnforcedByTheService();
};

struct ServiceFixture {
    InMemoryRepository repository;
    SessionStore sessions;
    MockPile pileGateway;
    MockPredictionProvider predictions;
    ApplicationService service{&repository, &sessions, &pileGateway, &predictions};
};

void ServerTests::pingReturnsUtcServerTime()
{
    ServiceFixture fixture;
    const ServiceResult result = fixture.service.ping({{QStringLiteral("echo"), QStringLiteral("hello")}});

    QCOMPARE(result.code, ErrorCode::Ok);
    QCOMPARE(result.data.value(QStringLiteral("echo")).toString(), QStringLiteral("hello"));
    QVERIFY(result.data.value(QStringLiteral("serverTime")).toString().endsWith(QLatin1Char('Z')));
}

void ServerTests::pingRejectsNonStringEcho()
{
    ServiceFixture fixture;
    const ServiceResult result = fixture.service.ping({{QStringLiteral("echo"), 42}});

    QCOMPARE(result.code, ErrorCode::InvalidRequest);
    QCOMPARE(result.message, QStringLiteral("INVALID_REQUEST"));
}

void ServerTests::routerPreservesRequestIdentity()
{
    ServiceFixture fixture;
    RequestRouter router(&fixture.service);

    RequestEnvelope request;
    request.type = QString::fromLatin1(MessageType::SystemPing);
    request.requestId = QStringLiteral("req-scaffold-1");
    request.data = {{QStringLiteral("echo"), QStringLiteral("ok")}};

    const ResponseEnvelope response = router.route(request);
    QCOMPARE(response.version, request.version);
    QCOMPARE(response.type, request.type);
    QCOMPARE(response.requestId, request.requestId);
    QCOMPARE(response.code, ErrorCode::Ok);
}

void ServerTests::routerRejectsUnimplementedMessage()
{
    ServiceFixture fixture;
    RequestRouter router(&fixture.service);

    RequestEnvelope request;
    request.type = QStringLiteral("unknown.message");
    request.requestId = QStringLiteral("req-scaffold-2");
    request.data = {};

    const ResponseEnvelope response = router.route(request);
    QCOMPARE(response.code, ErrorCode::InvalidRequest);
    QCOMPARE(response.message, QStringLiteral("INVALID_REQUEST"));
}

void ServerTests::loginCreatesAndAuthenticatesUser()
{
    ServiceFixture fixture;
    const ServiceResult result = fixture.service.loginUser({
        {QStringLiteral("phone"), QStringLiteral("13900000099")},
    });

    QCOMPARE(result.code, ErrorCode::Ok);
    QCOMPARE(result.data.value(QStringLiteral("isNewUser")).toBool(), true);
    const QString token = result.data.value(QStringLiteral("token")).toString();
    QVERIFY(!token.isEmpty());
    QCOMPARE(result.data.value(QStringLiteral("user")).toObject()
                 .value(QStringLiteral("nickname")).toString(),
             QStringLiteral("用户0099"));
    QCOMPARE(fixture.service.getProfile(token).code, ErrorCode::Ok);
    QCOMPARE(fixture.service.logout(token).code, ErrorCode::Ok);
}

void ServerTests::frozenUserCannotLogin()
{
    ServiceFixture fixture;
    const ServiceResult result = fixture.service.loginUser({
        {QStringLiteral("phone"), QStringLiteral("13800000004")},
    });
    QCOMPARE(result.code, ErrorCode::Forbidden);
    QCOMPARE(result.message, QStringLiteral("FORBIDDEN"));
}

void ServerTests::stationsRequireSessionAndFilterByRegion()
{
    ServiceFixture fixture;
    QCOMPARE(fixture.service.listStations(QString{}, {}).code, ErrorCode::InvalidSession);

    const ServiceResult login = fixture.service.loginUser({
        {QStringLiteral("phone"), QStringLiteral("13800000001")},
    });
    const QString token = login.data.value(QStringLiteral("token")).toString();
    const ServiceResult stations = fixture.service.listStations(token, {
        {QStringLiteral("region"), QStringLiteral("浑南区")},
        {QStringLiteral("longitude"), 123.42},
        {QStringLiteral("latitude"), 41.70},
    });
    QCOMPARE(stations.code, ErrorCode::Ok);
    const QJsonArray items = stations.data.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 1);
    const QJsonObject station = items.first().toObject();
    QCOMPARE(station.value(QStringLiteral("stationId")).toInt(), 1);
    QCOMPARE(station.value(QStringLiteral("recommended")).toBool(), true);
    QVERIFY(station.value(QStringLiteral("distanceKm")).toDouble() > 0.0);
}

void ServerTests::profileAndRechargeUpdateRepository()
{
    ServiceFixture fixture;
    const ServiceResult login = fixture.service.loginUser({
        {QStringLiteral("phone"), QStringLiteral("13800000001")},
    });
    const QString token = login.data.value(QStringLiteral("token")).toString();

    const ServiceResult update = fixture.service.updateProfile(token, {
        {QStringLiteral("nickname"), QStringLiteral("新昵称")},
    });
    QCOMPARE(update.code, ErrorCode::Ok);
    QCOMPARE(update.data.value(QStringLiteral("user")).toObject()
                 .value(QStringLiteral("nickname")).toString(),
             QStringLiteral("新昵称"));

    const ServiceResult recharge = fixture.service.recharge(token, {
        {QStringLiteral("amountCents"), 500},
    });
    QCOMPARE(recharge.code, ErrorCode::Ok);
    QCOMPARE(recharge.data.value(QStringLiteral("balanceCents")).toInteger(), qint64{20500});
}

void ServerTests::adminLoginAcceptsDemoCredentials()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);

    const ServiceResult result = facade.login(QStringLiteral("admin"),
                                              QStringLiteral("123456"));
    QCOMPARE(result.code, ErrorCode::Ok);
    const QJsonObject admin = result.data.value(QStringLiteral("admin")).toObject();
    QCOMPARE(admin.value(QStringLiteral("adminId")).toInteger(), qint64{1});
    QCOMPARE(admin.value(QStringLiteral("displayName")).toString(),
             QStringLiteral("系统管理员"));
    QCOMPARE(admin.value(QStringLiteral("role")).toString(), QStringLiteral("SYS_ADMIN"));
}

void ServerTests::adminLoginRejectsWrongPassword()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);

    const ServiceResult result = facade.login(QStringLiteral("admin"),
                                              QStringLiteral("wrong-password"));
    QCOMPARE(result.code, ErrorCode::InvalidCredentials);
    QCOMPARE(result.message, QStringLiteral("INVALID_CREDENTIALS"));
}

void ServerTests::adminDashboardContainsExactRevenueRange()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    const ServiceResult sevenDays = facade.getDashboard(7);
    QCOMPARE(sevenDays.code, ErrorCode::Ok);
    QCOMPARE(sevenDays.data.value(QStringLiteral("revenuePoints")).toArray().size(), 7);
    QCOMPARE(sevenDays.data.value(QStringLiteral("stationCount")).toInt(), 3);
    QCOMPARE(sevenDays.data.value(QStringLiteral("pileCount")).toInt(), 6);

    const ServiceResult thirtyDays = facade.getDashboard(30);
    QCOMPARE(thirtyDays.code, ErrorCode::Ok);
    QCOMPARE(thirtyDays.data.value(QStringLiteral("revenuePoints")).toArray().size(), 30);
}

void ServerTests::adminDashboardAcceptsCustomDateRange()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());
    const QDate end = QDate::currentDate();
    const ServiceResult result = facade.getDashboard(end.addDays(-12), end);
    QCOMPARE(result.code, ErrorCode::Ok);
    QCOMPARE(result.data.value(QStringLiteral("revenuePoints")).toArray().size(), 13);
    QCOMPARE(facade.getDashboard(end, end.addDays(-1)).code, ErrorCode::InvalidRequest);
    QCOMPARE(facade.getDashboard(end.addDays(-366), end).code, ErrorCode::InvalidRequest);
}

void ServerTests::adminListsAndCreatesStationsWithPiles()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    QCOMPARE(facade.listStations().data.value(QStringLiteral("items")).toArray().size(), 3);
    QCOMPARE(facade.listPiles().data.value(QStringLiteral("items")).toArray().size(), 6);

    const ServiceResult created = facade.createStation({
        {QStringLiteral("name"), QStringLiteral("铁西测试充电站")},
        {QStringLiteral("region"), QStringLiteral("铁西区")},
        {QStringLiteral("address"), QStringLiteral("铁西区测试路1号")},
        {QStringLiteral("longitude"), 123.36},
        {QStringLiteral("latitude"), 41.80},
        {QStringLiteral("priceCentsPerKwh"), 130},
        {QStringLiteral("piles"), QJsonArray{
            QJsonObject{{QStringLiteral("pileCode"), QStringLiteral("PILE-TX-01")},
                        {QStringLiteral("pileType"), QStringLiteral("FAST")},
                        {QStringLiteral("ratedPowerKw"), 80.0}},
            QJsonObject{{QStringLiteral("pileCode"), QStringLiteral("PILE-TX-02")},
                        {QStringLiteral("pileType"), QStringLiteral("SLOW")},
                        {QStringLiteral("ratedPowerKw"), 11.0}},
        }},
    });
    QCOMPARE(created.code, ErrorCode::Ok);
    QCOMPARE(created.data.value(QStringLiteral("piles")).toArray().size(), 2);
    QCOMPARE(created.data.value(QStringLiteral("piles")).toArray().at(0).toObject()
                 .value(QStringLiteral("ratedPowerKw")).toDouble(), 80.0);
    QCOMPARE(facade.listStations().data.value(QStringLiteral("items")).toArray().size(), 4);
    QCOMPARE(facade.listPiles().data.value(QStringLiteral("items")).toArray().size(), 8);
}

void ServerTests::adminDeletesOnlyStationsWithoutOrders()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    const ServiceResult created = facade.createStation({
        {QStringLiteral("name"), QStringLiteral("可删除测试站")},
        {QStringLiteral("region"), QStringLiteral("铁西区")},
        {QStringLiteral("address"), QStringLiteral("铁西区删除测试路1号")},
        {QStringLiteral("longitude"), 123.36},
        {QStringLiteral("latitude"), 41.80},
        {QStringLiteral("priceCentsPerKwh"), 130},
        {QStringLiteral("piles"), QJsonArray{}},
    });
    QCOMPARE(created.code, ErrorCode::Ok);
    const qint64 stationId = created.data.value(QStringLiteral("station"))
                                 .toObject()
                                 .value(QStringLiteral("stationId"))
                                 .toInteger();

    const ServiceResult deleted = facade.deleteStation(stationId);
    QCOMPARE(deleted.code, ErrorCode::Ok);
    QCOMPARE(deleted.data.value(QStringLiteral("success")).toBool(), true);
    QCOMPARE(facade.listStations().data.value(QStringLiteral("items")).toArray().size(), 3);
    QCOMPARE(facade.listPiles().data.value(QStringLiteral("items")).toArray().size(), 6);

    const ServiceResult historyPreserved = facade.deleteStation(1);
    QCOMPARE(historyPreserved.code, ErrorCode::IllegalOrderState);
    QCOMPARE(historyPreserved.message, QStringLiteral("ILLEGAL_ORDER_STATE"));
    QCOMPARE(facade.listStations().data.value(QStringLiteral("items")).toArray().size(), 3);

    const ServiceResult missing = facade.deleteStation(99999);
    QCOMPARE(missing.code, ErrorCode::NotFound);
}

void ServerTests::adminManagesPileLifecycleSafely()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());
    const ServiceResult created = facade.createPile({
        {QStringLiteral("stationId"), 1},
        {QStringLiteral("pileCode"), QStringLiteral("PILE-NEW-01")},
        {QStringLiteral("pileType"), QStringLiteral("FAST")},
        {QStringLiteral("ratedPowerKw"), 60.0},
    });
    QCOMPARE(created.code, ErrorCode::Ok);
    QCOMPARE(facade.createPile({
        {QStringLiteral("stationId"), 1},
        {QStringLiteral("pileCode"), QStringLiteral("PILE-NEW-01")},
        {QStringLiteral("pileType"), QStringLiteral("FAST")},
        {QStringLiteral("ratedPowerKw"), 60.0},
    }).code, ErrorCode::InvalidRequest);
    const qint64 pileId = created.data.value(QStringLiteral("pile")).toObject()
                              .value(QStringLiteral("pileId")).toInteger();
    QCOMPARE(facade.setPileStatus(pileId, PileStatus::Offline).code, ErrorCode::Ok);
    QCOMPARE(facade.restartPile(pileId).code, ErrorCode::Ok);
    QCOMPARE(facade.setPileStatus(pileId, PileStatus::Fault).code, ErrorCode::Ok);
    QCOMPARE(facade.restartPile(pileId).code, ErrorCode::IllegalOrderState);
    QCOMPARE(facade.setPileStatus(pileId, PileStatus::Idle).code, ErrorCode::IllegalOrderState);
    QCOMPARE(facade.deletePile(pileId).code, ErrorCode::IllegalOrderState);
    const ServiceResult removable = facade.createPile({
        {QStringLiteral("stationId"), 1},
        {QStringLiteral("pileCode"), QStringLiteral("PILE-NEW-02")},
        {QStringLiteral("pileType"), QStringLiteral("SLOW")},
        {QStringLiteral("ratedPowerKw"), 7.0},
    });
    QCOMPARE(removable.code, ErrorCode::Ok);
    QCOMPARE(facade.deletePile(removable.data.value(QStringLiteral("pile")).toObject()
                                   .value(QStringLiteral("pileId")).toInteger()).code,
             ErrorCode::Ok);
}

void ServerTests::adminEditsPileMetadataSafely()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    QCOMPARE(facade.updatePile({
        {QStringLiteral("pileId"), 1},
        {QStringLiteral("pileCode"), QStringLiteral("PILE-A-01-EDITED")},
        {QStringLiteral("pileType"), QStringLiteral("SLOW")},
        {QStringLiteral("ratedPowerKw"), 11.0},
    }).code, ErrorCode::Ok);

    const QJsonArray piles = facade.listPiles().data.value(QStringLiteral("items")).toArray();
    const auto edited = std::find_if(piles.cbegin(), piles.cend(), [](const QJsonValue &value) {
        return value.toObject().value(QStringLiteral("pileId")).toInteger() == 1;
    });
    QVERIFY(edited != piles.cend());
    QCOMPARE(edited->toObject().value(QStringLiteral("pileCode")).toString(),
             QStringLiteral("PILE-A-01-EDITED"));
    QCOMPARE(edited->toObject().value(QStringLiteral("pileType")).toString(),
             QStringLiteral("SLOW"));
    QCOMPARE(edited->toObject().value(QStringLiteral("ratedPowerKw")).toDouble(), 11.0);

    QCOMPARE(facade.updatePile({
        {QStringLiteral("pileId"), 3},
        {QStringLiteral("pileCode"), QStringLiteral("PILE-A-01-EDITED")},
        {QStringLiteral("pileType"), QStringLiteral("FAST")},
        {QStringLiteral("ratedPowerKw"), 60.0},
    }).code, ErrorCode::InvalidRequest);

    QCOMPARE(facade.updatePile({
        {QStringLiteral("pileId"), 1},
        {QStringLiteral("pileCode"), QStringLiteral("PILE-A-01-EDITED")},
        {QStringLiteral("pileType"), QStringLiteral("FAST")},
        {QStringLiteral("ratedPowerKw"), 60.0},
    }).code, ErrorCode::Ok);
}

void ServerTests::adminStationDisableEnforcesPileSafety()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    // Station 1 owns a charging pile in the demo data, so disabling it must
    // be rejected without changing either the station or its piles.
    const ServiceResult busy = facade.setStationStatus(1, StationStatus::Disabled);
    QCOMPARE(busy.code, ErrorCode::IllegalOrderState);
    QCOMPARE(busy.message, QStringLiteral("STATION_HAS_ACTIVE_PILES"));
    QCOMPARE(facade.listStations().data.value(QStringLiteral("items")).toArray()
                 .at(0).toObject().value(QStringLiteral("status")).toString(),
             QStringLiteral("ACTIVE"));

    // Station 2 has one idle and one faulted pile.  Only the idle online pile
    // is taken offline; the fault state is preserved for maintenance.
    const ServiceResult disabled = facade.setStationStatus(2, StationStatus::Disabled);
    QCOMPARE(disabled.code, ErrorCode::Ok);
    QCOMPARE(disabled.data.value(QStringLiteral("station")).toObject()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("DISABLED"));
    const QJsonArray piles = facade.listPiles(2).data.value(QStringLiteral("items")).toArray();
    QCOMPARE(piles.size(), 2);
    for (const QJsonValue &value : piles) {
        const QJsonObject pile = value.toObject();
        if (pile.value(QStringLiteral("pileId")).toInteger() == 3) {
            QCOMPARE(pile.value(QStringLiteral("status")).toString(), QStringLiteral("OFFLINE"));
        } else if (pile.value(QStringLiteral("pileId")).toInteger() == 4) {
            QCOMPARE(pile.value(QStringLiteral("status")).toString(), QStringLiteral("FAULT"));
        }
    }

    // Enabling a station does not silently power hardware back on; operators
    // must explicitly bring an offline pile online.
    QCOMPARE(facade.setStationStatus(2, StationStatus::Active).code, ErrorCode::Ok);
    const QJsonArray afterEnable = facade.listPiles(2).data.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : afterEnable) {
        const QJsonObject pile = value.toObject();
        if (pile.value(QStringLiteral("pileId")).toInteger() == 3) {
            QCOMPARE(pile.value(QStringLiteral("status")).toString(), QStringLiteral("OFFLINE"));
        }
    }
}

void ServerTests::adminCannotFreezeUserWithCurrentOrder()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    const ServiceResult result = facade.setUserStatus(1, UserStatus::Frozen);
    QCOMPARE(result.code, ErrorCode::CurrentOrderExists);
    QCOMPARE(result.message, QStringLiteral("CURRENT_ORDER_EXISTS"));
}

void ServerTests::adminAccountsSupportRolesScopesAndPasswordChanges()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    const ServiceResult created = facade.createAdmin({
        {QStringLiteral("username"), QStringLiteral("second_admin")},
        {QStringLiteral("initialPassword"), QStringLiteral("password8")},
        {QStringLiteral("displayName"), QStringLiteral("第二管理员")},
        {QStringLiteral("role"), QStringLiteral("SYS_ADMIN")},
        {QStringLiteral("stationIds"), QJsonArray{}},
    });
    QCOMPARE(created.code, ErrorCode::Ok);
    const QJsonObject createdAdmin = created.data.value(QStringLiteral("admin")).toObject();
    QVERIFY(createdAdmin.value(QStringLiteral("mustChangePassword")).toBool());
    QVERIFY(!createdAdmin.contains(QStringLiteral("passwordHash")));
    QCOMPARE(facade.listAdmins().data.value(QStringLiteral("items")).toArray().size(), 2);
    QCOMPARE(facade.createAdmin({
        {QStringLiteral("username"), QStringLiteral("SECOND_ADMIN")},
        {QStringLiteral("initialPassword"), QStringLiteral("password8")},
        {QStringLiteral("displayName"), QStringLiteral("重复账号")},
        {QStringLiteral("role"), QStringLiteral("SYS_ADMIN")},
        {QStringLiteral("stationIds"), QJsonArray{}},
    }).message, QStringLiteral("DUPLICATE_USERNAME"));

    QCOMPARE(facade.updateAdmin({
        {QStringLiteral("adminId"), 1},
        {QStringLiteral("displayName"), QStringLiteral("系统管理员")},
        {QStringLiteral("role"), QStringLiteral("SYS_ADMIN")},
        {QStringLiteral("status"), QStringLiteral("DISABLED")},
        {QStringLiteral("reason"), QStringLiteral("测试自我停用保护")},
        {QStringLiteral("stationIds"), QJsonArray{}},
    }).message, QStringLiteral("CANNOT_DISABLE_SELF"));
    QCOMPARE(facade.updateAdmin({
        {QStringLiteral("adminId"), 1},
        {QStringLiteral("displayName"), QStringLiteral("系统管理员")},
        {QStringLiteral("role"), QStringLiteral("USER_ADMIN")},
        {QStringLiteral("status"), QStringLiteral("ACTIVE")},
        {QStringLiteral("reason"), QStringLiteral("测试自我改角色保护")},
        {QStringLiteral("stationIds"), QJsonArray{}},
    }).message, QStringLiteral("CANNOT_CHANGE_OWN_ROLE"));

    const qint64 secondAdminId = createdAdmin.value(QStringLiteral("adminId")).toInteger();
    QCOMPARE(facade.updateAdmin({
        {QStringLiteral("adminId"), secondAdminId},
        {QStringLiteral("displayName"), QStringLiteral("第二管理员")},
        {QStringLiteral("role"), QStringLiteral("SYS_ADMIN")},
        {QStringLiteral("status"), QStringLiteral("DISABLED")},
        {QStringLiteral("reason"), QStringLiteral("测试停用登录保护")},
        {QStringLiteral("stationIds"), QJsonArray{}},
    }).code, ErrorCode::Ok);
    facade.logout();
    QCOMPARE(facade.login(QStringLiteral("second_admin"),
                          QStringLiteral("password8")).message,
             QStringLiteral("PRINCIPAL_DISABLED"));
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());

    QCOMPARE(facade.changePassword(QStringLiteral("wrong"),
                                   QStringLiteral("newpassword8")).message,
             QStringLiteral("INVALID_CREDENTIALS"));
    QCOMPARE(facade.changePassword(QStringLiteral("123456"),
                                   QStringLiteral("newpassword8")).code,
             ErrorCode::Ok);
    QCOMPARE(facade.currentAdmin().code, ErrorCode::InvalidSession);
    QCOMPARE(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).code,
             ErrorCode::InvalidCredentials);
    QCOMPARE(facade.login(QStringLiteral("admin"), QStringLiteral("newpassword8")).code,
             ErrorCode::Ok);
}

void ServerTests::adminPasswordsAllowSixDigitsAndEnforceBoundaries()
{
    ServiceFixture fixture;
    AdminFacade system(&fixture.service), account(&fixture.service);
    QVERIFY(system.login("admin", "123456").ok());
    QJsonObject input{{"username", "six_digit_admin"}, {"displayName", "Six digit account"},
                      {"initialPassword", "12345"}, {"role", "USER_ADMIN"}, {"stationIds", QJsonArray{}}};
    QCOMPARE(system.createAdmin(input).code, ErrorCode::InvalidRequest);
    input["initialPassword"] = QString(129, '1');
    QCOMPARE(system.createAdmin(input).code, ErrorCode::InvalidRequest);
    input["initialPassword"] = "123456";
    QVERIFY(system.createAdmin(input).ok());
    QVERIFY(account.login("six_digit_admin", "123456").ok());
    QCOMPARE(account.changePassword("123456", "12345").code, ErrorCode::InvalidRequest);
    QCOMPARE(account.changePassword("123456", QString(129, '2')).code, ErrorCode::InvalidRequest);
    QCOMPARE(account.changePassword("123456", "123456").code, ErrorCode::InvalidRequest);
    QCOMPARE(account.changePassword("wrong1", "654321").code, ErrorCode::InvalidCredentials);
    QVERIFY(account.changePassword("123456", "654321").ok());
    QCOMPARE(account.currentAdmin().code, ErrorCode::InvalidSession);
    QCOMPARE(account.login("six_digit_admin", "123456").code, ErrorCode::InvalidCredentials);
    QVERIFY(account.login("six_digit_admin", "654321").ok());
    QVERIFY(account.changePassword("654321", QString(128, '3')).ok());
    QVERIFY(account.login("six_digit_admin", QString(128, '3')).ok());
}

void ServerTests::rolePermissionsAreEnforcedByTheService()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);
    QVERIFY(facade.login(QStringLiteral("admin"), QStringLiteral("123456")).ok());
    QCOMPARE(facade.createAdmin({
        {QStringLiteral("username"), QStringLiteral("station_ops")},
        {QStringLiteral("initialPassword"), QStringLiteral("stationpass")},
        {QStringLiteral("displayName"), QStringLiteral("和平站点管理员")},
        {QStringLiteral("role"), QStringLiteral("STATION_ADMIN")},
        {QStringLiteral("stationIds"), QJsonArray{2}},
    }).code, ErrorCode::Ok);
    QCOMPARE(facade.createAdmin({
        {QStringLiteral("username"), QStringLiteral("user_ops")},
        {QStringLiteral("initialPassword"), QStringLiteral("userpass88")},
        {QStringLiteral("displayName"), QStringLiteral("用户管理员")},
        {QStringLiteral("role"), QStringLiteral("USER_ADMIN")},
        {QStringLiteral("stationIds"), QJsonArray{}},
    }).code, ErrorCode::Ok);

    facade.logout();
    QVERIFY(facade.login(QStringLiteral("station_ops"),
                         QStringLiteral("stationpass")).ok());
    QCOMPARE(facade.listStations().message, QStringLiteral("PASSWORD_CHANGE_REQUIRED"));
    QCOMPARE(facade.changePassword(QStringLiteral("stationpass"),
                                   QStringLiteral("stationpass2")).code,
             ErrorCode::Ok);
    QVERIFY(facade.login(QStringLiteral("station_ops"),
                         QStringLiteral("stationpass2")).ok());
    const ServiceResult stations = facade.listStations();
    QCOMPARE(stations.code, ErrorCode::Ok);
    const QJsonArray stationItems = stations.data.value(QStringLiteral("items")).toArray();
    QCOMPARE(stationItems.size(), 1);
    QCOMPARE(stationItems.first().toObject().value(QStringLiteral("stationId")).toInteger(),
             qint64{2});
    QCOMPARE(facade.listPiles(1).message, QStringLiteral("STATION_SCOPE_FORBIDDEN"));
    QCOMPARE(facade.setPileStatus(1, PileStatus::Offline).message,
             QStringLiteral("STATION_SCOPE_FORBIDDEN"));
    QCOMPARE(facade.listUsers().message, QStringLiteral("ROLE_FORBIDDEN"));
    QCOMPARE(facade.listAdmins().message, QStringLiteral("ROLE_FORBIDDEN"));
    QCOMPARE(facade.getDashboard(7).data.value(QStringLiteral("stationCount")).toInt(), 1);

    facade.logout();
    QVERIFY(facade.login(QStringLiteral("user_ops"), QStringLiteral("userpass88")).ok());
    QCOMPARE(facade.changePassword(QStringLiteral("userpass88"),
                                   QStringLiteral("userpass99")).code,
             ErrorCode::Ok);
    QVERIFY(facade.login(QStringLiteral("user_ops"), QStringLiteral("userpass99")).ok());
    QCOMPARE(facade.listUsers().code, ErrorCode::Ok);
    QCOMPARE(facade.setUserStatus(4, UserStatus::Active).code, ErrorCode::Ok);
    QCOMPARE(facade.listOrders().code, ErrorCode::Ok);
    QCOMPARE(facade.listStations().message, QStringLiteral("ROLE_FORBIDDEN"));
    QCOMPARE(facade.listAdmins().message, QStringLiteral("ROLE_FORBIDDEN"));
}

void ServerTests::dashboardExporterWritesAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DashboardExporter exporter;
    QString error;
    const QString path = directory.filePath(QStringLiteral("dashboard.json"));
    QVERIFY2(exporter.exportSnapshot(path,
                                     {{QStringLiteral("schemaVersion"), 1}},
                                     &error),
             qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("schemaVersion")).toInt(), 1);
}

QTEST_GUILESS_MAIN(ServerTests)

#include "server_tests.moc"
