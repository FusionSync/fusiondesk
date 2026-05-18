#ifndef FUSIONDESK_NETWORK_CHANNEL_DEFAULTS_H
#define FUSIONDESK_NETWORK_CHANNEL_DEFAULTS_H

#include <vector>

#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/network/priority_scheduler.h"

namespace fusiondesk {
namespace network {

std::vector<ChannelSpec> defaultMvpChannelSpecs();
ChannelSpec defaultLargeDataChannelSpec();
SendOptions defaultSendOptions(const protocol::PacketEnvelope& packet);

} // namespace network
} // namespace fusiondesk

#endif // FUSIONDESK_NETWORK_CHANNEL_DEFAULTS_H
