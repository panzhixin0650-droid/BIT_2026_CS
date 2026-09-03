#include "application/application_service.h"
#include "application/session_store.h"
#include "adapters/dashboard_exporter.h"
#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "admin_ui/admin_facade.h"
#include "persistence/in_memory_repository.h"
#include "transport/request_router.h"

#include "charging/protocol/protocol_constants.h"

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
    void adminListsAndCreatesStationsWithPiles();
    void adminRestartsFaultPile();
    void adminCannotFreezeUserWithCurrentOrder();
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
        {QStringLiteral("phone"), QStringLiteral("13800000005")},
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
    QCOMPARE(result.data.value(QStringLiteral("adminId")).toInteger(), qint64{1});
    QCOMPARE(result.data.value(QStringLiteral("displayName")).toString(),
             QStringLiteral("系统管理员"));
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

    const ServiceResult sevenDays = facade.getDashboard(7);
    QCOMPARE(sevenDays.code, ErrorCode::Ok);
    QCOMPARE(sevenDays.data.value(QStringLiteral("revenuePoints")).toArray().size(), 7);
    QCOMPARE(sevenDays.data.value(QStringLiteral("stationCount")).toInt(), 3);
    QCOMPARE(sevenDays.data.value(QStringLiteral("pileCount")).toInt(), 6);

    const ServiceResult thirtyDays = facade.getDashboard(30);
    QCOMPARE(thirtyDays.code, ErrorCode::Ok);
    QCOMPARE(thirtyDays.data.value(QStringLiteral("revenuePoints")).toArray().size(), 30);
}

void ServerTests::adminListsAndCreatesStationsWithPiles()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);

    QCOMPARE(facade.listStations().data.value(QStringLiteral("items")).toArray().size(), 3);
    QCOMPARE(facade.listPiles().data.value(QStringLiteral("items")).toArray().size(), 6);

    const ServiceResult created = facade.createStation({
        {QStringLiteral("name"), QStringLiteral("铁西测试充电站")},
        {QStringLiteral("region"), QStringLiteral("铁西区")},
        {QStringLiteral("address"), QStringLiteral("铁西区测试路1号")},
        {QStringLiteral("longitude"), 123.36},
        {QStringLiteral("latitude"), 41.80},
        {QStringLiteral("priceCentsPerKwh"), 130},
        {QStringLiteral("pileCount"), 2},
    });
    QCOMPARE(created.code, ErrorCode::Ok);
    QCOMPARE(created.data.value(QStringLiteral("piles")).toArray().size(), 2);
    QCOMPARE(facade.listStations().data.value(QStringLiteral("items")).toArray().size(), 4);
    QCOMPARE(facade.listPiles().data.value(QStringLiteral("items")).toArray().size(), 8);
}

void ServerTests::adminRestartsFaultPile()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);

    const ServiceResult result = facade.restartPile(4);
    QCOMPARE(result.code, ErrorCode::Ok);
    QCOMPARE(result.data.value(QStringLiteral("pile")).toObject()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("IDLE"));
}

void ServerTests::adminCannotFreezeUserWithCurrentOrder()
{
    ServiceFixture fixture;
    AdminFacade facade(&fixture.service);

    const ServiceResult result = facade.setUserStatus(1, UserStatus::Frozen);
    QCOMPARE(result.code, ErrorCode::CurrentOrderExists);
    QCOMPARE(result.message, QStringLiteral("CURRENT_ORDER_EXISTS"));
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
