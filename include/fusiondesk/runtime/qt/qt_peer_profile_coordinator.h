#ifndef FUSIONDESK_RUNTIME_QT_QT_PEER_PROFILE_COORDINATOR_H
#define FUSIONDESK_RUNTIME_QT_QT_PEER_PROFILE_COORDINATOR_H

#include <string>
#include <vector>

#include "fusiondesk/runtime/qt/qt_transport_profile.h"

namespace fusiondesk {
namespace runtime {
namespace qt {

struct QtTcpPeerProfilePlanChannel
{
    network::ChannelKey key;
    std::string endpoint;
    std::string clientReadyEndpoint;
    std::string agentReadyEndpoint;
};

struct QtTcpPeerProfilePlanOptions
{
    std::vector<network::ChannelSpec> knownSpecs;
    std::vector<QtTcpPeerProfilePlanChannel> channels;
    std::vector<network::ChannelKey> channelKeys;
    std::string endpoint;
    std::string clientReadyEndpoint;
    std::string agentReadyEndpoint;
    std::string clientProfilePath;
    std::string agentProfilePath;
    protocol::SessionId clientSessionId = 0;
    protocol::SessionId agentSessionId = 0;
};

struct QtTcpPeerProfilePlanResult
{
    bool ok = false;
    QtTcpPeerProfilePair pair;
    std::string clientProfilePath;
    std::string agentProfilePath;
    std::vector<std::string> messages;
};

class QtTcpPeerProfileCoordinator
{
public:
    QtTcpPeerProfilePlanResult saveLocalTcpPeerProfiles(
        const QtTcpPeerProfilePlanOptions& options) const;
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_PEER_PROFILE_COORDINATOR_H
