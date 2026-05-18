#ifndef FUSIONDESK_SESSION_CONTEXT_H
#define FUSIONDESK_SESSION_CONTEXT_H

#include <cstdint>
#include <string>

#include "fusiondesk/core/protocol/capability_negotiation.h"
#include "fusiondesk/core/protocol/feature_flags.h"

namespace fusiondesk {
namespace session {

enum class SessionRole
{
    Client,
    Agent,
    Auth,
    Relay,
    Standalone
};

enum class TransportMode
{
    Unknown,
    Lan,
    Relay,
    DirectTunnel
};

enum class SecurityMode
{
    Unknown,
    None,
    Tls,
    Enterprise
};

struct SessionContext
{
    protocol::SessionId sessionId = 0;
    protocol::TraceId traceId = 0;
    std::string tenantId;
    std::string userId;
    std::string deviceId;
    std::string clientDeviceId;
    std::string agentDeviceId;
    std::string localPeerId;
    std::string remotePeerId;
    std::string localPlatform;
    std::string remotePlatform;
    std::string localCpuArch;
    std::string remoteCpuArch;
    SessionRole role = SessionRole::Standalone;
    TransportMode transportMode = TransportMode::Unknown;
    SecurityMode securityMode = SecurityMode::Unknown;
    protocol::FeatureSet requestedFeatures;
    protocol::FeatureSet licensedFeatures;
    protocol::FeatureSet policyFeatures;
    protocol::FeatureSet allowedFeatures;
    protocol::NegotiatedCapabilities negotiatedCapabilities;
    std::string policyVersion;
    std::uint16_t protocolMajor = protocol::CurrentProtocolMajor;
    std::uint16_t protocolMinor = protocol::CurrentProtocolMinor;
    std::uint64_t createdMonotonicTimestampUsec = 0;
    std::uint32_t reconnectCount = 0;
};

} // namespace session
} // namespace fusiondesk

#endif // FUSIONDESK_SESSION_CONTEXT_H
