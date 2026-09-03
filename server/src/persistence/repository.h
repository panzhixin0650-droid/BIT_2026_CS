#pragma once

#include <QSqlDatabase>
#include <QString>

namespace charging::server {

// The sole SQL boundary. Schema migrations and application queries will be
// added in later changes; callers never receive a QSqlQuery from this class.
class Repository final {
public:
    explicit Repository(QString connectionName = QStringLiteral("charging-server"));
    ~Repository();

    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;

    [[nodiscard]] bool open(const QString &databasePath, QString *error = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

private:
    QString connectionName_;
    QSqlDatabase database_;
};

}  // namespace charging::server
