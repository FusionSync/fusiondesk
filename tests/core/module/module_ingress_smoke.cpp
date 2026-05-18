#include <cassert>
#include <string>

#include "fusiondesk/core/module/module_catalog.h"
#include "fusiondesk/core/module/module_ingress.h"
#include "fusiondesk/core/network/network_router.h"

using namespace fusiondesk;

namespace {

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

network::ChannelKey smallDataKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
                               protocol::ChannelType::Standard};
}

protocol::PacketEnvelope packet(network::ChannelKey key, protocol::PacketType packetType)
{
    protocol::PacketEnvelope envelope;
    envelope.channelId = key.channelId;
    envelope.channelType = key.channelType;
    envelope.packetType = packetType;
    envelope.messageKind = packetType == protocol::PacketType::PayloadAck
                               ? protocol::MessageKind::Request
                               : protocol::MessageKind::Event;
    return envelope;
}

void clientIngressRoutesConsumedVideoAndSmallDataPayloadAck()
{
    network::NetworkRouter router;
    module::ModuleIngressRegistry ingress(&router);
    int calls = 0;
    std::string moduleId;

    const module::RegisteredIngress registered =
        ingress.registerManifest(module::catalog::displayScreenClient(),
                                 [&calls, &moduleId](const std::string& id, const protocol::PacketEnvelope&) {
                                     ++calls;
                                     moduleId = id;
                                 });

    assert(!registered.tokens.empty());
    router.submitIncoming(packet(screenKey(), protocol::PacketType::Video));
    router.submitIncoming(packet(smallDataKey(), protocol::PacketType::PayloadAck));
    assert(calls == 2);
    assert(moduleId == "display.screen.client");
}

void agentIngressRoutesConsumedSmallDataPayloadAckOnly()
{
    network::NetworkRouter router;
    module::ModuleIngressRegistry ingress(&router);
    int calls = 0;
    std::string moduleId;

    const module::RegisteredIngress registered =
        ingress.registerManifest(module::catalog::displayScreenAgent(),
                                 [&calls, &moduleId](const std::string& id, const protocol::PacketEnvelope&) {
                                     ++calls;
                                     moduleId = id;
                                 });

    assert(!registered.tokens.empty());
    router.submitIncoming(packet(screenKey(), protocol::PacketType::Video));
    router.submitIncoming(packet(smallDataKey(), protocol::PacketType::PayloadAck));
    assert(calls == 1);
    assert(moduleId == "display.screen.agent");
}

void unregisterStopsDispatch()
{
    network::NetworkRouter router;
    module::ModuleIngressRegistry ingress(&router);
    int calls = 0;

    ingress.registerManifest(module::catalog::displayScreenClient(),
                             [&calls](const std::string&, const protocol::PacketEnvelope&) {
                                 ++calls;
                             });

    router.submitIncoming(packet(screenKey(), protocol::PacketType::Video));
    ingress.unregisterModule("display.screen.client");
    router.submitIncoming(packet(screenKey(), protocol::PacketType::Video));
    assert(calls == 1);
}

void reregisterReplacesOldSubscriptions()
{
    network::NetworkRouter router;
    module::ModuleIngressRegistry ingress(&router);
    int oldCalls = 0;
    int newCalls = 0;

    ingress.registerManifest(module::catalog::displayScreenClient(),
                             [&oldCalls](const std::string&, const protocol::PacketEnvelope&) {
                                 ++oldCalls;
                             });
    ingress.registerManifest(module::catalog::displayScreenClient(),
                             [&newCalls](const std::string&, const protocol::PacketEnvelope&) {
                                 ++newCalls;
                             });

    router.submitIncoming(packet(screenKey(), protocol::PacketType::Video));
    assert(oldCalls == 0);
    assert(newCalls == 1);
}

} // namespace

int main()
{
    clientIngressRoutesConsumedVideoAndSmallDataPayloadAck();
    agentIngressRoutesConsumedSmallDataPayloadAckOnly();
    unregisterStopsDispatch();
    reregisterReplacesOldSubscriptions();
    return 0;
}
