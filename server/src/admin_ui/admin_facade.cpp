// 管理端门面实现：进程内转调应用服务，并保存当前管理员身份
#include "admin_facade.h"

#include "application/application_service.h"
#include "charging/protocol/protocol_constants.h"

namespace charging::server {

// 持有应用服务指针，本身不做业务判断
AdminFacade::AdminFacade(ApplicationService *service)
    : service_(service)
{
}

// 登录成功才记录管理员编号，后续调用据此鉴权
ServiceResult AdminFacade::login(const QString &username,
                                 const QString &password)
{
    currentAdminId_ = 0; // A failed login must not retain a previous administrator.
    ServiceResult result = service_ == nullptr
        ? ServiceResult::failure(charging::protocol::ErrorCode::InternalError,
                                 QStringLiteral("INTERNAL_ERROR"))
        : service_->loginAdmin(username, password);
    if (result.ok()) {
        currentAdminId_ = result.data.value(QStringLiteral("admin")).toObject()
                              .value(QStringLiteral("adminId")).toInteger();
    }
    return result;
}

// 退出登录只清空当前管理员编号
void AdminFacade::logout()
{
    currentAdminId_ = 0;
}

ServiceResult AdminFacade::currentAdmin() const
{
    return service_->getAdminProfile(currentAdminId_);
}

ServiceResult AdminFacade::listAdmins(const QString &keyword,
                                      const QString &status) const
{
    return service_->listAdminAccounts(currentAdminId_, keyword, status);
}

ServiceResult AdminFacade::createAdmin(const QJsonObject &input) const
{
    return service_->createAdminAccount(currentAdminId_, input);
}

ServiceResult AdminFacade::updateAdmin(const QJsonObject &input) const
{
    return service_->updateAdminAccount(currentAdminId_, input);
}

// 改密成功后清空身份，要求重新登录
ServiceResult AdminFacade::changePassword(const QString &currentPassword,
                                          const QString &newPassword)
{
    ServiceResult result = service_->changeAdminPassword(currentAdminId_,
                                                         currentPassword,
                                                         newPassword);
    if (result.ok()) currentAdminId_ = 0;
    return result;
}

// 看板可按天数或日期区间查询
ServiceResult AdminFacade::getDashboard(int days) const
{
    return service_->getDashboard(currentAdminId_, days);
}

ServiceResult AdminFacade::getDashboard(const QDate &startDate,
                                        const QDate &endDate) const
{
    return service_->getDashboard(currentAdminId_, startDate, endDate);
}

ServiceResult AdminFacade::listStations(const QString &region,
                                        const QString &keyword) const
{
    return service_->listAdminStations(currentAdminId_, region, keyword);
}

ServiceResult AdminFacade::createStation(const QJsonObject &input) const
{
    return service_->createAdminStation(currentAdminId_, input);
}

ServiceResult AdminFacade::updateStation(const QJsonObject &input) const
{
    return service_->updateAdminStation(currentAdminId_, input);
}

// 站点启停等状态变更交由服务层校验权限
ServiceResult AdminFacade::setStationStatus(qint64 stationId,
                                            charging::protocol::StationStatus status) const
{
    return service_->setAdminStationStatus(currentAdminId_, stationId, status);
}

ServiceResult AdminFacade::deleteStation(qint64 stationId) const
{
    return service_->deleteAdminStation(currentAdminId_, stationId);
}

ServiceResult AdminFacade::listPiles(std::optional<qint64> stationId) const
{
    return service_->listAdminPiles(currentAdminId_, stationId);
}

ServiceResult AdminFacade::createPile(const QJsonObject &input) const
{
    return service_->createAdminPile(currentAdminId_, input);
}

ServiceResult AdminFacade::updatePile(const QJsonObject &input) const
{
    return service_->updateAdminPile(currentAdminId_, input);
}

ServiceResult AdminFacade::deletePile(qint64 pileId) const
{
    return service_->deleteAdminPile(currentAdminId_, pileId);
}

ServiceResult AdminFacade::setPileStatus(qint64 pileId,
                                         charging::protocol::PileStatus status) const
{
    return service_->setAdminPileStatus(currentAdminId_, pileId, status);
}

// 重启电桩前由服务层判断桩是否占用
ServiceResult AdminFacade::restartPile(qint64 pileId) const
{
    return service_->restartAdminPile(currentAdminId_, pileId);
}

ServiceResult AdminFacade::listUsers(const QString &phoneKeyword) const
{
    return service_->listAdminUsers(currentAdminId_, phoneKeyword);
}

ServiceResult AdminFacade::setUserStatus(
    qint64 userId,
    charging::protocol::UserStatus status) const
{
    return service_->setAdminUserStatus(currentAdminId_, userId, status);
}

ServiceResult AdminFacade::listOrders() const
{
    return service_->listAdminOrders(currentAdminId_);
}

// 工单接口先确认已登录且服务可用，否则返回无权限
ServiceResult AdminFacade::listSupportTickets(std::optional<qint64> beforeId) const
{
    if (currentAdminId_ <= 0 || !service_)
        return ServiceResult::failure(charging::protocol::ErrorCode::Forbidden, QStringLiteral("FORBIDDEN"));
    return service_->listAdminSupportTickets(currentAdminId_, beforeId);
}

ServiceResult AdminFacade::updateSupportTicket(const QJsonObject &input) const
{
    if (currentAdminId_ <= 0 || !service_)
        return ServiceResult::failure(charging::protocol::ErrorCode::Forbidden, QStringLiteral("FORBIDDEN"));
    return service_->updateAdminSupportTicket(currentAdminId_, input);
}

}  // namespace charging::server
