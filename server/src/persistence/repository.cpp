#include "repository.h"

#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace charging::server {

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
    if (database_.isValid() && database_.isOpen()) {
        if (error != nullptr) {
            *error = QStringLiteral("database is already open");
        }
        return false;
    }

    if (QSqlDatabase::contains(connectionName_)) {
        database_ = QSqlDatabase::database(connectionName_);
    } else {
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                               connectionName_);
    }
    database_.setDatabaseName(databasePath);

    if (!database_.open()) {
        if (error != nullptr) {
            *error = database_.lastError().text();
        }
        return false;
    }

    QSqlQuery pragma(database_);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        if (error != nullptr) {
            *error = pragma.lastError().text();
        }
        database_.close();
        return false;
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void Repository::close()
{
    if (database_.isValid()) {
        database_.close();
    }
}

bool Repository::isOpen() const noexcept
{
    return database_.isValid() && database_.isOpen();
}

}  // namespace charging::server
