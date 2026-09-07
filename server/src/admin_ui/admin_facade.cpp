#include "admin_facade.h"

#include "application/application_service.h"
#include "charging/protocol/protocol_constants.h"

namespace charging::server {

AdminFacade::AdminFacade(ApplicationService *service)
    : service_(service)
{
}

ServiceResult AdminFacade::login(const QString &username,
                                 const QString &password)
{
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

ServiceResult AdminFacade::changePassword(const QString &currentPassword,
                                          const QString &newPassword)
{
    ServiceResult result = service_->changeAdminPassword(currentAdminId_,
                                                         currentPassword,
                                                         newPassword);
    if (result.ok()) currentAdminId_ = 0;
    return result;
}

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

}  // namespace charging::server
