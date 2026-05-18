#include "fusiondesk/runtime/connection/peer_connection_plan.h"

#include <set>
#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(PeerConnectionPlanResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

const network::ChannelSpec* findSpec(const std::vector<network::ChannelSpec>& specs,
                                     network::ChannelKey key)
{
    for (const network::ChannelSpec& spec : specs) {
        if (spec.key == key)
            return &spec;
    }
    return nullptr;
}

} // namespace

PeerConnectionPlanResult resolvePeerConnectionPlan(
    const PeerConnectionPlanRequest& request)
{
    PeerConnectionPlanResult result;
    if (request.knownSpecs.empty())
        appendFailure(result, "peer connection plan requires known channel specs");
    if (request.channels.empty())
        appendFailure(result, "peer connection plan requires channel requests");
    if (!result.messages.empty())
        return result;

    std::set<network::ChannelKey> requestedKeys;
    std::set<std::string> endpoints;
    std::vector<PeerConnectionPlanChannel> resolved;

    for (const PeerConnectionChannelRequest& channel : request.channels) {
        const network::ChannelSpec* spec = nullptr;
        bool valid = true;

        if (channel.key.channelId == 0) {
            appendFailure(result, "peer connection plan requested channel has empty channel id");
            valid = false;
        }

        const auto insertedKey = requestedKeys.insert(channel.key);
        if (!insertedKey.second) {
            appendFailure(result, "peer connection plan has duplicate requested channels");
            valid = false;
        }

        if (channel.endpoint.empty()) {
            appendFailure(result, "peer connection plan channel requires endpoint");
            valid = false;
        } else {
            const auto insertedEndpoint = endpoints.insert(channel.endpoint);
            if (!insertedEndpoint.second) {
                appendFailure(result, "peer connection plan requires distinct endpoints per channel");
                valid = false;
            }
        }

        spec = findSpec(request.knownSpecs, channel.key);
        if (spec == nullptr) {
            appendFailure(result, "peer connection plan requested channel is not in known specs");
            valid = false;
        }

        if (!valid)
            continue;

        PeerConnectionPlanChannel resolvedChannel;
        resolvedChannel.spec = *spec;
        resolvedChannel.endpoint = channel.endpoint;
        resolvedChannel.clientReadyEndpoint = channel.clientReadyEndpoint;
        resolvedChannel.agentReadyEndpoint = channel.agentReadyEndpoint;
        resolved.push_back(std::move(resolvedChannel));
    }

    if (!result.messages.empty())
        return result;

    result.channels = std::move(resolved);
    result.ok = !result.channels.empty();
    return result;
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
