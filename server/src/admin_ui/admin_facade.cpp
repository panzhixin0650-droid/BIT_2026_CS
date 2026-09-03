#include "admin_facade.h"

#include "application/application_service.h"
#include "charging/protocol/protocol_constants.h"

namespace charging::server {

AdminFacade::AdminFacade(ApplicationService *service)
    : service_(service)
{
}

ServiceResult AdminFacade::login(const QString &username,
                                 const QString &password) const
{
    return service_ == nullptr
        ? ServiceResult::failure(charging::protocol::ErrorCode::InternalError,
                                 QStringLiteral("INTERNAL_ERROR"))
        : service_->loginAdmin(username, password);
}

ServiceResult AdminFacade::getDashboard(int days) const
{
    return service_->getDashboard(days);
}

ServiceResult AdminFacade::listStations(const QString &region,
                                        const QString &keyword) const
{
    return service_->listAdminStations(region, keyword);
}

ServiceResult AdminFacade::createStation(const QJsonObject &input) const
{
    return service_->createAdminStation(input);
}

ServiceResult AdminFacade::listPiles(std::optional<qint64> stationId) const
{
    return service_->listAdminPiles(stationId);
}

ServiceResult AdminFacade::restartPile(qint64 pileId) const
{
    return service_->restartAdminPile(pileId);
}

ServiceResult AdminFacade::listUsers(const QString &phoneKeyword) const
{
    return service_->listAdminUsers(phoneKeyword);
}

ServiceResult AdminFacade::setUserStatus(
    qint64 userId,
    charging::protocol::UserStatus status) const
{
    return service_->setAdminUserStatus(userId, status);
}

ServiceResult AdminFacade::listOrders() const
{
    return service_->listAdminOrders();
}

}  // namespace charging::server
