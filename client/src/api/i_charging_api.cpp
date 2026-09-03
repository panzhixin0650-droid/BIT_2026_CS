#include "api/i_charging_api.h"

namespace charging::client {

IChargingApi::IChargingApi(QObject *parent)
    : QObject(parent)
{
}

IChargingApi::~IChargingApi() = default;

}  // namespace charging::client
