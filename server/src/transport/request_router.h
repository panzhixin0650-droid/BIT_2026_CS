#pragma once

#include "application/application_service.h"

#include "charging/protocol/envelope.h"

namespace charging::server {

// Converts protocol envelopes into typed application calls. It intentionally
// knows no database details and is the only place that maps a message type to
// an ApplicationService operation.
class RequestRouter final {
public:
    explicit RequestRouter(ApplicationService *service);

    [[nodiscard]] charging::protocol::ResponseEnvelope route(
        const charging::protocol::RequestEnvelope &request) const;

private:
    ApplicationService *service_ = nullptr;
};

}  // namespace charging::server
