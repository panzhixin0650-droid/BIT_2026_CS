#include "application_service.h"

#include "application/session_store.h"
#include "adapters/mock_pile.h"
#include "adapters/mock_prediction_provider.h"
#include "charging/protocol/protocol_constants.h"
#include "persistence/i_repository.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace charging::server {
namespace {

using namespace charging::protocol;

constexpr double kEarthRadiusKm = 6371.0;

ServiceResult invalidRequest()
{
    return ServiceResult::failure(ErrorCode::InvalidRequest,
                                  QStringLiteral("INVALID_REQUEST"));
}

ServiceResult internalError()
{
    return ServiceResult::failure(ErrorCode::InternalError,
                                  QStringLiteral("INTERNAL_ERROR"));
}

bool readString(const QJsonObject &input, const QString &key, QString *value)
{
    const QJsonValue item = input.value(key);
    if (!item.isString()) {
        return false;
    }
    *value = item.toString();
    return true;
}

bool readInteger(const QJsonObject &input, const QString &key, qint64 *value)
{
    const QJsonValue item = input.value(key);
    if (!item.isDouble()) {
        return false;
    }
    const double number = item.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number
        || number < static_cast<double>(std::numeric_limits<qint64>::min())
        || number > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    *value = static_cast<qint64>(number);
    return true;
}

double degreesToRadians(double degrees)
{
    return degrees * 3.14159265358979323846 / 180.0;
}

double distanceKm(double fromLongitude,
                  double fromLatitude,
                  double toLongitude,
                  double toLatitude)
{
    const double latitudeDelta = degreesToRadians(toLatitude - fromLatitude);
    const double longitudeDelta = degreesToRadians(toLongitude - fromLongitude);
    const double fromLatitudeRadians = degreesToRadians(fromLatitude);
    const double toLatitudeRadians = degreesToRadians(toLatitude);
    const double haversine = std::pow(std::sin(latitudeDelta / 2.0), 2.0)
        + std::cos(fromLatitudeRadians) * std::cos(toLatitudeRadians)
            * std::pow(std::sin(longitudeDelta / 2.0), 2.0);
    const double distance = 2.0 * kEarthRadiusKm
        * std::asin(std::min(1.0, std::sqrt(haversine)));
    return std::round(distance * 100.0) / 100.0;
}

QJsonArray stationsToJson(const QList<StationDto> &stations)
{
    QJsonArray items;
    for (const StationDto &station : stations) {
        items.append(toJson(station));
    }
    return items;
}

QJsonArray pilesToJson(const QList<PileDto> &piles)
{
    QJsonArray items;
    for (const PileDto &pile : piles) {
        items.append(toJson(pile));
    }
    return items;
}

}  // namespace

ApplicationService::ApplicationService(IRepository *repository,
                                       SessionStore *sessions,
                                       MockPile *pileGateway,
                                       MockPredictionProvider *predictions,
                                       QObject *parent)
    : QObject(parent)
    , repository_(repository)
    , sessions_(sessions)
    , pileGateway_(pileGateway)
    , predictions_(predictions)
{
}

ServiceResult ApplicationService::ping(const QJsonObject &input) const
{
    const QString echoKey = QStringLiteral("echo");
    if (input.contains(echoKey) && !input.value(echoKey).isString()) {
        return ServiceResult::failure(charging::protocol::ErrorCode::InvalidRequest,
                                      QStringLiteral("INVALID_REQUEST"));
    }

    QJsonObject data;
    if (input.contains(echoKey)) {
        data.insert(echoKey, input.value(echoKey));
    }
    data.insert(QStringLiteral("serverTime"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return ServiceResult::success(std::move(data));
}

ServiceResult ApplicationService::loginUser(const QJsonObject &input)
{
    if (repository_ == nullptr || sessions_ == nullptr) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }

    QString phone;
    static const QRegularExpression phonePattern(QStringLiteral("^[0-9]{11}$"));
    if (!readString(input, QStringLiteral("phone"), &phone)
        || !phonePattern.match(phone).hasMatch()) {
        return invalidRequest();
    }

    bool isNewUser = false;
    std::optional<UserDto> user = repository_->findUserByPhone(phone);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    if (!user.has_value()) {
        isNewUser = true;
        user = repository_->createUser(
            phone,
            QStringLiteral("用户") + phone.right(4),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!repository_->lastOperationSucceeded() || !user.has_value()
            || user->userId <= 0) {
            return internalError();
        }
    }

    if (user->status == UserStatus::Frozen) {
        return ServiceResult::failure(ErrorCode::Forbidden,
                                      QStringLiteral("FORBIDDEN"));
    }

    return ServiceResult::success({
        {QStringLiteral("token"), sessions_->create(user->userId)},
        {QStringLiteral("isNewUser"), isNewUser},
        {QStringLiteral("user"), toJson(*user)},
    });
}

ServiceResult ApplicationService::logout(const QString &token)
{
    if (sessions_ == nullptr || token.isEmpty()
        || !sessions_->userIdForToken(token).has_value()) {
        return ServiceResult::failure(ErrorCode::InvalidSession,
                                      QStringLiteral("INVALID_SESSION"));
    }
    if (!sessions_->remove(token)) {
        return ServiceResult::failure(ErrorCode::InvalidSession,
                                      QStringLiteral("INVALID_SESSION"));
    }
    return ServiceResult::success({{QStringLiteral("success"), true}});
}

ServiceResult ApplicationService::getProfile(const QString &token) const
{
    ServiceResult failure;
    const std::optional<qint64> userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) {
        return failure;
    }
    const std::optional<UserDto> user = repository_->findUserById(*userId);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    if (!user.has_value()) {
        return ServiceResult::failure(ErrorCode::NotFound,
                                      QStringLiteral("NOT_FOUND"));
    }
    return ServiceResult::success({{QStringLiteral("user"), toJson(*user)}});
}

ServiceResult ApplicationService::updateProfile(const QString &token,
                                                const QJsonObject &input)
{
    ServiceResult failure;
    const std::optional<qint64> userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) {
        return failure;
    }

    QString nickname;
    if (!readString(input, QStringLiteral("nickname"), &nickname)
        || nickname.size() < 1 || nickname.size() > 32) {
        return invalidRequest();
    }

    std::optional<UserDto> user = repository_->findUserById(*userId);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    if (!user.has_value()) {
        return ServiceResult::failure(ErrorCode::NotFound,
                                      QStringLiteral("NOT_FOUND"));
    }
    user->nickname = nickname;
    if (!repository_->updateUser(*user)) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    return ServiceResult::success({{QStringLiteral("user"), toJson(*user)}});
}

ServiceResult ApplicationService::recharge(const QString &token,
                                           const QJsonObject &input)
{
    ServiceResult failure;
    const std::optional<qint64> userId = authenticatedUserId(token, &failure);
    if (!userId.has_value()) {
        return failure;
    }

    qint64 amountCents = 0;
    if (!readInteger(input, QStringLiteral("amountCents"), &amountCents)
        || amountCents < 1 || amountCents > 1000000) {
        return invalidRequest();
    }

    const std::optional<UserDto> user =
        repository_->addUserBalance(*userId, amountCents);
    if (!repository_->lastOperationSucceeded() || !user.has_value()) {
        return internalError();
    }
    return ServiceResult::success({
        {QStringLiteral("balanceCents"), static_cast<double>(user->balanceCents)},
    });
}

ServiceResult ApplicationService::listStations(const QString &token,
                                               const QJsonObject &input) const
{
    ServiceResult failure;
    if (!authenticatedUserId(token, &failure).has_value()) {
        return failure;
    }

    const bool hasLongitude = input.contains(QStringLiteral("longitude"));
    const bool hasLatitude = input.contains(QStringLiteral("latitude"));
    if (hasLongitude != hasLatitude) {
        return invalidRequest();
    }

    double longitude = 0.0;
    double latitude = 0.0;
    if (hasLongitude) {
        const QJsonValue longitudeValue = input.value(QStringLiteral("longitude"));
        const QJsonValue latitudeValue = input.value(QStringLiteral("latitude"));
        if (!longitudeValue.isDouble() || !latitudeValue.isDouble()) {
            return invalidRequest();
        }
        longitude = longitudeValue.toDouble();
        latitude = latitudeValue.toDouble();
        if (!std::isfinite(longitude) || !std::isfinite(latitude)
            || longitude < -180.0 || longitude > 180.0
            || latitude < -90.0 || latitude > 90.0) {
            return invalidRequest();
        }
    }

    QString region;
    if (input.contains(QStringLiteral("region"))
        && !readString(input, QStringLiteral("region"), &region)) {
        return invalidRequest();
    }
    QString keyword;
    if (input.contains(QStringLiteral("keyword"))
        && !readString(input, QStringLiteral("keyword"), &keyword)) {
        return invalidRequest();
    }

    const QList<StationDto> storedStations = repository_->listActiveStations();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }

    QList<StationDto> stations;
    for (StationDto station : storedStations) {
        if (!region.isEmpty() && station.region != region) {
            continue;
        }
        if (!keyword.isEmpty()
            && !station.name.contains(keyword, Qt::CaseInsensitive)
            && !station.address.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        if (hasLongitude) {
            station.distanceKm = distanceKm(longitude, latitude,
                                            station.longitude, station.latitude);
        }
        station.predictedCongestion = predictions_ == nullptr
                || !predictions_->available()
            ? std::nullopt
            : predictions_->congestionForStation(station.stationId);
        station.recommended = false;
        stations.append(station);
    }

    std::sort(stations.begin(), stations.end(),
              [hasLongitude](const StationDto &left, const StationDto &right) {
                  if (hasLongitude && left.distanceKm != right.distanceKm) {
                      return left.distanceKm.value_or(0.0) < right.distanceKm.value_or(0.0);
                  }
                  return left.stationId < right.stationId;
              });

    auto recommended = std::find_if(stations.begin(), stations.end(),
                                    [](const StationDto &station) {
                                        return station.predictedCongestion
                                            == CongestionLevel::Low;
                                    });
    if (recommended != stations.end()) {
        recommended->recommended = true;
    }

    return ServiceResult::success({
        {QStringLiteral("items"), stationsToJson(stations)},
    });
}

ServiceResult ApplicationService::getStation(const QString &token,
                                             const QJsonObject &input) const
{
    ServiceResult failure;
    if (!authenticatedUserId(token, &failure).has_value()) {
        return failure;
    }

    qint64 stationId = 0;
    if (!readInteger(input, QStringLiteral("stationId"), &stationId)
        || stationId <= 0) {
        return invalidRequest();
    }

    std::optional<StationDto> station = repository_->findStationById(stationId);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    if (!station.has_value() || station->status != StationStatus::Active) {
        return ServiceResult::failure(ErrorCode::NotFound,
                                      QStringLiteral("NOT_FOUND"));
    }
    station->distanceKm.reset();
    station->predictedCongestion = predictions_ == nullptr
            || !predictions_->available()
        ? std::nullopt
        : predictions_->congestionForStation(stationId);
    station->recommended = false;

    const QList<PileDto> piles = repository_->listPilesByStationId(stationId);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }

    return ServiceResult::success({
        {QStringLiteral("station"), toJson(*station)},
        {QStringLiteral("piles"), pilesToJson(piles)},
    });
}

std::optional<qint64> ApplicationService::authenticatedUserId(
    const QString &token,
    ServiceResult *failure) const
{
    if (repository_ == nullptr || sessions_ == nullptr) {
        *failure = ServiceResult::failure(ErrorCode::InternalError,
                                          QStringLiteral("INTERNAL_ERROR"));
        return std::nullopt;
    }

    const std::optional<qint64> userId = sessions_->userIdForToken(token);
    if (!userId.has_value()) {
        *failure = ServiceResult::failure(ErrorCode::InvalidSession,
                                          QStringLiteral("INVALID_SESSION"));
        return std::nullopt;
    }

    const std::optional<UserDto> user = repository_->findUserById(*userId);
    if (!repository_->lastOperationSucceeded()) {
        *failure = internalError();
        return std::nullopt;
    }
    if (!user.has_value()) {
        *failure = ServiceResult::failure(ErrorCode::InvalidSession,
                                          QStringLiteral("INVALID_SESSION"));
        return std::nullopt;
    }
    if (user->status == UserStatus::Frozen) {
        *failure = ServiceResult::failure(ErrorCode::Forbidden,
                                          QStringLiteral("FORBIDDEN"));
        return std::nullopt;
    }
    return userId;
}

ServiceResult ApplicationService::loginAdmin(const QString &username,
                                             const QString &password) const
{
    if (repository_ == nullptr || username.isEmpty() || password.isEmpty()) {
        return ServiceResult::failure(ErrorCode::InvalidCredentials,
                                      QStringLiteral("INVALID_CREDENTIALS"));
    }
    const std::optional<AdminRecord> admin =
        repository_->findAdminByUsername(username);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    const QString passwordHash = QString::fromLatin1(QCryptographicHash::hash(
        password.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!admin.has_value() || admin->passwordHash != passwordHash) {
        return ServiceResult::failure(ErrorCode::InvalidCredentials,
                                      QStringLiteral("INVALID_CREDENTIALS"));
    }
    return ServiceResult::success({
        {QStringLiteral("adminId"), static_cast<double>(admin->adminId)},
        {QStringLiteral("displayName"), admin->displayName},
    });
}

ServiceResult ApplicationService::getDashboard(int days) const
{
    if (days != 7 && days != 30) return invalidRequest();
    const QTimeZone businessZone("Asia/Shanghai");
    const QDate today = QDateTime::currentDateTimeUtc().toTimeZone(businessZone).date();
    return getDashboard(today.addDays(1 - days), today);
}

ServiceResult ApplicationService::getDashboard(const QDate &startDate,
                                                const QDate &endDate) const
{
    if (repository_ == nullptr || !startDate.isValid() || !endDate.isValid()
        || startDate > endDate || startDate.daysTo(endDate) > 365) {
        return invalidRequest();
    }

    const QList<OrderDto> orders = repository_->listOrders();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    const QList<StationDto> stations = repository_->listStations();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    const QList<PileDto> piles = repository_->listPiles();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    const QTimeZone businessZone("Asia/Shanghai");
    const QDate today = QDateTime::currentDateTimeUtc().toTimeZone(businessZone).date();

    qint64 todayRevenue = 0;
    qint64 monthRevenue = 0;
    qint64 totalRevenue = 0;
    QHash<QDate, qint64> revenueByDate;
    for (const OrderDto &order : orders) {
        if (order.status != OrderStatus::Completed || !order.paidAt.has_value()) {
            continue;
        }
        const QDate paidDate = QDateTime::fromString(*order.paidAt, Qt::ISODate)
                                   .toTimeZone(businessZone).date();
        totalRevenue += order.amountCents;
        revenueByDate[paidDate] += order.amountCents;
        if (paidDate == today) {
            todayRevenue += order.amountCents;
        }
        if (paidDate.year() == today.year() && paidDate.month() == today.month()) {
            monthRevenue += order.amountCents;
        }
    }

    qint64 idle = 0;
    qint64 inUse = 0;
    qint64 fault = 0;
    for (const PileDto &pile : piles) {
        if (pile.status == PileStatus::Idle) {
            ++idle;
        } else if (pile.status == PileStatus::Reserved
                   || pile.status == PileStatus::Charging) {
            ++inUse;
        } else {
            ++fault;
        }
    }

    QJsonArray revenuePoints;
    for (QDate date = startDate; date <= endDate; date = date.addDays(1)) {
        revenuePoints.append(QJsonObject{
            {QStringLiteral("date"), date.toString(Qt::ISODate)},
            {QStringLiteral("revenueCents"),
             static_cast<double>(revenueByDate.value(date, 0))},
        });
    }

    return ServiceResult::success({
        {QStringLiteral("todayRevenueCents"), static_cast<double>(todayRevenue)},
        {QStringLiteral("monthRevenueCents"), static_cast<double>(monthRevenue)},
        {QStringLiteral("totalRevenueCents"), static_cast<double>(totalRevenue)},
        {QStringLiteral("stationCount"), stations.size()},
        {QStringLiteral("pileCount"), piles.size()},
        {QStringLiteral("pileStates"), QJsonObject{
             {QStringLiteral("idle"), static_cast<double>(idle)},
             {QStringLiteral("inUse"), static_cast<double>(inUse)},
             {QStringLiteral("fault"), static_cast<double>(fault)},
         }},
        {QStringLiteral("revenuePoints"), revenuePoints},
        {QStringLiteral("predictions"), QJsonArray{}},
        {QStringLiteral("generatedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    });
}

ServiceResult ApplicationService::listAdminStations(const QString &region,
                                                    const QString &keyword) const
{
    if (repository_ == nullptr) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    const QList<StationDto> storedStations = repository_->listStations();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    QList<StationDto> result;
    for (StationDto station : storedStations) {
        if (!region.isEmpty() && station.region != region) {
            continue;
        }
        if (!keyword.isEmpty()
            && !station.name.contains(keyword, Qt::CaseInsensitive)
            && !station.address.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        station.distanceKm.reset();
        station.predictedCongestion.reset();
        station.recommended = false;
        result.append(station);
    }
    return ServiceResult::success({
        {QStringLiteral("items"), stationsToJson(result)},
    });
}

ServiceResult ApplicationService::createAdminStation(const QJsonObject &input)
{
    if (repository_ == nullptr) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    StationDto station;
    qint64 price = 0;
    const QJsonValue longitudeValue = input.value(QStringLiteral("longitude"));
    const QJsonValue latitudeValue = input.value(QStringLiteral("latitude"));
    if (!readString(input, QStringLiteral("name"), &station.name)
        || !readString(input, QStringLiteral("region"), &station.region)
        || !readString(input, QStringLiteral("address"), &station.address)
        || station.name.isEmpty() || station.name.size() > 64
        || station.region.isEmpty() || station.region.size() > 64
        || station.address.isEmpty() || station.address.size() > 200
        || !longitudeValue.isDouble() || !latitudeValue.isDouble()
        || !readInteger(input, QStringLiteral("priceCentsPerKwh"), &price)
        || !input.value(QStringLiteral("piles")).isArray()) {
        return invalidRequest();
    }
    station.longitude = longitudeValue.toDouble();
    station.latitude = latitudeValue.toDouble();
    if (!std::isfinite(station.longitude) || !std::isfinite(station.latitude)
        || station.longitude < -180.0 || station.longitude > 180.0
        || station.latitude < -90.0 || station.latitude > 90.0
        || price <= 0) {
        return invalidRequest();
    }
    const QJsonArray pileItems = input.value(QStringLiteral("piles")).toArray();
    if (pileItems.size() > 100) return invalidRequest();
    QList<PileDto> piles;
    QSet<QString> pileCodes;
    for (const QJsonValue &value : pileItems) {
        if (!value.isObject()) return invalidRequest();
        const QJsonObject pileInput = value.toObject();
        QString pileCode;
        QString pileType;
        const QJsonValue powerValue = pileInput.value(QStringLiteral("ratedPowerKw"));
        if (!readString(pileInput, QStringLiteral("pileCode"), &pileCode)
            || !readString(pileInput, QStringLiteral("pileType"), &pileType)
            || !powerValue.isDouble()) {
            return invalidRequest();
        }
        pileCode = pileCode.trimmed();
        const QString normalizedCode = pileCode.toCaseFolded();
        const double power = powerValue.toDouble();
        if (pileCode.isEmpty() || pileCode.size() > 64 || pileCodes.contains(normalizedCode)
            || !std::isfinite(power) || power <= 0.0 || power > 1000.0
            || (pileType != QStringLiteral("FAST") && pileType != QStringLiteral("SLOW"))) {
            return invalidRequest();
        }
        pileCodes.insert(normalizedCode);
        PileDto pile;
        pile.pileCode = pileCode;
        pile.pileType = pileType == QStringLiteral("FAST") ? PileType::Fast : PileType::Slow;
        pile.ratedPowerKw = power;
        pile.status = PileStatus::Idle;
        piles.append(pile);
    }
    const QList<PileDto> existingPiles = repository_->listPiles();
    if (!repository_->lastOperationSucceeded()) return internalError();
    if (std::any_of(existingPiles.cbegin(), existingPiles.cend(), [&pileCodes](const PileDto &pile) {
            return pileCodes.contains(pile.pileCode.toCaseFolded());
        })) {
        return ServiceResult::failure(ErrorCode::InvalidRequest,
                                      QStringLiteral("PILE_CODE_EXISTS"));
    }
    station.priceCentsPerKwh = price;
    station.status = StationStatus::Active;
    station = repository_->createStation(station, piles);
    if (!repository_->lastOperationSucceeded() || station.stationId <= 0) {
        return internalError();
    }
    const QList<PileDto> createdPiles =
        repository_->listPilesByStationId(station.stationId);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    return ServiceResult::success({
        {QStringLiteral("station"), toJson(station)},
        {QStringLiteral("piles"), pilesToJson(createdPiles)},
    });
}

ServiceResult ApplicationService::deleteAdminStation(qint64 stationId)
{
    if (repository_ == nullptr || stationId <= 0) {
        return invalidRequest();
    }

    switch (repository_->deleteStation(stationId)) {
    case DeleteStationResult::Deleted:
        return ServiceResult::success({{QStringLiteral("success"), true}});
    case DeleteStationResult::NotFound:
        return ServiceResult::failure(ErrorCode::NotFound,
                                      QStringLiteral("NOT_FOUND"));
    case DeleteStationResult::HasOrders:
        return ServiceResult::failure(ErrorCode::IllegalOrderState,
                                      QStringLiteral("ILLEGAL_ORDER_STATE"));
    case DeleteStationResult::StorageError:
        return internalError();
    }
    return internalError();
}

ServiceResult ApplicationService::updateAdminStation(const QJsonObject &input)
{
    if (repository_ == nullptr) return internalError();
    qint64 stationId = 0;
    QString name;
    QString region;
    QString address;
    qint64 price = 0;
    const QJsonValue longitudeValue = input.value(QStringLiteral("longitude"));
    const QJsonValue latitudeValue = input.value(QStringLiteral("latitude"));
    QString statusText;
    if (!readInteger(input, QStringLiteral("stationId"), &stationId)
        || !readString(input, QStringLiteral("name"), &name)
        || !readString(input, QStringLiteral("region"), &region)
        || !readString(input, QStringLiteral("address"), &address)
        || !readInteger(input, QStringLiteral("priceCentsPerKwh"), &price)
        || !readString(input, QStringLiteral("status"), &statusText)
        || !longitudeValue.isDouble() || !latitudeValue.isDouble()
        || stationId <= 0 || name.trimmed().isEmpty() || name.size() > 64
        || region.trimmed().isEmpty() || region.size() > 64
        || address.trimmed().isEmpty() || address.size() > 200
        || price <= 0 || !std::isfinite(longitudeValue.toDouble())
        || !std::isfinite(latitudeValue.toDouble())
        || longitudeValue.toDouble() < -180.0 || longitudeValue.toDouble() > 180.0
        || latitudeValue.toDouble() < -90.0 || latitudeValue.toDouble() > 90.0) {
        return invalidRequest();
    }
    StationStatus status;
    if (statusText == QStringLiteral("ACTIVE")) status = StationStatus::Active;
    else if (statusText == QStringLiteral("DISABLED")) status = StationStatus::Disabled;
    else return invalidRequest();
    StationDto station;
    station.stationId = stationId;
    station.name = name.trimmed();
    station.region = region.trimmed();
    station.address = address.trimmed();
    station.longitude = longitudeValue.toDouble();
    station.latitude = latitudeValue.toDouble();
    station.priceCentsPerKwh = price;
    station.status = status;
    if (!repository_->updateStation(station)) {
        if (!repository_->lastOperationSucceeded()) return internalError();
        return ServiceResult::failure(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));
    }
    const auto updated = repository_->findStationById(stationId);
    if (!repository_->lastOperationSucceeded() || !updated.has_value()) return internalError();
    return ServiceResult::success({{QStringLiteral("station"), toJson(*updated)}});
}

ServiceResult ApplicationService::setAdminStationStatus(qint64 stationId,
                                                        StationStatus status)
{
    if (repository_ == nullptr || stationId <= 0
        || (status != StationStatus::Active && status != StationStatus::Disabled)) {
        return invalidRequest();
    }
    const auto existing = repository_->findStationById(stationId);
    if (!repository_->lastOperationSucceeded()) return internalError();
    if (!existing.has_value()) return ServiceResult::failure(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));

    // A station can only be taken offline when none of its piles is reserved
    // or charging.  Once the check passes, idle online piles are taken offline
    // together with the station; faulted and already-offline piles retain
    // their state for later maintenance handling.  Enabling a station never
    // force-starts piles, so operators can bring hardware online explicitly.
    QList<PileDto> changedPiles;
    if (status == StationStatus::Disabled) {
        const QList<PileDto> stationPiles = repository_->listPilesByStationId(stationId);
        if (!repository_->lastOperationSucceeded()) return internalError();
        for (const PileDto &pile : stationPiles) {
            if (pile.status == PileStatus::Reserved || pile.status == PileStatus::Charging) {
                return ServiceResult::failure(ErrorCode::IllegalOrderState,
                                              QStringLiteral("STATION_HAS_ACTIVE_PILES"));
            }
        }
        for (PileDto pile : stationPiles) {
            if (pile.status != PileStatus::Idle) continue;
            pile.status = PileStatus::Offline;
            if (!repository_->updatePile(pile)) {
                // Best-effort rollback keeps a failed station operation from
                // leaving a subset of its idle piles offline.
                for (const PileDto &changed : changedPiles) {
                    PileDto restored = changed;
                    restored.status = PileStatus::Idle;
                    repository_->updatePile(restored);
                }
                if (!repository_->lastOperationSucceeded()) return internalError();
                return internalError();
            }
            changedPiles.append(pile);
        }
    }

    StationDto station = *existing;
    station.status = status;
    if (!repository_->updateStation(station)) {
        if (!repository_->lastOperationSucceeded()) return internalError();
        for (const PileDto &changed : changedPiles) {
            PileDto restored = changed;
            restored.status = PileStatus::Idle;
            repository_->updatePile(restored);
        }
        return ServiceResult::failure(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));
    }
    const auto updated = repository_->findStationById(stationId);
    if (!repository_->lastOperationSucceeded() || !updated.has_value()) return internalError();
    return ServiceResult::success({{QStringLiteral("station"), toJson(*updated)}});
}

ServiceResult ApplicationService::listAdminPiles(
    std::optional<qint64> stationId) const
{
    if (repository_ == nullptr) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    const QList<PileDto> piles = stationId.has_value()
        ? repository_->listPilesByStationId(*stationId)
        : repository_->listPiles();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    return ServiceResult::success({
        {QStringLiteral("items"), pilesToJson(piles)},
    });
}

ServiceResult ApplicationService::createAdminPile(const QJsonObject &input)
{
    if (repository_ == nullptr) return internalError();
    qint64 stationId = 0;
    QString pileCode;
    QString pileType;
    const QJsonValue powerValue = input.value(QStringLiteral("ratedPowerKw"));
    if (!readInteger(input, QStringLiteral("stationId"), &stationId)
        || !readString(input, QStringLiteral("pileCode"), &pileCode)
        || !readString(input, QStringLiteral("pileType"), &pileType)
        || !powerValue.isDouble() || stationId <= 0 || pileCode.trimmed().isEmpty()
        || pileCode.size() > 64 || !std::isfinite(powerValue.toDouble())
        || powerValue.toDouble() <= 0.0
        || (pileType != QStringLiteral("FAST") && pileType != QStringLiteral("SLOW"))) {
        return invalidRequest();
    }
    const auto station = repository_->findStationById(stationId);
    if (!repository_->lastOperationSucceeded()) return internalError();
    if (!station.has_value() || station->status != StationStatus::Active) {
        return ServiceResult::failure(ErrorCode::InvalidRequest,
                                      QStringLiteral("INVALID_STATION"));
    }
    const QList<PileDto> existingPiles = repository_->listPiles();
    if (!repository_->lastOperationSucceeded()) return internalError();
    if (std::any_of(existingPiles.cbegin(), existingPiles.cend(), [&pileCode](const PileDto &pile) {
            return pile.pileCode.compare(pileCode.trimmed(), Qt::CaseInsensitive) == 0;
        })) {
        return ServiceResult::failure(ErrorCode::InvalidRequest,
                                      QStringLiteral("PILE_CODE_EXISTS"));
    }
    PileDto pile;
    pile.stationId = stationId;
    pile.pileCode = pileCode.trimmed();
    pile.pileType = pileType == QStringLiteral("FAST") ? PileType::Fast : PileType::Slow;
    pile.ratedPowerKw = powerValue.toDouble();
    pile = repository_->createPile(pile);
    if (!repository_->lastOperationSucceeded() || pile.pileId <= 0) return internalError();
    return ServiceResult::success({{QStringLiteral("pile"), toJson(pile)}});
}

ServiceResult ApplicationService::updateAdminPile(const QJsonObject &input)
{
    if (repository_ == nullptr) return internalError();

    qint64 pileId = 0;
    QString pileCode;
    QString pileType;
    const QJsonValue powerValue = input.value(QStringLiteral("ratedPowerKw"));
    if (!readInteger(input, QStringLiteral("pileId"), &pileId)
        || !readString(input, QStringLiteral("pileCode"), &pileCode)
        || !readString(input, QStringLiteral("pileType"), &pileType)
        || !powerValue.isDouble() || pileId <= 0
        || pileCode.trimmed().isEmpty() || pileCode.size() > 64
        || !std::isfinite(powerValue.toDouble()) || powerValue.toDouble() <= 0.0
        || powerValue.toDouble() > 1000.0
        || (pileType != QStringLiteral("FAST") && pileType != QStringLiteral("SLOW"))) {
        return invalidRequest();
    }

    QList<PileDto> piles = repository_->listPiles();
    if (!repository_->lastOperationSucceeded()) return internalError();
    auto found = std::find_if(piles.begin(), piles.end(), [pileId](const PileDto &pile) {
        return pile.pileId == pileId;
    });
    if (found == piles.end()) {
        return ServiceResult::failure(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));
    }
    // A pile participating in a reservation or an active charge cannot have
    // its hardware metadata changed while the client is using it.
    if (found->status == PileStatus::Reserved || found->status == PileStatus::Charging) {
        return ServiceResult::failure(ErrorCode::IllegalOrderState,
                                      QStringLiteral("ILLEGAL_ORDER_STATE"));
    }

    const QString normalizedCode = pileCode.trimmed().toCaseFolded();
    const bool duplicate = std::any_of(piles.cbegin(), piles.cend(),
                                       [pileId, &normalizedCode](const PileDto &pile) {
        return pile.pileId != pileId
            && pile.pileCode.toCaseFolded() == normalizedCode;
    });
    if (duplicate) {
        return ServiceResult::failure(ErrorCode::InvalidRequest,
                                      QStringLiteral("PILE_CODE_EXISTS"));
    }

    found->pileCode = pileCode.trimmed();
    found->pileType = pileType == QStringLiteral("FAST") ? PileType::Fast : PileType::Slow;
    found->ratedPowerKw = powerValue.toDouble();
    if (!repository_->updatePile(*found)) {
        if (!repository_->lastOperationSucceeded()) return internalError();
        return ServiceResult::failure(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));
    }
    return ServiceResult::success({{QStringLiteral("pile"), toJson(*found)}});
}

ServiceResult ApplicationService::deleteAdminPile(qint64 pileId)
{
    if (repository_ == nullptr || pileId <= 0) return invalidRequest();
    switch (repository_->deletePile(pileId)) {
    case DeletePileResult::Deleted:
        return ServiceResult::success({{QStringLiteral("success"), true}});
    case DeletePileResult::NotFound:
        return ServiceResult::failure(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));
    case DeletePileResult::Busy:
        return ServiceResult::failure(ErrorCode::IllegalOrderState, QStringLiteral("ILLEGAL_ORDER_STATE"));
    case DeletePileResult::HasOrders:
        return ServiceResult::failure(ErrorCode::IllegalOrderState, QStringLiteral("ILLEGAL_ORDER_STATE"));
    case DeletePileResult::StorageError:
        return internalError();
    }
    return internalError();
}

ServiceResult ApplicationService::setAdminPileStatus(qint64 pileId, PileStatus status)
{
    if (repository_ == nullptr || pileId <= 0
        || (status != PileStatus::Idle && status != PileStatus::Offline
            && status != PileStatus::Fault)) {
        return invalidRequest();
    }
    QList<PileDto> piles = repository_->listPiles();
    if (!repository_->lastOperationSucceeded()) return internalError();
    const auto found = std::find_if(piles.begin(), piles.end(), [pileId](const PileDto &pile) {
        return pile.pileId == pileId;
    });
    if (found == piles.end()) return ServiceResult::failure(ErrorCode::NotFound, QStringLiteral("NOT_FOUND"));
    if (found->status == PileStatus::Reserved || found->status == PileStatus::Charging
        || (found->status == PileStatus::Fault && status != PileStatus::Fault)) {
        return ServiceResult::failure(ErrorCode::IllegalOrderState, QStringLiteral("ILLEGAL_ORDER_STATE"));
    }
    found->status = status;
    if (!repository_->updatePile(*found)) return internalError();
    return ServiceResult::success({{QStringLiteral("pile"), toJson(*found)}});
}

ServiceResult ApplicationService::restartAdminPile(qint64 pileId)
{
    if (repository_ == nullptr || pileGateway_ == nullptr || pileId <= 0) {
        return invalidRequest();
    }
    QList<PileDto> piles = repository_->listPiles();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    const auto found = std::find_if(piles.begin(), piles.end(),
                                    [pileId](const PileDto &pile) {
                                        return pile.pileId == pileId;
                                    });
    if (found == piles.end()) {
        return ServiceResult::failure(ErrorCode::NotFound,
                                      QStringLiteral("NOT_FOUND"));
    }
    QString error;
    if (found->status == PileStatus::Fault
        || !pileGateway_->restart(found->pileId, found->status, &error)) {
        return ServiceResult::failure(ErrorCode::IllegalOrderState,
                                      QStringLiteral("ILLEGAL_ORDER_STATE"));
    }
    found->status = PileStatus::Idle;
    if (!repository_->updatePile(*found)) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    return ServiceResult::success({{QStringLiteral("pile"), toJson(*found)}});
}

ServiceResult ApplicationService::listAdminUsers(const QString &phoneKeyword) const
{
    if (repository_ == nullptr) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    const QList<UserDto> users = repository_->listUsers();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    QJsonArray items;
    for (const UserDto &user : users) {
        if (phoneKeyword.isEmpty() || user.phone.contains(phoneKeyword)
            || user.nickname.contains(phoneKeyword, Qt::CaseInsensitive)) {
            items.append(toJson(user));
        }
    }
    return ServiceResult::success({{QStringLiteral("items"), items}});
}

ServiceResult ApplicationService::setAdminUserStatus(qint64 userId,
                                                     UserStatus status)
{
    if (repository_ == nullptr || userId <= 0) {
        return invalidRequest();
    }
    std::optional<UserDto> user = repository_->findUserById(userId);
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    if (!user.has_value()) {
        return ServiceResult::failure(ErrorCode::NotFound,
                                      QStringLiteral("NOT_FOUND"));
    }
    if (status == UserStatus::Frozen) {
        const QList<OrderDto> orders = repository_->listOrders();
        if (!repository_->lastOperationSucceeded()) {
            return internalError();
        }
        const bool hasCurrentOrder = std::any_of(
            orders.cbegin(), orders.cend(),
            [userId](const OrderDto &order) {
                return order.userId == userId
                    && (order.status == OrderStatus::Reserved
                        || order.status == OrderStatus::Charging
                        || order.status == OrderStatus::PendingPayment);
            });
        if (hasCurrentOrder) {
            return ServiceResult::failure(ErrorCode::CurrentOrderExists,
                                          QStringLiteral("CURRENT_ORDER_EXISTS"));
        }
    }
    user->status = status;
    if (!repository_->updateUser(*user)) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    return ServiceResult::success({{QStringLiteral("user"), toJson(*user)}});
}

ServiceResult ApplicationService::listAdminOrders() const
{
    if (repository_ == nullptr) {
        return ServiceResult::failure(ErrorCode::InternalError,
                                      QStringLiteral("INTERNAL_ERROR"));
    }
    const QList<OrderDto> orders = repository_->listOrders();
    if (!repository_->lastOperationSucceeded()) {
        return internalError();
    }
    QJsonArray items;
    for (const OrderDto &order : orders) {
        items.append(toJson(order));
    }
    return ServiceResult::success({{QStringLiteral("items"), items}});
}

}  // namespace charging::server
