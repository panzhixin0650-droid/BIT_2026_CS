#include "persistence/repository.h"

#include "charging/protocol/dto.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace charging::server {
namespace {

using namespace charging::protocol;

bool runSql(const QString &databasePath,
            const QByteArray &sql,
            QString *error = nullptr)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral(CHARGING_SQLITE3_EXECUTABLE),
                  {QStringLiteral("-batch"), QStringLiteral("-bail"), databasePath});
    if (!process.waitForStarted()) {
        if (error != nullptr) {
            *error = process.errorString();
        }
        return false;
    }
    process.write(sql);
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        if (error != nullptr) {
            *error = QStringLiteral("sqlite3 timed out");
        }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            *error = QString::fromUtf8(process.readAll());
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool runSqlFile(const QString &databasePath,
                const QString &sqlPath,
                QString *error = nullptr)
{
    QFile input(sqlPath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = input.errorString();
        }
        return false;
    }
    return runSql(databasePath, input.readAll(), error);
}

bool initializeDemoDatabase(const QString &databasePath, QString *error = nullptr)
{
    return runSqlFile(databasePath,
                      QStringLiteral(CHARGING_DATABASE_MIGRATION_PATH), error)
        && runSqlFile(databasePath,
                      QStringLiteral(CHARGING_DATABASE_SEED_PATH), error);
}

template<typename Container, typename Predicate>
bool contains(const Container &items, Predicate predicate)
{
    return std::find_if(items.cbegin(), items.cend(), predicate) != items.cend();
}

}  // namespace

class RepositoryTests final : public QObject {
    Q_OBJECT

private slots:
    void rejectsMissingAndWrongSchema();
    void readsSeedAndDerivedFields();
    void writesPersistAcrossReopen();
    void deletesOnlyStationsWithoutOrders();
    void stationCreationRollsBackCompletely();
};

void RepositoryTests::rejectsMissingAndWrongSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString missingPath = directory.filePath(QStringLiteral("missing.db"));
    Repository repository(QStringLiteral("repository-test-invalid"));
    QString error;
    QVERIFY(!repository.open(missingPath, &error));
    QVERIFY(!repository.lastOperationSucceeded());
    QVERIFY(!QFileInfo::exists(missingPath));
    QVERIFY(error.contains(QStringLiteral("does not exist")));

    const QString emptyPath = directory.filePath(QStringLiteral("empty.db"));
    QFile emptyDatabase(emptyPath);
    QVERIFY(emptyDatabase.open(QIODevice::WriteOnly));
    emptyDatabase.close();

    QVERIFY(!repository.open(emptyPath, &error));
    QVERIFY(!repository.lastOperationSucceeded());
    QVERIFY(error.contains(QStringLiteral("schema version")));
}

void RepositoryTests::readsSeedAndDerivedFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("demo.db"));
    QString error;
    QVERIFY2(initializeDemoDatabase(databasePath, &error), qPrintable(error));

    Repository repository(QStringLiteral("repository-test-read"));
    QVERIFY2(repository.open(databasePath, &error), qPrintable(error));

    const auto admin = repository.findAdminByUsername(QStringLiteral("admin"));
    QVERIFY(repository.lastOperationSucceeded());
    QVERIFY(admin.has_value());
    QCOMPARE(admin->displayName, QStringLiteral("演示管理员"));

    const auto user = repository.findUserByPhone(QStringLiteral("13800000001"));
    QVERIFY(user.has_value());
    QCOMPARE(user->balanceCents, qint64{20000});
    QVERIFY(user->status == UserStatus::Active);
    QCOMPARE(repository.listUsers().size(), 5);

    const QList<StationDto> activeStations = repository.listActiveStations();
    QCOMPARE(activeStations.size(), 2);
    const auto station = repository.findStationById(1);
    QVERIFY(station.has_value());
    QCOMPARE(station->totalPileCount, qint64{2});
    QCOMPARE(station->availablePileCount, qint64{1});
    QCOMPARE(station->onlineRatePercent, 100.0);

    const QList<PileDto> piles = repository.listPiles();
    QCOMPARE(piles.size(), 12);
    const auto pileA01 = std::find_if(
        piles.cbegin(), piles.cend(), [](const PileDto &pile) {
            return pile.pileCode == QStringLiteral("PILE-A-01");
        });
    QVERIFY(pileA01 != piles.cend());
    QCOMPARE(pileA01->chargeCount, qint64{4});
    QCOMPARE(pileA01->totalChargeSeconds, qint64{14400});

    const QList<OrderDto> orders = repository.listOrders();
    QCOMPARE(orders.size(), 12);
    QVERIFY(contains(orders, [](const OrderDto &order) {
        return order.orderId == 101 && order.stationId == 1
            && order.stationName == QStringLiteral("浑南演示充电站")
            && order.pileCode == QStringLiteral("PILE-A-01");
    }));
    QVERIFY(repository.lastOperationSucceeded());
}

void RepositoryTests::writesPersistAcrossReopen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("demo.db"));
    QString error;
    QVERIFY2(initializeDemoDatabase(databasePath, &error), qPrintable(error));

    qint64 userId = 0;
    qint64 stationId = 0;
    {
        Repository repository(QStringLiteral("repository-test-write"));
        QVERIFY2(repository.open(databasePath, &error), qPrintable(error));

        UserDto user = repository.createUser(
            QStringLiteral("13900000099"), QStringLiteral("用户0099"),
            QStringLiteral("2026-09-03T00:00:00Z"));
        QVERIFY(repository.lastOperationSucceeded());
        QVERIFY(user.userId > 0);
        userId = user.userId;
        user.nickname = QStringLiteral("持久化用户");
        QVERIFY(repository.updateUser(user));

        const auto recharged = repository.addUserBalance(userId, 500);
        QVERIFY(repository.lastOperationSucceeded());
        QVERIFY(recharged.has_value());
        QCOMPARE(recharged->balanceCents, qint64{500});

        StationDto station;
        station.name = QStringLiteral("持久化测试站");
        station.region = QStringLiteral("浑南区");
        station.address = QStringLiteral("测试路1号");
        station.longitude = 123.45;
        station.latitude = 41.72;
        station.priceCentsPerKwh = 130;
        station = repository.createStation(station, 2);
        QVERIFY(repository.lastOperationSucceeded());
        QVERIFY(station.stationId > 0);
        stationId = station.stationId;
        QCOMPARE(repository.listPilesByStationId(stationId).size(), 2);
    }

    Repository reopened(QStringLiteral("repository-test-reopen"));
    QVERIFY2(reopened.open(databasePath, &error), qPrintable(error));
    const auto user = reopened.findUserById(userId);
    QVERIFY(user.has_value());
    QCOMPARE(user->nickname, QStringLiteral("持久化用户"));
    QCOMPARE(user->balanceCents, qint64{500});
    const auto station = reopened.findStationById(stationId);
    QVERIFY(station.has_value());
    QCOMPARE(station->totalPileCount, qint64{2});
    QCOMPARE(station->availablePileCount, qint64{2});
}

void RepositoryTests::deletesOnlyStationsWithoutOrders()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("demo.db"));
    QString error;
    QVERIFY2(initializeDemoDatabase(databasePath, &error), qPrintable(error));

    qint64 stationId = 0;
    {
        Repository repository(QStringLiteral("repository-test-delete"));
        QVERIFY2(repository.open(databasePath, &error), qPrintable(error));

        QVERIFY(repository.deleteStation(1) == DeleteStationResult::HasOrders);
        QVERIFY(repository.lastOperationSucceeded());
        QVERIFY(repository.findStationById(1).has_value());
        QCOMPARE(repository.listPilesByStationId(1).size(), 2);

        StationDto station;
        station.name = QStringLiteral("可删除测试站");
        station.region = QStringLiteral("浑南区");
        station.address = QStringLiteral("删除测试路1号");
        station.longitude = 123.45;
        station.latitude = 41.72;
        station.priceCentsPerKwh = 130;
        station = repository.createStation(station, 2);
        QVERIFY(station.stationId > 0);
        stationId = station.stationId;

        QVERIFY(repository.deleteStation(stationId)
                == DeleteStationResult::Deleted);
        QVERIFY(repository.lastOperationSucceeded());
        QVERIFY(!repository.findStationById(stationId).has_value());
        QVERIFY(repository.lastOperationSucceeded());
        QVERIFY(repository.listPilesByStationId(stationId).isEmpty());
        QVERIFY(repository.deleteStation(99999)
                == DeleteStationResult::NotFound);
    }

    Repository reopened(QStringLiteral("repository-test-delete-reopen"));
    QVERIFY2(reopened.open(databasePath, &error), qPrintable(error));
    QVERIFY(!reopened.findStationById(stationId).has_value());
    QCOMPARE(reopened.listStations().size(), 3);
    QCOMPARE(reopened.listPiles().size(), 12);
}

void RepositoryTests::stationCreationRollsBackCompletely()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("demo.db"));
    QString error;
    QVERIFY2(initializeDemoDatabase(databasePath, &error), qPrintable(error));
    QVERIFY2(runSql(databasePath, QByteArrayLiteral(
        "PRAGMA foreign_keys = ON;"
        "INSERT INTO charging_piles "
        "(pile_id, station_id, pile_code, pile_type, rated_power_kw, status) "
        "VALUES (99, 1, 'PILE-004-02', 'FAST', 60.0, 'IDLE');"), &error),
        qPrintable(error));

    Repository repository(QStringLiteral("repository-test-rollback"));
    QVERIFY2(repository.open(databasePath, &error), qPrintable(error));

    StationDto station;
    station.name = QStringLiteral("应回滚的站点");
    station.region = QStringLiteral("浑南区");
    station.address = QStringLiteral("回滚路1号");
    station.longitude = 123.45;
    station.latitude = 41.72;
    station.priceCentsPerKwh = 130;
    station = repository.createStation(station, 2);
    QCOMPARE(station.stationId, qint64{0});
    QVERIFY(!repository.lastOperationSucceeded());

    const QList<StationDto> stations = repository.listStations();
    QVERIFY(repository.lastOperationSucceeded());
    QCOMPARE(stations.size(), 3);
    QVERIFY(!contains(stations, [](const StationDto &item) {
        return item.stationId == 4;
    }));
    const QList<PileDto> piles = repository.listPiles();
    QVERIFY(!contains(piles, [](const PileDto &pile) {
        return pile.pileCode == QStringLiteral("PILE-004-01");
    }));
}

}  // namespace charging::server

QTEST_GUILESS_MAIN(charging::server::RepositoryTests)

#include "repository_tests.moc"
