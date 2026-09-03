#include "repository.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace charging::server {
namespace {

using namespace charging::protocol;

constexpr int kExpectedSchemaVersion = 1;

bool parseUserStatus(const QString &text, UserStatus *status)
{
    if (text == QStringLiteral("ACTIVE")) {
        *status = UserStatus::Active;
        return true;
    }
    if (text == QStringLiteral("FROZEN")) {
        *status = UserStatus::Frozen;
        return true;
    }
    return false;
}

bool parseStationStatus(const QString &text, StationStatus *status)
{
    if (text == QStringLiteral("ACTIVE")) {
        *status = StationStatus::Active;
        return true;
    }
    if (text == QStringLiteral("DISABLED")) {
        *status = StationStatus::Disabled;
        return true;
    }
    return false;
}

bool parsePileType(const QString &text, PileType *type)
{
    if (text == QStringLiteral("FAST")) {
        *type = PileType::Fast;
        return true;
    }
    if (text == QStringLiteral("SLOW")) {
        *type = PileType::Slow;
        return true;
    }
    return false;
}

bool parsePileStatus(const QString &text, PileStatus *status)
{
    if (text == QStringLiteral("IDLE")) {
        *status = PileStatus::Idle;
        return true;
    }
    if (text == QStringLiteral("RESERVED")) {
        *status = PileStatus::Reserved;
        return true;
    }
    if (text == QStringLiteral("CHARGING")) {
        *status = PileStatus::Charging;
        return true;
    }
    if (text == QStringLiteral("FAULT")) {
        *status = PileStatus::Fault;
        return true;
    }
    if (text == QStringLiteral("OFFLINE")) {
        *status = PileStatus::Offline;
        return true;
    }
    return false;
}

bool parseOrderMode(const QString &text, OrderMode *mode)
{
    if (text == QStringLiteral("RESERVATION")) {
        *mode = OrderMode::Reservation;
        return true;
    }
    if (text == QStringLiteral("DIRECT")) {
        *mode = OrderMode::Direct;
        return true;
    }
    return false;
}

bool parseOrderStatus(const QString &text, OrderStatus *status)
{
    if (text == QStringLiteral("RESERVED")) {
        *status = OrderStatus::Reserved;
        return true;
    }
    if (text == QStringLiteral("CHARGING")) {
        *status = OrderStatus::Charging;
        return true;
    }
    if (text == QStringLiteral("PENDING_PAYMENT")) {
        *status = OrderStatus::PendingPayment;
        return true;
    }
    if (text == QStringLiteral("COMPLETED")) {
        *status = OrderStatus::Completed;
        return true;
    }
    if (text == QStringLiteral("CANCELLED")) {
        *status = OrderStatus::Cancelled;
        return true;
    }
    return false;
}

bool readUser(const QSqlQuery &query, UserDto *user)
{
    user->userId = query.value(0).toLongLong();
    user->phone = query.value(1).toString();
    user->nickname = query.value(2).toString();
    user->balanceCents = query.value(3).toLongLong();
    user->createdAt = query.value(5).toString();
    return parseUserStatus(query.value(4).toString(), &user->status);
}

bool readStation(const QSqlQuery &query, StationDto *station)
{
    station->stationId = query.value(0).toLongLong();
    station->name = query.value(1).toString();
    station->region = query.value(2).toString();
    station->address = query.value(3).toString();
    station->longitude = query.value(4).toDouble();
    station->latitude = query.value(5).toDouble();
    station->priceCentsPerKwh = query.value(6).toLongLong();
    station->totalPileCount = query.value(8).toLongLong();
    station->availablePileCount = query.value(9).toLongLong();
    station->onlineRatePercent = query.value(10).toDouble();
    station->distanceKm.reset();
    station->predictedCongestion.reset();
    station->recommended = false;
    return parseStationStatus(query.value(7).toString(), &station->status);
}

bool readPile(const QSqlQuery &query, PileDto *pile)
{
    pile->pileId = query.value(0).toLongLong();
    pile->stationId = query.value(1).toLongLong();
    pile->pileCode = query.value(2).toString();
    pile->ratedPowerKw = query.value(4).toDouble();
    pile->chargeCount = query.value(6).toLongLong();
    pile->totalChargeSeconds = query.value(7).toLongLong();
    return parsePileType(query.value(3).toString(), &pile->pileType)
        && parsePileStatus(query.value(5).toString(), &pile->status);
}

std::optional<QString> optionalString(const QVariant &value)
{
    return value.isNull() ? std::nullopt
                          : std::optional<QString>(value.toString());
}

bool readOrder(const QSqlQuery &query, OrderDto *order)
{
    order->orderId = query.value(0).toLongLong();
    order->orderNo = query.value(1).toString();
    order->createdAt = query.value(2).toString();
    order->userId = query.value(3).toLongLong();
    order->stationId = query.value(4).toLongLong();
    order->stationName = query.value(5).toString();
    order->pileId = query.value(6).toLongLong();
    order->pileCode = query.value(7).toString();
    order->reservedAt = optionalString(query.value(10));
    order->startedAt = optionalString(query.value(11));
    order->endedAt = optionalString(query.value(12));
    order->paidAt = optionalString(query.value(13));
    order->durationSeconds = query.value(14).toLongLong();
    order->energyWh = query.value(15).toLongLong();
    order->unitPriceCentsPerKwh = query.value(16).isNull()
        ? std::nullopt
        : std::optional<qint64>(query.value(16).toLongLong());
    order->amountCents = query.value(17).toLongLong();
    return parseOrderMode(query.value(8).toString(), &order->mode)
        && parseOrderStatus(query.value(9).toString(), &order->status);
}

QString userSelectSql(const QString &whereClause = {})
{
    return QStringLiteral(
               "SELECT user_id, phone, nickname, balance_cents, status, created_at "
               "FROM users ")
        + whereClause;
}

QString stationSelectSql(const QString &whereClause = {})
{
    return QStringLiteral(
               "SELECT s.station_id, s.name, s.region, s.address, "
               "s.longitude, s.latitude, s.price_cents_per_kwh, s.status, "
               "COUNT(p.pile_id) AS total_pile_count, "
               "SUM(CASE WHEN s.status = 'ACTIVE' AND p.status = 'IDLE' "
               "THEN 1 ELSE 0 END) AS available_pile_count, "
               "CASE WHEN COUNT(p.pile_id) = 0 THEN 0.0 "
               "ELSE 100.0 * SUM(CASE WHEN p.pile_id IS NOT NULL "
               "AND p.status <> 'OFFLINE' THEN 1 ELSE 0 END) "
               "/ COUNT(p.pile_id) END AS online_rate_percent "
               "FROM charging_stations AS s "
               "LEFT JOIN charging_piles AS p ON p.station_id = s.station_id ")
        + whereClause
        + QStringLiteral(
              " GROUP BY s.station_id, s.name, s.region, s.address, "
              "s.longitude, s.latitude, s.price_cents_per_kwh, s.status "
              "ORDER BY s.station_id");
}

QString pileSelectSql(const QString &whereClause = {})
{
    return QStringLiteral(
               "SELECT p.pile_id, p.station_id, p.pile_code, p.pile_type, "
               "p.rated_power_kw, p.status, COUNT(o.order_id) AS charge_count, "
               "COALESCE(SUM(o.duration_seconds), 0) AS total_charge_seconds "
               "FROM charging_piles AS p "
               "LEFT JOIN charging_orders AS o ON o.pile_id = p.pile_id "
               "AND o.status IN ('PENDING_PAYMENT', 'COMPLETED') ")
        + whereClause
        + QStringLiteral(
              " GROUP BY p.pile_id, p.station_id, p.pile_code, p.pile_type, "
              "p.rated_power_kw, p.status ORDER BY p.pile_id");
}

QString orderSelectSql()
{
    return QStringLiteral(
        "SELECT o.order_id, o.order_no, o.created_at, o.user_id, "
        "s.station_id, s.name, p.pile_id, p.pile_code, o.mode, o.status, "
        "o.reserved_at, o.started_at, o.ended_at, o.paid_at, "
        "o.duration_seconds, o.energy_wh, o.unit_price_cents_per_kwh, "
        "o.amount_cents FROM charging_orders AS o "
        "JOIN charging_piles AS p ON p.pile_id = o.pile_id "
        "JOIN charging_stations AS s ON s.station_id = p.station_id "
        "ORDER BY o.created_at DESC, o.order_id DESC");
}

}  // namespace

Repository::Repository(QString connectionName)
    : connectionName_(std::move(connectionName))
{
}

Repository::~Repository()
{
    close();
}

bool Repository::open(const QString &databasePath, QString *error)
{
    beginOperation();

    const auto reject = [this, error](const QString &detail) {
        failOperation(QStringLiteral("open"), detail);
        if (error != nullptr) {
            *error = detail;
        }
        if (database_.isValid()) {
            database_.close();
        }
        return false;
    };

    if (database_.isValid() && database_.isOpen()) {
        return reject(QStringLiteral("database is already open"));
    }

    const QFileInfo databaseFile(databasePath);
    if (databasePath.trimmed().isEmpty() || !databaseFile.exists()
        || !databaseFile.isFile()) {
        return reject(QStringLiteral("database file does not exist"));
    }

    if (!database_.isValid()) {
        if (QSqlDatabase::contains(connectionName_)) {
            return reject(QStringLiteral("database connection name is already in use"));
        }
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                               connectionName_);
    }
    if (!database_.isValid()) {
        return reject(QStringLiteral("QSQLITE driver is unavailable"));
    }

    database_.setDatabaseName(databaseFile.absoluteFilePath());
    if (!database_.open()) {
        return reject(database_.lastError().text());
    }

    QSqlQuery foreignKeys(database_);
    if (!foreignKeys.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        return reject(foreignKeys.lastError().text());
    }
    if (!foreignKeys.exec(QStringLiteral("PRAGMA foreign_keys"))
        || !foreignKeys.next() || foreignKeys.value(0).toInt() != 1) {
        return reject(QStringLiteral("failed to enable foreign keys"));
    }

    QSqlQuery version(database_);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) {
        return reject(version.lastError().text());
    }
    if (version.value(0).toInt() != kExpectedSchemaVersion) {
        return reject(QStringLiteral("unsupported database schema version: %1")
                          .arg(version.value(0).toInt()));
    }

    QSqlQuery tables(database_);
    if (!tables.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table' "
            "AND name IN ('users', 'admins', 'charging_stations', "
            "'charging_piles', 'charging_orders')"))
        || !tables.next()) {
        return reject(tables.lastError().text());
    }
    if (tables.value(0).toInt() != 5) {
        return reject(QStringLiteral("required Demo tables are missing"));
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void Repository::close()
{
    if (!database_.isValid()) {
        return;
    }
    const QString connectionName = database_.connectionName();
    database_.close();
    database_ = QSqlDatabase{};
    QSqlDatabase::removeDatabase(connectionName);
}

bool Repository::isOpen() const noexcept
{
    return database_.isValid() && database_.isOpen();
}

bool Repository::lastOperationSucceeded() const noexcept
{
    return lastOperationSucceeded_;
}

void Repository::beginOperation() const noexcept
{
    lastOperationSucceeded_ = true;
}

void Repository::failOperation(const QString &operation,
                               const QString &detail) const
{
    lastOperationSucceeded_ = false;
    qWarning().noquote()
        << QStringLiteral("Repository %1 failed: %2").arg(operation, detail);
}

bool Repository::requireOpen(const QString &operation) const
{
    if (isOpen()) {
        return true;
    }
    failOperation(operation, QStringLiteral("database is not open"));
    return false;
}

std::optional<AdminRecord> Repository::findAdminByUsername(
    const QString &username) const
{
    beginOperation();
    const QString operation = QStringLiteral("findAdminByUsername");
    if (!requireOpen(operation)) {
        return std::nullopt;
    }

    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "SELECT admin_id, username, password_hash, display_name "
            "FROM admins WHERE username = :username"))) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":username"), username);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    return AdminRecord{
        query.value(0).toLongLong(),
        query.value(1).toString(),
        query.value(2).toString(),
        query.value(3).toString(),
    };
}

std::optional<UserDto> Repository::findUserByPhone(const QString &phone) const
{
    beginOperation();
    const QString operation = QStringLiteral("findUserByPhone");
    if (!requireOpen(operation)) {
        return std::nullopt;
    }

    QSqlQuery query(database_);
    if (!query.prepare(userSelectSql(QStringLiteral("WHERE phone = :phone")))) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":phone"), phone);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    UserDto user;
    if (!readUser(query, &user)) {
        failOperation(operation, QStringLiteral("invalid user status in database"));
        return std::nullopt;
    }
    return user;
}

std::optional<UserDto> Repository::findUserById(qint64 userId) const
{
    beginOperation();
    const QString operation = QStringLiteral("findUserById");
    if (!requireOpen(operation)) {
        return std::nullopt;
    }

    QSqlQuery query(database_);
    if (!query.prepare(userSelectSql(QStringLiteral("WHERE user_id = :user_id")))) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    UserDto user;
    if (!readUser(query, &user)) {
        failOperation(operation, QStringLiteral("invalid user status in database"));
        return std::nullopt;
    }
    return user;
}

UserDto Repository::createUser(const QString &phone,
                               const QString &nickname,
                               const QString &createdAt)
{
    beginOperation();
    const QString operation = QStringLiteral("createUser");
    if (!requireOpen(operation)) {
        return {};
    }

    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO users (phone, nickname, balance_cents, status, created_at) "
            "VALUES (:phone, :nickname, 0, 'ACTIVE', :created_at)"))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    query.bindValue(QStringLiteral(":phone"), phone);
    query.bindValue(QStringLiteral(":nickname"), nickname);
    query.bindValue(QStringLiteral(":created_at"), createdAt);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return {};
    }

    bool idOk = false;
    const qint64 userId = query.lastInsertId().toLongLong(&idOk);
    if (!idOk || userId <= 0) {
        failOperation(operation, QStringLiteral("database did not return a user id"));
        return {};
    }
    return UserDto{userId, phone, nickname, 0, UserStatus::Active, createdAt};
}

bool Repository::updateUser(const UserDto &user)
{
    beginOperation();
    const QString operation = QStringLiteral("updateUser");
    if (!requireOpen(operation)) {
        return false;
    }

    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "UPDATE users SET nickname = :nickname, balance_cents = :balance, "
            "status = :status WHERE user_id = :user_id"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    query.bindValue(QStringLiteral(":nickname"), user.nickname);
    query.bindValue(QStringLiteral(":balance"), user.balanceCents);
    query.bindValue(QStringLiteral(":status"), toString(user.status));
    query.bindValue(QStringLiteral(":user_id"), user.userId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

std::optional<UserDto> Repository::addUserBalance(qint64 userId,
                                                  qint64 amountCents)
{
    beginOperation();
    const QString operation = QStringLiteral("addUserBalance");
    if (!requireOpen(operation) || amountCents <= 0) {
        if (amountCents <= 0) {
            failOperation(operation, QStringLiteral("amount must be positive"));
        }
        return std::nullopt;
    }

    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "UPDATE users SET balance_cents = balance_cents + :amount "
            "WHERE user_id = :user_id "
            "AND balance_cents <= 9223372036854775807 - :amount"))) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":amount"), amountCents);
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    if (query.numRowsAffected() != 1) {
        return std::nullopt;
    }
    return findUserById(userId);
}

QList<UserDto> Repository::listUsers() const
{
    beginOperation();
    const QString operation = QStringLiteral("listUsers");
    QList<UserDto> users;
    if (!requireOpen(operation)) {
        return users;
    }

    QSqlQuery query(database_);
    if (!query.exec(userSelectSql(QStringLiteral("ORDER BY user_id")))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    while (query.next()) {
        UserDto user;
        if (!readUser(query, &user)) {
            failOperation(operation, QStringLiteral("invalid user status in database"));
            return {};
        }
        users.append(user);
    }
    return users;
}

QList<StationDto> Repository::listActiveStations() const
{
    beginOperation();
    const QString operation = QStringLiteral("listActiveStations");
    QList<StationDto> stations;
    if (!requireOpen(operation)) {
        return stations;
    }

    QSqlQuery query(database_);
    if (!query.exec(stationSelectSql(QStringLiteral("WHERE s.status = 'ACTIVE'")))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    while (query.next()) {
        StationDto station;
        if (!readStation(query, &station)) {
            failOperation(operation, QStringLiteral("invalid station status in database"));
            return {};
        }
        stations.append(station);
    }
    return stations;
}

QList<StationDto> Repository::listStations() const
{
    beginOperation();
    const QString operation = QStringLiteral("listStations");
    QList<StationDto> stations;
    if (!requireOpen(operation)) {
        return stations;
    }

    QSqlQuery query(database_);
    if (!query.exec(stationSelectSql())) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    while (query.next()) {
        StationDto station;
        if (!readStation(query, &station)) {
            failOperation(operation, QStringLiteral("invalid station status in database"));
            return {};
        }
        stations.append(station);
    }
    return stations;
}

std::optional<StationDto> Repository::findStationById(qint64 stationId) const
{
    beginOperation();
    const QString operation = QStringLiteral("findStationById");
    if (!requireOpen(operation)) {
        return std::nullopt;
    }

    QSqlQuery query(database_);
    if (!query.prepare(stationSelectSql(
            QStringLiteral("WHERE s.station_id = :station_id")))) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":station_id"), stationId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    StationDto station;
    if (!readStation(query, &station)) {
        failOperation(operation, QStringLiteral("invalid station status in database"));
        return std::nullopt;
    }
    return station;
}

StationDto Repository::createStation(StationDto station, qint64 pileCount)
{
    beginOperation();
    const QString operation = QStringLiteral("createStation");
    if (!requireOpen(operation)) {
        return {};
    }
    if (pileCount < 0 || pileCount > 100) {
        failOperation(operation, QStringLiteral("invalid pile count"));
        return {};
    }
    if (!database_.transaction()) {
        failOperation(operation, database_.lastError().text());
        return {};
    }

    QSqlQuery stationInsert(database_);
    if (!stationInsert.prepare(QStringLiteral(
            "INSERT INTO charging_stations "
            "(name, region, address, longitude, latitude, "
            "price_cents_per_kwh, status, created_at) VALUES "
            "(:name, :region, :address, :longitude, :latitude, "
            ":price, 'ACTIVE', :created_at)"))) {
        database_.rollback();
        failOperation(operation, stationInsert.lastError().text());
        return {};
    }
    stationInsert.bindValue(QStringLiteral(":name"), station.name);
    stationInsert.bindValue(QStringLiteral(":region"), station.region);
    stationInsert.bindValue(QStringLiteral(":address"), station.address);
    stationInsert.bindValue(QStringLiteral(":longitude"), station.longitude);
    stationInsert.bindValue(QStringLiteral(":latitude"), station.latitude);
    stationInsert.bindValue(QStringLiteral(":price"), station.priceCentsPerKwh);
    stationInsert.bindValue(QStringLiteral(":created_at"),
                            QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!stationInsert.exec()) {
        database_.rollback();
        failOperation(operation, stationInsert.lastError().text());
        return {};
    }

    bool idOk = false;
    const qint64 stationId = stationInsert.lastInsertId().toLongLong(&idOk);
    if (!idOk || stationId <= 0) {
        database_.rollback();
        failOperation(operation, QStringLiteral("database did not return a station id"));
        return {};
    }

    QSqlQuery pileInsert(database_);
    if (!pileInsert.prepare(QStringLiteral(
            "INSERT INTO charging_piles "
            "(station_id, pile_code, pile_type, rated_power_kw, status) "
            "VALUES (:station_id, :pile_code, :pile_type, :power, 'IDLE')"))) {
        database_.rollback();
        failOperation(operation, pileInsert.lastError().text());
        return {};
    }
    for (qint64 index = 0; index < pileCount; ++index) {
        const PileType pileType = index % 2 == 0
            ? PileType::Fast
            : PileType::Slow;
        pileInsert.bindValue(QStringLiteral(":station_id"), stationId);
        pileInsert.bindValue(
            QStringLiteral(":pile_code"),
            QStringLiteral("PILE-%1-%2")
                .arg(stationId, 3, 10, QLatin1Char('0'))
                .arg(index + 1, 2, 10, QLatin1Char('0')));
        pileInsert.bindValue(QStringLiteral(":pile_type"), toString(pileType));
        pileInsert.bindValue(QStringLiteral(":power"),
                             pileType == PileType::Fast ? 60.0 : 7.0);
        if (!pileInsert.exec()) {
            database_.rollback();
            failOperation(operation, pileInsert.lastError().text());
            return {};
        }
    }

    if (!database_.commit()) {
        database_.rollback();
        failOperation(operation, database_.lastError().text());
        return {};
    }

    station.stationId = stationId;
    station.status = StationStatus::Active;
    station.totalPileCount = pileCount;
    station.availablePileCount = pileCount;
    station.onlineRatePercent = pileCount == 0 ? 0.0 : 100.0;
    station.distanceKm.reset();
    station.predictedCongestion.reset();
    station.recommended = false;
    return station;
}

DeleteStationResult Repository::deleteStation(qint64 stationId)
{
    beginOperation();
    const QString operation = QStringLiteral("deleteStation");
    if (!requireOpen(operation)) {
        return DeleteStationResult::StorageError;
    }
    if (stationId <= 0) {
        return DeleteStationResult::NotFound;
    }
    if (!database_.transaction()) {
        failOperation(operation, database_.lastError().text());
        return DeleteStationResult::StorageError;
    }

    const auto storageFailure = [this, &operation](const QString &detail) {
        database_.rollback();
        failOperation(operation, detail);
        return DeleteStationResult::StorageError;
    };

    QSqlQuery orderCheck(database_);
    if (!orderCheck.prepare(QStringLiteral(
            "SELECT 1 FROM charging_orders AS o "
            "JOIN charging_piles AS p ON p.pile_id = o.pile_id "
            "WHERE p.station_id = :station_id LIMIT 1"))) {
        return storageFailure(orderCheck.lastError().text());
    }
    orderCheck.bindValue(QStringLiteral(":station_id"), stationId);
    if (!orderCheck.exec()) {
        return storageFailure(orderCheck.lastError().text());
    }
    const bool hasOrders = orderCheck.next();
    orderCheck.finish();
    if (hasOrders) {
        if (!database_.rollback()) {
            failOperation(operation, database_.lastError().text());
            return DeleteStationResult::StorageError;
        }
        return DeleteStationResult::HasOrders;
    }

    QSqlQuery pileDelete(database_);
    if (!pileDelete.prepare(QStringLiteral(
            "DELETE FROM charging_piles WHERE station_id = :station_id"))) {
        return storageFailure(pileDelete.lastError().text());
    }
    pileDelete.bindValue(QStringLiteral(":station_id"), stationId);
    if (!pileDelete.exec()) {
        return storageFailure(pileDelete.lastError().text());
    }

    QSqlQuery stationDelete(database_);
    if (!stationDelete.prepare(QStringLiteral(
            "DELETE FROM charging_stations WHERE station_id = :station_id"))) {
        return storageFailure(stationDelete.lastError().text());
    }
    stationDelete.bindValue(QStringLiteral(":station_id"), stationId);
    if (!stationDelete.exec()) {
        return storageFailure(stationDelete.lastError().text());
    }
    if (stationDelete.numRowsAffected() != 1) {
        if (!database_.rollback()) {
            failOperation(operation, database_.lastError().text());
            return DeleteStationResult::StorageError;
        }
        return DeleteStationResult::NotFound;
    }

    if (!database_.commit()) {
        const QString detail = database_.lastError().text();
        database_.rollback();
        failOperation(operation, detail);
        return DeleteStationResult::StorageError;
    }
    return DeleteStationResult::Deleted;
}

QList<PileDto> Repository::listPilesByStationId(qint64 stationId) const
{
    beginOperation();
    const QString operation = QStringLiteral("listPilesByStationId");
    QList<PileDto> piles;
    if (!requireOpen(operation)) {
        return piles;
    }

    QSqlQuery query(database_);
    if (!query.prepare(pileSelectSql(
            QStringLiteral("WHERE p.station_id = :station_id")))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    query.bindValue(QStringLiteral(":station_id"), stationId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    while (query.next()) {
        PileDto pile;
        if (!readPile(query, &pile)) {
            failOperation(operation, QStringLiteral("invalid pile value in database"));
            return {};
        }
        piles.append(pile);
    }
    return piles;
}

QList<PileDto> Repository::listPiles() const
{
    beginOperation();
    const QString operation = QStringLiteral("listPiles");
    QList<PileDto> piles;
    if (!requireOpen(operation)) {
        return piles;
    }

    QSqlQuery query(database_);
    if (!query.exec(pileSelectSql())) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    while (query.next()) {
        PileDto pile;
        if (!readPile(query, &pile)) {
            failOperation(operation, QStringLiteral("invalid pile value in database"));
            return {};
        }
        piles.append(pile);
    }
    return piles;
}

bool Repository::updatePile(const PileDto &pile)
{
    beginOperation();
    const QString operation = QStringLiteral("updatePile");
    if (!requireOpen(operation)) {
        return false;
    }

    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "UPDATE charging_piles SET status = :status WHERE pile_id = :pile_id"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    query.bindValue(QStringLiteral(":status"), toString(pile.status));
    query.bindValue(QStringLiteral(":pile_id"), pile.pileId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

QList<OrderDto> Repository::listOrders() const
{
    beginOperation();
    const QString operation = QStringLiteral("listOrders");
    QList<OrderDto> orders;
    if (!requireOpen(operation)) {
        return orders;
    }

    QSqlQuery query(database_);
    if (!query.exec(orderSelectSql())) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    while (query.next()) {
        OrderDto order;
        if (!readOrder(query, &order)) {
            failOperation(operation, QStringLiteral("invalid order value in database"));
            return {};
        }
        orders.append(order);
    }
    return orders;
}

}  // namespace charging::server
