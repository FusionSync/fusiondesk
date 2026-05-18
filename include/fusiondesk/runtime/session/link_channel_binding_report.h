#ifndef FUSIONDESK_RUNTIME_SESSION_LINK_CHANNEL_BINDING_REPORT_H
#define FUSIONDESK_RUNTIME_SESSION_LINK_CHANNEL_BINDING_REPORT_H

#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/session/session.h"

namespace fusiondesk {
namespace runtime {

struct LinkChannelBindingReportOptions
{
    std::vector<network::ChannelKey> listeningChannels;
};

struct LinkChannelBindingItem
{
    network::ChannelKey key;
    std::string name;
    std::string ownerModuleId;
    bool profileMinimum = false;
    bool moduleRequired = false;
    std::vector<std::string> requiredByModules;

    bool registered = false;
    bool listening = false;
    bool bound = false;
    bool ready = false;
    bool degraded = false;
    bool closed = false;
    bool failed = false;
    bool blocked = false;

    network::ChannelLifecycleState lifecycle =
        network::ChannelLifecycleState::Registered;
    network::ChannelPressure pressure;
    network::ChannelReadyInfo readyInfo;
    std::string message;
};

struct LinkChannelBindingReport
{
    bool ok = false;
    protocol::SessionId sessionId = 0;
    std::vector<LinkChannelBindingItem> channels;
    std::vector<std::string> messages;
};

LinkChannelBindingReport buildLinkChannelBindingReport(
    session::Session& session,
    const LinkChannelBindingReportOptions& options = {});

} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_SESSION_LINK_CHANNEL_BINDING_REPORT_H
