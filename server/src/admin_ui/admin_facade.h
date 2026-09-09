// 管理端门面声明：界面统一经此访问服务，不直连仓储
#pragma once

#include "application/service_result.h"

#include "charging/protocol/dto.h"

#include <QJsonObject>
#include <QDate>
#include <QString>

#include <optional>

namespace charging::server {

class ApplicationService;

// In-process administrator boundary. The UI will call this facade rather than
// accessing Repository or SQL directly.
// 进程内调用，不走 TCP 协议
class AdminFacade final {
public:
    explicit AdminFacade(ApplicationService *service);

    // 登录与退出会改变门面内保存的管理员身份
    [[nodiscard]] ServiceResult login(const QString &username,
                                      const QString &password);
    void logout();
    [[nodiscard]] ServiceResult currentAdmin() const;
    [[nodiscard]] ServiceResult listAdmins(const QString &keyword = {},
                                           const QString &status = {}) const;
    [[nodiscard]] ServiceResult createAdmin(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult updateAdmin(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult changePassword(const QString &currentPassword,
                                               const QString &newPassword);
    // 看板查询提供天数与日期区间两种重载
    [[nodiscard]] ServiceResult getDashboard(int days) const;
    [[nodiscard]] ServiceResult getDashboard(const QDate &startDate,
                                             const QDate &endDate) const;
    [[nodiscard]] ServiceResult listStations(const QString &region = {},
                                             const QString &keyword = {}) const;
    [[nodiscard]] ServiceResult createStation(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult updateStation(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult setStationStatus(
        qint64 stationId,
        charging::protocol::StationStatus status) const;
    [[nodiscard]] ServiceResult deleteStation(qint64 stationId) const;
    [[nodiscard]] ServiceResult listPiles(
        std::optional<qint64> stationId = std::nullopt) const;
    [[nodiscard]] ServiceResult createPile(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult updatePile(const QJsonObject &input) const;
    [[nodiscard]] ServiceResult deletePile(qint64 pileId) const;
    [[nodiscard]] ServiceResult setPileStatus(
        qint64 pileId,
        charging::protocol::PileStatus status) const;
    [[nodiscard]] ServiceResult restartPile(qint64 pileId) const;
    [[nodiscard]] ServiceResult listUsers(const QString &phoneKeyword = {}) const;
    [[nodiscard]] ServiceResult setUserStatus(
        qint64 userId,
        charging::protocol::UserStatus status) const;
    [[nodiscard]] ServiceResult listOrders() const;
    [[nodiscard]] ServiceResult listSupportTickets(std::optional<qint64> beforeId = {}) const;
    [[nodiscard]] ServiceResult updateSupportTicket(const QJsonObject &input) const;

private:
    // 当前管理员编号为 0 表示未登录
    ApplicationService *service_ = nullptr;
    qint64 currentAdminId_ = 0;
};

}  // namespace charging::server
