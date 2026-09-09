// 本文件声明基于 SQL 数据库的仓储实现类
#pragma once

#include "i_repository.h"

#include <QSqlDatabase>
#include <QString>

namespace charging::server {

// The sole SQL boundary. Callers receive domain DTOs rather than QSqlQuery or
// database-specific errors, so another persistence backend can be introduced
// without changing ApplicationService or the external protocol.
class Repository final : public IRepository {
public:
    // 用连接名构造仓储，析构时释放连接
    explicit Repository(QString connectionName = QStringLiteral("charging-server"));
    ~Repository();

    Repository(const Repository &) = delete;
    Repository &operator=(const Repository &) = delete;

    // 打开或关闭数据库文件，并可返回错误信息
    [[nodiscard]] bool open(const QString &databasePath, QString *error = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool lastOperationSucceeded() const noexcept override;
    [[nodiscard]] bool supportsSupportTickets() const override;
    // 这些开关表示对应数据表迁移是否已就绪
    [[nodiscard]] bool supportsRepairTickets() const override { return isOpen() && repairTicketsAvailable_; }
    [[nodiscard]] bool supportsAdminAccounts() const override { return isOpen() && adminAccountsAvailable_; }
    [[nodiscard]] std::optional<charging::protocol::SupportTicketDto>
    findSupportTicket(qint64 ticketId) const override;
    [[nodiscard]] std::optional<charging::protocol::SupportTicketDto>
    findSupportSubmission(qint64 userId, const QString &submissionId) const override;
    [[nodiscard]] QList<charging::protocol::SupportTicketDto>
    listSupportTickets(std::optional<qint64> userId, std::optional<qint64> beforeId,
                       int limit) const override;
    [[nodiscard]] charging::protocol::SupportTicketDto
    createSupportTicket(charging::protocol::SupportTicketDto ticket) override;
    [[nodiscard]] bool updateSupportTicket(
        const charging::protocol::SupportTicketDto &ticket) override;
    // 事务接口供订单等多表写入流程使用
    [[nodiscard]] bool beginTransaction() override;
    [[nodiscard]] bool commitTransaction() override;
    void rollbackTransaction() override;

    // 管理员账号、站点授权范围与审计记录接口
    [[nodiscard]] std::optional<AdminRecord>
    findAdminByUsername(const QString &username) const override;
    [[nodiscard]] std::optional<AdminRecord>
    findAdminById(qint64 adminId) const override;
    [[nodiscard]] QList<AdminRecord> listAdmins() const override;
    [[nodiscard]] AdminRecord createAdmin(AdminRecord admin) override;
    [[nodiscard]] bool updateAdmin(const AdminRecord &admin) override;
    [[nodiscard]] bool replaceAdminStationScopes(
        qint64 adminId,
        const QList<qint64> &stationIds,
        qint64 grantedByAdminId,
        const QString &grantedAt) override;
    [[nodiscard]] bool appendAdminAudit(qint64 actorAdminId,
                                        const QString &action,
                                        qint64 targetAdminId,
                                        const QString &detailsJson,
                                        const QString &createdAt) override;

    // 用户查询、创建与余额变更接口
    [[nodiscard]] std::optional<charging::protocol::UserDto>
    findUserByPhone(const QString &phone) const override;
    [[nodiscard]] std::optional<charging::protocol::UserDto>
    findUserById(qint64 userId) const override;
    [[nodiscard]] charging::protocol::UserDto createUser(
        const QString &phone,
        const QString &nickname,
        const QString &createdAt) override;
    [[nodiscard]] bool updateUser(
        const charging::protocol::UserDto &user) override;
    [[nodiscard]] std::optional<charging::protocol::UserDto>
    addUserBalance(qint64 userId, qint64 amountCents) override;
    [[nodiscard]] QList<charging::protocol::UserDto>
    listUsers() const override;

    // 站点与电桩的查询和增删改接口
    [[nodiscard]] QList<charging::protocol::StationDto>
    listActiveStations() const override;
    [[nodiscard]] QList<charging::protocol::StationDto>
    listStations() const override;
    [[nodiscard]] std::optional<charging::protocol::StationDto>
    findStationById(qint64 stationId) const override;
    [[nodiscard]] charging::protocol::StationDto createStation(
        charging::protocol::StationDto station,
        const QList<charging::protocol::PileDto> &piles) override;
    [[nodiscard]] bool updateStation(
        const charging::protocol::StationDto &station) override;
    [[nodiscard]] DeleteStationResult deleteStation(qint64 stationId) override;
    [[nodiscard]] QList<charging::protocol::PileDto>
    listPilesByStationId(qint64 stationId) const override;
    [[nodiscard]] QList<charging::protocol::PileDto>
    listPiles() const override;
    [[nodiscard]] charging::protocol::PileDto createPile(
        charging::protocol::PileDto pile) override;
    [[nodiscard]] DeletePileResult deletePile(qint64 pileId) override;
    [[nodiscard]] bool updatePile(
        const charging::protocol::PileDto &pile) override;

    // 订单读写接口，更新需传入期望状态
    [[nodiscard]] QList<charging::protocol::OrderDto>
    listOrders(std::optional<qint64> userId = std::nullopt) const override;
    [[nodiscard]] std::optional<charging::protocol::OrderDto>
    findOrderById(qint64 orderId) const override;
    [[nodiscard]] charging::protocol::OrderDto createOrder(
        charging::protocol::OrderDto order) override;
    [[nodiscard]] bool updateOrder(
        const charging::protocol::OrderDto &order,
        charging::protocol::OrderStatus expectedStatus) override;

private:
    // 内部辅助：记录本次操作是否成功、是否已打开连接
    void beginOperation() const noexcept;
    void failOperation(const QString &operation, const QString &detail) const;
    [[nodiscard]] bool requireOpen(const QString &operation) const;

    QString connectionName_;
    QSqlDatabase database_;
    bool transactionOpen_ = false;
    // 记录各表可用性与上次操作结果的状态位
    bool supportTicketsAvailable_ = false;
    bool repairTicketsAvailable_ = false;
    bool adminAccountsAvailable_ = false;
    mutable bool lastOperationSucceeded_ = true;
};

}  // namespace charging::server
