#ifndef FUSIONDESK_POLICY_POLICY_TYPES_H
#define FUSIONDESK_POLICY_POLICY_TYPES_H

#include <string>
#include <vector>

#include "fusiondesk/core/protocol/feature_flags.h"
#include "fusiondesk/core/session/session_context.h"

namespace fusiondesk {
namespace policy {

struct Principal
{
    std::string userId;
    std::string tenantId;
    std::vector<std::string> roles;
};

struct DeviceContext
{
    std::string deviceId;
    std::string platform;
    bool trusted = false;
};

enum class DenyReason
{
    None,
    FeatureNotLicensed,
    FeatureDisabledByPolicy,
    UnsupportedPlatform,
    MissingDependency,
    ModuleVersionMismatch,
    TransportNotAllowed,
    RuntimeHealthBlocked
};

struct PolicyDecision
{
    bool allowed = false;
    DenyReason reason = DenyReason::FeatureDisabledByPolicy;
    std::string message;
    protocol::FeatureSet effectiveFeatures;
};

struct PolicyContext
{
    Principal principal;
    DeviceContext localDevice;
    DeviceContext remoteDevice;
    session::SessionContext session;
};

} // namespace policy
} // namespace fusiondesk

#endif // FUSIONDESK_POLICY_POLICY_TYPES_H
