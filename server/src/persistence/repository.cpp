#include "repository.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <cmath>
#include <utility>

namespace charging::server {
namespace {

using namespace charging::protocol;

QString adminSelectSql(bool accountsAvailable, const QString &whereClause = {})
{
    if (!accountsAvailable) {
        // Schema 1/2 retain their fixed administrator without rewriting credentials.
        return QStringLiteral("SELECT admin_id, username, password_hash, 'SHA256_LEGACY', "
                              "display_name, 'SYS_ADMIN', 'ACTIVE', 0, NULL, '', '', 0 FROM admins ")
            + whereClause;
    }
    return QStringLiteral(
               "SELECT admin_id, username, password_hash, password_algorithm, "
               "display_name, role, status, must_change_password, last_login_at, "
               "created_at, updated_at, version FROM admins ")
        + whereClause;
}

bool readAdmin(const QSqlQuery &query, AdminRecord *admin)
{
    admin->adminId = query.value(0).toLongLong();
    admin->username = query.value(1).toString();
    admin->passwordHash = query.value(2).toString();
    admin->passwordAlgorithm = query.value(3).toString();
    admin->displayName = query.value(4).toString();
    admin->role = query.value(5).toString();
    admin->status = query.value(6).toString();
    admin->mustChangePassword = query.value(7).toBool();
    admin->lastLoginAt = query.value(8).toString();
    admin->createdAt = query.value(9).toString();
    admin->updatedAt = query.value(10).toString();
    admin->version = query.value(11).toLongLong();
    return admin->adminId > 0
        && (admin->role == QStringLiteral("SYS_ADMIN")
            || admin->role == QStringLiteral("STATION_ADMIN")
            || admin->role == QStringLiteral("USER_ADMIN"))
        && (admin->status == QStringLiteral("ACTIVE")
            || admin->status == QStringLiteral("DISABLED"));
}

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

QString orderSelectSql(const QString &whereClause = {})
{
    return QStringLiteral(
        "SELECT o.order_id, o.order_no, o.created_at, o.user_id, "
        "s.station_id, s.name, p.pile_id, p.pile_code, o.mode, o.status, "
        "o.reserved_at, o.started_at, o.ended_at, o.paid_at, "
        "o.duration_seconds, o.energy_wh, o.unit_price_cents_per_kwh, "
        "o.amount_cents FROM charging_orders AS o "
        "JOIN charging_piles AS p ON p.pile_id = o.pile_id "
        "JOIN charging_stations AS s ON s.station_id = p.station_id ")
        + whereClause
        + QStringLiteral(" ORDER BY o.created_at DESC, o.order_id DESC");
}

void bindOrderValues(QSqlQuery &query, const OrderDto &order)
{
    const auto optionalText = [](const std::optional<QString> &value) {
        return value.has_value() ? QVariant(*value) : QVariant{};
    };
    query.bindValue(QStringLiteral(":status"), toString(order.status));
    query.bindValue(QStringLiteral(":started_at"), optionalText(order.startedAt));
    query.bindValue(QStringLiteral(":ended_at"), optionalText(order.endedAt));
    query.bindValue(QStringLiteral(":paid_at"), optionalText(order.paidAt));
    query.bindValue(QStringLiteral(":duration"), order.durationSeconds);
    query.bindValue(QStringLiteral(":energy"), order.energyWh);
    query.bindValue(QStringLiteral(":price"), order.unitPriceCentsPerKwh.has_value()
                        ? QVariant(*order.unitPriceCentsPerKwh) : QVariant{});
    query.bindValue(QStringLiteral(":amount"), order.amountCents);
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
    const int schemaVersion = version.value(0).toInt();
    if (schemaVersion < 1 || schemaVersion > 3) {
        return reject(QStringLiteral("unsupported database schema version: %1")
                          .arg(version.value(0).toInt()));
    }

    QSqlQuery tables(database_);
    if (!tables.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table' "
            "AND name IN ('users', 'admins', 'charging_stations', 'charging_piles', "
            "'charging_orders')"))
        || !tables.next()) {
        return reject(tables.lastError().text());
    }
    if (tables.value(0).toInt() != 5) {
        return reject(QStringLiteral("required Demo tables are missing"));
    }

    supportTicketsAvailable_ = false;
    if (schemaVersion >= 2) {
        QSqlQuery tickets(database_);
        if (!tickets.exec(QStringLiteral(
                "SELECT ticket_id, user_id, submission_id, title, summary, source_model, "
                "status, reply, created_at, updated_at FROM support_tickets LIMIT 0")))
            return reject(QStringLiteral("support ticket schema is missing or invalid"));
        supportTicketsAvailable_ = true;
    }

    adminAccountsAvailable_ = false;
    QSqlQuery adminSchema(database_);
    if (!adminSchema.exec(adminSelectSql(schemaVersion == 3, QStringLiteral("LIMIT 0"))))
        return reject(QStringLiteral("administrator schema is missing or invalid"));
    if (schemaVersion == 3) {
        for (const auto &sql : {
                 QStringLiteral("SELECT admin_id, station_id, granted_by_admin_id, granted_at FROM admin_station_scopes LIMIT 0"),
                 QStringLiteral("SELECT audit_id, actor_admin_id, action, target_admin_id, details_json, created_at FROM admin_audit_logs LIMIT 0")}) {
            QSqlQuery extension(database_);
            if (!extension.exec(sql)) return reject(QStringLiteral("administrator extension schema is missing or invalid"));
        }
        adminAccountsAvailable_ = true;
    } else {
        // The reverted PR used version 2 for a different layout. Do not guess or migrate it.
        QSqlQuery legacy(database_);
        if (!legacy.exec(QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('admins')"))
            || !legacy.next() || legacy.value(0).toInt() != 4)
            return reject(QStringLiteral("legacy administrator layout mismatch; explicit migration recovery required"));
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void Repository::close()
{
    supportTicketsAvailable_ = false;
    adminAccountsAvailable_ = false;
    rollbackTransaction();
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
    if (!query.prepare(adminSelectSql(adminAccountsAvailable_, QStringLiteral("WHERE username = :username")))) {
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

    AdminRecord admin;
    if (!readAdmin(query, &admin)) {
        failOperation(operation, QStringLiteral("invalid admin record in database"));
        return std::nullopt;
    }
    if (!adminAccountsAvailable_) return admin;
    QSqlQuery scopes(database_);
    if (!scopes.prepare(QStringLiteral(
            "SELECT station_id FROM admin_station_scopes "
            "WHERE admin_id = :admin_id ORDER BY station_id"))) {
        failOperation(operation, scopes.lastError().text());
        return std::nullopt;
    }
    scopes.bindValue(QStringLiteral(":admin_id"), admin.adminId);
    if (!scopes.exec()) {
        failOperation(operation, scopes.lastError().text());
        return std::nullopt;
    }
    while (scopes.next()) admin.stationIds.append(scopes.value(0).toLongLong());
    return admin;
}

std::optional<AdminRecord> Repository::findAdminById(qint64 adminId) const
{
    beginOperation();
    const QString operation = QStringLiteral("findAdminById");
    if (!requireOpen(operation)) return std::nullopt;
    QSqlQuery query(database_);
    if (!query.prepare(adminSelectSql(adminAccountsAvailable_, QStringLiteral("WHERE admin_id = :admin_id")))) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":admin_id"), adminId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) return std::nullopt;
    AdminRecord admin;
    if (!readAdmin(query, &admin)) {
        failOperation(operation, QStringLiteral("invalid admin record in database"));
        return std::nullopt;
    }
    if (!adminAccountsAvailable_) return admin;
    QSqlQuery scopes(database_);
    if (!scopes.prepare(QStringLiteral(
            "SELECT station_id FROM admin_station_scopes "
            "WHERE admin_id = :admin_id ORDER BY station_id"))) {
        failOperation(operation, scopes.lastError().text());
        return std::nullopt;
    }
    scopes.bindValue(QStringLiteral(":admin_id"), adminId);
    if (!scopes.exec()) {
        failOperation(operation, scopes.lastError().text());
        return std::nullopt;
    }
    while (scopes.next()) admin.stationIds.append(scopes.value(0).toLongLong());
    return admin;
}

QList<AdminRecord> Repository::listAdmins() const
{
    beginOperation();
    const QString operation = QStringLiteral("listAdmins");
    QList<AdminRecord> result;
    if (!requireOpen(operation)) return result;
    QSqlQuery query(database_);
    if (!query.exec(adminSelectSql(adminAccountsAvailable_, QStringLiteral("ORDER BY admin_id")))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    while (query.next()) {
        AdminRecord admin;
        if (!readAdmin(query, &admin)) {
            failOperation(operation, QStringLiteral("invalid admin record in database"));
            return {};
        }
        result.append(admin);
    }
    if (!adminAccountsAvailable_) return result;
    QSqlQuery scopes(database_);
    if (!scopes.exec(QStringLiteral(
            "SELECT admin_id, station_id FROM admin_station_scopes "
            "ORDER BY admin_id, station_id"))) {
        failOperation(operation, scopes.lastError().text());
        return {};
    }
    while (scopes.next()) {
        const qint64 ownerId = scopes.value(0).toLongLong();
        for (AdminRecord &admin : result) {
            if (admin.adminId == ownerId) {
                admin.stationIds.append(scopes.value(1).toLongLong());
                break;
            }
        }
    }
    return result;
}

AdminRecord Repository::createAdmin(AdminRecord admin)
{
    beginOperation();
    const QString operation = QStringLiteral("createAdmin");
    if (!adminAccountsAvailable_) {
        failOperation(operation, QStringLiteral("administrator migration required"));
        return {};
    }
    if (!requireOpen(operation)) return {};
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO admins (username, password_hash, password_algorithm, "
            "display_name, role, status, must_change_password, last_login_at, "
            "created_at, updated_at, version) VALUES (:username, :password_hash, "
            ":password_algorithm, :display_name, :role, :status, "
            ":must_change_password, NULL, :created_at, :updated_at, 0)"))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    query.bindValue(QStringLiteral(":username"), admin.username);
    query.bindValue(QStringLiteral(":password_hash"), admin.passwordHash);
    query.bindValue(QStringLiteral(":password_algorithm"), admin.passwordAlgorithm);
    query.bindValue(QStringLiteral(":display_name"), admin.displayName);
    query.bindValue(QStringLiteral(":role"), admin.role);
    query.bindValue(QStringLiteral(":status"), admin.status);
    query.bindValue(QStringLiteral(":must_change_password"), admin.mustChangePassword ? 1 : 0);
    query.bindValue(QStringLiteral(":created_at"), admin.createdAt);
    query.bindValue(QStringLiteral(":updated_at"), admin.updatedAt);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    admin.adminId = query.lastInsertId().toLongLong();
    return admin;
}

bool Repository::updateAdmin(const AdminRecord &admin)
{
    beginOperation();
    const QString operation = QStringLiteral("updateAdmin");
    if (!adminAccountsAvailable_) {
        failOperation(operation, QStringLiteral("administrator migration required"));
        return false;
    }
    if (!requireOpen(operation)) return false;
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "UPDATE admins SET password_hash = :password_hash, "
            "password_algorithm = :password_algorithm, display_name = :display_name, "
            "role = :role, status = :status, must_change_password = :must_change, "
            "last_login_at = :last_login, updated_at = :updated_at, version = version + 1 "
            "WHERE admin_id = :admin_id AND version = :version"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    query.bindValue(QStringLiteral(":password_hash"), admin.passwordHash);
    query.bindValue(QStringLiteral(":password_algorithm"), admin.passwordAlgorithm);
    query.bindValue(QStringLiteral(":display_name"), admin.displayName);
    query.bindValue(QStringLiteral(":role"), admin.role);
    query.bindValue(QStringLiteral(":status"), admin.status);
    query.bindValue(QStringLiteral(":must_change"), admin.mustChangePassword ? 1 : 0);
    query.bindValue(QStringLiteral(":last_login"),
                    admin.lastLoginAt.isEmpty() ? QVariant{} : QVariant(admin.lastLoginAt));
    query.bindValue(QStringLiteral(":updated_at"), admin.updatedAt);
    query.bindValue(QStringLiteral(":admin_id"), admin.adminId);
    query.bindValue(QStringLiteral(":version"), admin.version);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) return false;
    return true;
}

bool Repository::replaceAdminStationScopes(qint64 adminId,
                                           const QList<qint64> &stationIds,
                                           qint64 grantedByAdminId,
                                           const QString &grantedAt)
{
    beginOperation();
    const QString operation = QStringLiteral("replaceAdminStationScopes");
    if (!adminAccountsAvailable_) {
        failOperation(operation, QStringLiteral("administrator migration required"));
        return false;
    }
    if (!requireOpen(operation)) return false;
    QSqlQuery remove(database_);
    if (!remove.prepare(QStringLiteral(
            "DELETE FROM admin_station_scopes WHERE admin_id = :admin_id"))) {
        failOperation(operation, remove.lastError().text());
        return false;
    }
    remove.bindValue(QStringLiteral(":admin_id"), adminId);
    if (!remove.exec()) {
        failOperation(operation, remove.lastError().text());
        return false;
    }
    for (qint64 stationId : stationIds) {
        QSqlQuery scope(database_);
        if (!scope.prepare(QStringLiteral(
                "INSERT INTO admin_station_scopes "
                "(admin_id, station_id, granted_by_admin_id, granted_at) "
                "VALUES (:admin_id, :station_id, :granted_by, :granted_at)"))) {
            failOperation(operation, scope.lastError().text());
            return false;
        }
        scope.bindValue(QStringLiteral(":admin_id"), adminId);
        scope.bindValue(QStringLiteral(":station_id"), stationId);
        scope.bindValue(QStringLiteral(":granted_by"), grantedByAdminId);
        scope.bindValue(QStringLiteral(":granted_at"), grantedAt);
        if (!scope.exec()) {
            failOperation(operation, scope.lastError().text());
            return false;
        }
    }
    return true;
}

bool Repository::appendAdminAudit(qint64 actorAdminId,
                                  const QString &action,
                                  qint64 targetAdminId,
                                  const QString &detailsJson,
                                  const QString &createdAt)
{
    beginOperation();
    const QString operation = QStringLiteral("appendAdminAudit");
    if (!adminAccountsAvailable_) {
        failOperation(operation, QStringLiteral("administrator migration required"));
        return false;
    }
    if (!requireOpen(operation)) return false;
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO admin_audit_logs "
            "(actor_admin_id, action, target_admin_id, details_json, created_at) "
            "VALUES (:actor, :action, :target, :details, :created_at)"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    query.bindValue(QStringLiteral(":actor"), actorAdminId);
    query.bindValue(QStringLiteral(":action"), action);
    query.bindValue(QStringLiteral(":target"), targetAdminId);
    query.bindValue(QStringLiteral(":details"), detailsJson);
    query.bindValue(QStringLiteral(":created_at"), createdAt);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    return true;
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

StationDto Repository::createStation(StationDto station,
                                     const QList<PileDto> &piles)
{
    beginOperation();
    const QString operation = QStringLiteral("createStation");
    if (!requireOpen(operation)) {
        return {};
    }
    if (piles.size() > 100) {
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
    for (const PileDto &pile : piles) {
        if (pile.pileCode.trimmed().isEmpty() || pile.pileCode.size() > 64
            || !std::isfinite(pile.ratedPowerKw) || pile.ratedPowerKw <= 0.0
            || pile.ratedPowerKw > 1000.0) {
            database_.rollback();
            failOperation(operation, QStringLiteral("invalid pile"));
            return {};
        }
        pileInsert.bindValue(QStringLiteral(":station_id"), stationId);
        pileInsert.bindValue(QStringLiteral(":pile_code"), pile.pileCode.trimmed());
        pileInsert.bindValue(QStringLiteral(":pile_type"), toString(pile.pileType));
        pileInsert.bindValue(QStringLiteral(":power"), pile.ratedPowerKw);
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
    station.totalPileCount = piles.size();
    station.availablePileCount = piles.size();
    station.onlineRatePercent = piles.isEmpty() ? 0.0 : 100.0;
    station.distanceKm.reset();
    station.predictedCongestion.reset();
    station.recommended = false;
    return station;
}

bool Repository::updateStation(const StationDto &station)
{
    beginOperation();
    const QString operation = QStringLiteral("updateStation");
    if (!requireOpen(operation) || station.stationId <= 0
        || station.name.trimmed().isEmpty() || station.name.size() > 64
        || station.region.trimmed().isEmpty() || station.region.size() > 64
        || station.address.trimmed().isEmpty() || station.address.size() > 200
        || !std::isfinite(station.longitude) || station.longitude < -180.0 || station.longitude > 180.0
        || !std::isfinite(station.latitude) || station.latitude < -90.0 || station.latitude > 90.0
        || station.priceCentsPerKwh <= 0
        || (station.status != StationStatus::Active && station.status != StationStatus::Disabled)) {
        failOperation(operation, QStringLiteral("invalid station"));
        return false;
    }
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "UPDATE charging_stations SET name = :name, region = :region, "
            "address = :address, longitude = :longitude, latitude = :latitude, "
            "price_cents_per_kwh = :price, status = :status "
            "WHERE station_id = :station_id"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    query.bindValue(QStringLiteral(":name"), station.name.trimmed());
    query.bindValue(QStringLiteral(":region"), station.region.trimmed());
    query.bindValue(QStringLiteral(":address"), station.address.trimmed());
    query.bindValue(QStringLiteral(":longitude"), station.longitude);
    query.bindValue(QStringLiteral(":latitude"), station.latitude);
    query.bindValue(QStringLiteral(":price"), station.priceCentsPerKwh);
    query.bindValue(QStringLiteral(":status"), toString(station.status));
    query.bindValue(QStringLiteral(":station_id"), station.stationId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
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

PileDto Repository::createPile(PileDto pile)
{
    beginOperation();
    const QString operation = QStringLiteral("createPile");
    if (!requireOpen(operation) || pile.stationId <= 0 || pile.pileCode.isEmpty()
        || pile.ratedPowerKw <= 0.0) {
        failOperation(operation, QStringLiteral("invalid pile"));
        return {};
    }
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO charging_piles (station_id, pile_code, pile_type, rated_power_kw, status) "
            "VALUES (:station_id, :pile_code, :pile_type, :power, :status)"))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    query.bindValue(QStringLiteral(":station_id"), pile.stationId);
    query.bindValue(QStringLiteral(":pile_code"), pile.pileCode);
    query.bindValue(QStringLiteral(":pile_type"), toString(pile.pileType));
    query.bindValue(QStringLiteral(":power"), pile.ratedPowerKw);
    query.bindValue(QStringLiteral(":status"), toString(PileStatus::Idle));
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    bool idOk = false;
    pile.pileId = query.lastInsertId().toLongLong(&idOk);
    if (!idOk || pile.pileId <= 0) {
        failOperation(operation, QStringLiteral("database did not return a pile id"));
        return {};
    }
    pile.status = PileStatus::Idle;
    pile.chargeCount = 0;
    pile.totalChargeSeconds = 0;
    return pile;
}

DeletePileResult Repository::deletePile(qint64 pileId)
{
    beginOperation();
    const QString operation = QStringLiteral("deletePile");
    if (!requireOpen(operation) || pileId <= 0) return DeletePileResult::NotFound;
    QSqlQuery status(database_);
    if (!status.prepare(QStringLiteral("SELECT status FROM charging_piles WHERE pile_id = :pile_id"))) {
        failOperation(operation, status.lastError().text());
        return DeletePileResult::StorageError;
    }
    status.bindValue(QStringLiteral(":pile_id"), pileId);
    if (!status.exec()) { failOperation(operation, status.lastError().text()); return DeletePileResult::StorageError; }
    if (!status.next()) return DeletePileResult::NotFound;
    const QString state = status.value(0).toString();
    if (state != QStringLiteral("IDLE") && state != QStringLiteral("OFFLINE")) return DeletePileResult::Busy;
    QSqlQuery orders(database_);
    if (!orders.prepare(QStringLiteral("SELECT 1 FROM charging_orders WHERE pile_id = :pile_id LIMIT 1"))) {
        failOperation(operation, orders.lastError().text()); return DeletePileResult::StorageError;
    }
    orders.bindValue(QStringLiteral(":pile_id"), pileId);
    if (!orders.exec()) { failOperation(operation, orders.lastError().text()); return DeletePileResult::StorageError; }
    if (orders.next()) return DeletePileResult::HasOrders;
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral("DELETE FROM charging_piles WHERE pile_id = :pile_id"))) {
        failOperation(operation, query.lastError().text()); return DeletePileResult::StorageError;
    }
    query.bindValue(QStringLiteral(":pile_id"), pileId);
    if (!query.exec()) { failOperation(operation, query.lastError().text()); return DeletePileResult::StorageError; }
    return query.numRowsAffected() == 1 ? DeletePileResult::Deleted : DeletePileResult::NotFound;
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
            "UPDATE charging_piles SET pile_code = :pile_code, pile_type = :pile_type, "
            "rated_power_kw = :power, status = :status WHERE pile_id = :pile_id"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    query.bindValue(QStringLiteral(":pile_code"), pile.pileCode.trimmed());
    query.bindValue(QStringLiteral(":pile_type"), toString(pile.pileType));
    query.bindValue(QStringLiteral(":power"), pile.ratedPowerKw);
    query.bindValue(QStringLiteral(":status"), toString(pile.status));
    query.bindValue(QStringLiteral(":pile_id"), pile.pileId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool Repository::beginTransaction()
{
    beginOperation();
    const QString operation = QStringLiteral("beginOrderTransaction");
    if (!requireOpen(operation)) return false;
    if (transactionOpen_) {
        failOperation(operation, QStringLiteral("nested transaction"));
        return false;
    }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    transactionOpen_ = true;
    return true;
}

bool Repository::commitTransaction()
{
    beginOperation();
    const QString operation = QStringLiteral("commitOrderTransaction");
    if (!requireOpen(operation) || !transactionOpen_) return false;
    if (!database_.commit()) {
        failOperation(operation, database_.lastError().text());
        return false;
    }
    transactionOpen_ = false;
    return true;
}

void Repository::rollbackTransaction()
{
    if (!transactionOpen_) return;
    if (!database_.rollback()) {
        failOperation(QStringLiteral("rollbackOrderTransaction"),
                      database_.lastError().text());
        return;
    }
    transactionOpen_ = false;
}

QList<OrderDto> Repository::listOrders(std::optional<qint64> userId) const
{
    beginOperation();
    const QString operation = QStringLiteral("listOrders");
    QList<OrderDto> orders;
    if (!requireOpen(operation)) {
        return orders;
    }

    QSqlQuery query(database_);
    if (!query.prepare(orderSelectSql(userId.has_value()
            ? QStringLiteral("WHERE o.user_id = :user_id") : QString{}))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    if (userId.has_value()) query.bindValue(QStringLiteral(":user_id"), *userId);
    if (!query.exec()) {
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

std::optional<OrderDto> Repository::findOrderById(qint64 orderId) const
{
    beginOperation();
    const QString operation = QStringLiteral("findOrderById");
    if (!requireOpen(operation)) return std::nullopt;
    QSqlQuery query(database_);
    if (!query.prepare(orderSelectSql(QStringLiteral("WHERE o.order_id = :order_id")))) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":order_id"), orderId);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) return std::nullopt;
    OrderDto order;
    if (!readOrder(query, &order)) {
        failOperation(operation, QStringLiteral("invalid order value in database"));
        return std::nullopt;
    }
    return order;
}

OrderDto Repository::createOrder(OrderDto order)
{
    beginOperation();
    const QString operation = QStringLiteral("createOrder");
    if (!requireOpen(operation) || !transactionOpen_) return {};
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO charging_orders (order_no, user_id, pile_id, mode, status, "
            "reserved_at, started_at, ended_at, paid_at, duration_seconds, energy_wh, "
            "unit_price_cents_per_kwh, amount_cents, created_at) "
            "VALUES (:order_no, :user_id, :pile_id, :mode, :status, :reserved_at, "
            ":started_at, :ended_at, :paid_at, :duration, :energy, :price, :amount, :created_at)"))) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    bindOrderValues(query, order);
    query.bindValue(QStringLiteral(":order_no"), order.orderNo);
    query.bindValue(QStringLiteral(":user_id"), order.userId);
    query.bindValue(QStringLiteral(":pile_id"), order.pileId);
    query.bindValue(QStringLiteral(":mode"), toString(order.mode));
    query.bindValue(QStringLiteral(":reserved_at"), order.reservedAt.has_value()
                        ? QVariant(*order.reservedAt) : QVariant{});
    query.bindValue(QStringLiteral(":created_at"), order.createdAt);
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return {};
    }
    const auto saved = findOrderById(query.lastInsertId().toLongLong());
    return saved.value_or(OrderDto{});
}

bool Repository::updateOrder(const OrderDto &order, OrderStatus expectedStatus)
{
    beginOperation();
    const QString operation = QStringLiteral("updateOrder");
    if (!requireOpen(operation) || !transactionOpen_) return false;
    QSqlQuery query(database_);
    if (!query.prepare(QStringLiteral(
            "UPDATE charging_orders SET status = :status, started_at = :started_at, "
            "ended_at = :ended_at, paid_at = :paid_at, duration_seconds = :duration, "
            "energy_wh = :energy, unit_price_cents_per_kwh = :price, amount_cents = :amount "
            "WHERE order_id = :order_id AND status = :expected_status"))) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    bindOrderValues(query, order);
    query.bindValue(QStringLiteral(":order_id"), order.orderId);
    query.bindValue(QStringLiteral(":expected_status"), toString(expectedStatus));
    if (!query.exec()) {
        failOperation(operation, query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

namespace {
QString ticketSelect()
{
    return QStringLiteral("SELECT ticket_id, user_id, submission_id, title, summary, "
                          "source_model, status, reply, created_at, updated_at FROM support_tickets ");
}

bool readTicket(const QSqlQuery &query, SupportTicketDto *ticket)
{
    return fromJson({{"ticketId", query.value(0).toLongLong()},
                     {"userId", query.value(1).toLongLong()},
                     {"submissionId", query.value(2).toString()},
                     {"title", query.value(3).toString()}, {"summary", query.value(4).toString()},
                     {"sourceModel", query.value(5).toString()}, {"status", query.value(6).toString()},
                     {"reply", query.value(7).toString()}, {"createdAt", query.value(8).toString()},
                     {"updatedAt", query.value(9).toString()}}, ticket);
}
}

bool Repository::supportsSupportTickets() const
{
    return isOpen() && supportTicketsAvailable_;
}

std::optional<SupportTicketDto> Repository::findSupportTicket(qint64 ticketId) const
{
    beginOperation();
    if (!requireOpen(QStringLiteral("findSupportTicket"))) return std::nullopt;
    QSqlQuery query(database_);
    query.prepare(ticketSelect() + QStringLiteral("WHERE ticket_id = :id"));
    query.bindValue(":id", ticketId);
    if (!query.exec()) {
        failOperation(QStringLiteral("findSupportTicket"), query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) return std::nullopt;
    SupportTicketDto ticket;
    if (!readTicket(query, &ticket)) {
        failOperation(QStringLiteral("findSupportTicket"), QStringLiteral("invalid ticket row"));
        return std::nullopt;
    }
    return ticket;
}

std::optional<SupportTicketDto> Repository::findSupportSubmission(
    qint64 userId, const QString &submissionId) const
{
    beginOperation();
    if (!requireOpen(QStringLiteral("findSupportSubmission"))) return std::nullopt;
    QSqlQuery query(database_);
    query.prepare(ticketSelect() + QStringLiteral("WHERE user_id = :user AND submission_id = :id"));
    query.bindValue(":user", userId);
    query.bindValue(":id", submissionId);
    if (!query.exec()) {
        failOperation(QStringLiteral("findSupportSubmission"), query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) return std::nullopt;
    SupportTicketDto ticket;
    if (!readTicket(query, &ticket)) {
        failOperation(QStringLiteral("findSupportSubmission"), QStringLiteral("invalid ticket row"));
        return std::nullopt;
    }
    return ticket;
}

QList<SupportTicketDto> Repository::listSupportTickets(
    std::optional<qint64> userId, std::optional<qint64> beforeId, int limit) const
{
    beginOperation();
    if (!requireOpen(QStringLiteral("listSupportTickets"))) return {};
    QString sql = ticketSelect() + QStringLiteral("WHERE 1=1 ");
    if (userId) sql += QStringLiteral("AND user_id = :user ");
    if (beforeId) sql += QStringLiteral("AND ticket_id < :before ");
    sql += QStringLiteral("ORDER BY ticket_id DESC LIMIT :limit");
    QSqlQuery query(database_);
    query.prepare(sql);
    if (userId) query.bindValue(":user", *userId);
    if (beforeId) query.bindValue(":before", *beforeId);
    query.bindValue(":limit", qBound(1, limit, 101));
    if (!query.exec()) {
        failOperation(QStringLiteral("listSupportTickets"), query.lastError().text());
        return {};
    }
    QList<SupportTicketDto> tickets;
    while (query.next()) {
        SupportTicketDto ticket;
        if (!readTicket(query, &ticket)) {
            failOperation(QStringLiteral("listSupportTickets"), QStringLiteral("invalid ticket row"));
            return {};
        }
        tickets.append(ticket);
    }
    return tickets;
}

SupportTicketDto Repository::createSupportTicket(SupportTicketDto ticket)
{
    beginOperation();
    if (!requireOpen(QStringLiteral("createSupportTicket"))) return {};
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO support_tickets (user_id, submission_id, title, summary, source_model, "
        "status, reply, created_at, updated_at) VALUES (:user, :id, :title, :summary, "
        ":model, 'OPEN', '', :created, :updated)"));
    query.bindValue(":user", ticket.userId);
    query.bindValue(":id", ticket.submissionId);
    query.bindValue(":title", ticket.title);
    query.bindValue(":summary", ticket.summary);
    query.bindValue(":model", ticket.sourceModel.isEmpty() ? QStringLiteral("") : ticket.sourceModel);
    query.bindValue(":created", ticket.createdAt);
    query.bindValue(":updated", ticket.updatedAt);
    if (!query.exec()) {
        failOperation(QStringLiteral("createSupportTicket"), query.lastError().text());
        return {};
    }
    ticket.ticketId = query.lastInsertId().toLongLong();
    ticket.status = TicketStatus::Open;
    ticket.reply = QStringLiteral("");
    return ticket;
}

bool Repository::updateSupportTicket(const SupportTicketDto &ticket)
{
    beginOperation();
    if (!requireOpen(QStringLiteral("updateSupportTicket"))) return false;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE support_tickets SET status = :status, reply = :reply, "
                                 "updated_at = :updated WHERE ticket_id = :id"));
    query.bindValue(":status", toString(ticket.status));
    query.bindValue(":reply", ticket.reply.isEmpty() ? QStringLiteral("") : ticket.reply);
    query.bindValue(":updated", ticket.updatedAt);
    query.bindValue(":id", ticket.ticketId);
    if (!query.exec()) {
        failOperation(QStringLiteral("updateSupportTicket"), query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}

}  // namespace charging::server
