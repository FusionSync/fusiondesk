#include <cassert>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/module/module_catalog.h"
#include "fusiondesk/core/module/module_compatibility.h"
#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/policy/policy_engine.h"
#include "fusiondesk/core/protocol/protocol_validator.h"
#include "fusiondesk/modules/input/input_factory.h"

using namespace fusiondesk;

namespace {

constexpr std::uint16_t kControlChannelId =
    static_cast<std::uint16_t>(protocol::ChannelIdValue::UserAuthMain);

class FakeChannel : public network::IChannel
{
public:
    FakeChannel(protocol::ChannelId id, protocol::ChannelType type)
        : id_(id)
        , type_(type)
    {
    }

    protocol::ChannelId id() const override
    {
        return id_;
    }

    protocol::ChannelType type() const override
    {
        return type_;
    }

    bool isOpen() const override
    {
        return true;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;

private:
    protocol::ChannelId id_ = 0;
    protocol::ChannelType type_ = protocol::ChannelType::Standard;
};

std::vector<std::uint8_t> bytes(const std::string& value)
{
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

bool hasPayload(const protocol::PacketEnvelope& packet, const std::string& expected)
{
    return packet.payload == bytes(expected);
}

bool payloadStartsWith(const protocol::PacketEnvelope& packet, const std::string& prefix)
{
    const std::vector<std::uint8_t> expected = bytes(prefix);
    return packet.payload.size() >= expected.size() &&
           std::equal(expected.begin(), expected.end(), packet.payload.begin());
}

module::ModuleManifest testProtocolManifest(session::SessionRole role)
{
    const bool client = role == session::SessionRole::Client;

    module::ModuleManifest manifest;
    manifest.moduleId = client ? "test.protocol.client" : "test.protocol.agent";
    manifest.displayName = client ? "Test Protocol Client" : "Test Protocol Agent";
    manifest.sku = "fusiondesk.module.test.protocol";
    manifest.version = client ? module::ModuleVersion{1, 2, 0} : module::ModuleVersion{1, 1, 0};
    manifest.feature = 0;
    manifest.roleFlags = client ? module::ModuleRoleClient : module::ModuleRoleAgent;
    manifest.runModeFlags = module::ModuleRunModeInProcess;
    manifest.disableAtRuntime = true;
    manifest.supportedPlatforms = {"windows", "linux", "macos", "android"};

    module::ModulePeerCompatibility peer;
    peer.peerModuleId = client ? "test.protocol.agent" : "test.protocol.client";
    peer.minPeerVersion = module::ModuleVersion{1, 0, 0};
    peer.maxPeerVersion = module::ModuleVersion{1, 9, 99};
    peer.compatibilityMode = "v1-family";
    manifest.compatiblePeers = {peer};

    module::ChannelBinding control;
    control.name = "control";
    control.channelId = kControlChannelId;
    control.channelType = protocol::ChannelType::Control;
    control.required = true;
    control.shared = true;
    control.consumes = {protocol::PacketType::Exchange};
    control.produces = {protocol::PacketType::Exchange};
    manifest.channels = {control};

    return manifest;
}

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control};
    capabilities.packetTypes = {protocol::PacketType::Exchange};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Error};
    return capabilities;
}

network::ChannelSpec controlSpec()
{
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().front();
    spec.key.channelId = kControlChannelId;
    spec.key.channelType = protocol::ChannelType::Control;
    spec.name = "control";
    spec.allowlist = {protocol::PacketType::Exchange};
    spec.ownerModuleId = "test.protocol";
    spec.required = true;
    return spec;
}

module::ModuleRuntime makeRuntime(session::SessionRole role,
                                  network::ChannelRegistry* registry,
                                  network::INetworkRouter* router)
{
    module::ModuleRuntime runtime;
    runtime.session.sessionId = role == session::SessionRole::Client ? 110 : 220;
    runtime.session.traceId = role == session::SessionRole::Client ? 1001 : 2002;
    runtime.session.role = role;
    runtime.session.localPlatform = "windows";
    runtime.session.allowedFeatures.bits = 0;
    runtime.channels = registry;
    runtime.network = router;
    return runtime;
}

class TestProtocolModule : public module::IModule
{
public:
    explicit TestProtocolModule(module::ModuleManifest manifest)
        : manifest_(std::move(manifest))
    {
    }

    const module::ModuleManifest& manifest() const override
    {
        return manifest_;
    }

    module::ModuleState state() const override
    {
        return state_;
    }

    bool attach(const module::ModuleRuntime& runtime) override
    {
        runtime_ = runtime;
        state_ = module::ModuleState::Attached;
        return true;
    }

    bool start(const module::ModuleStartOptions& options) override
    {
        for (const module::ModulePeerVersion& peer : options.peerVersions) {
            if (peer.compatible) {
                peerCompatibilityMode_ = peer.compatibilityMode;
                break;
            }
        }
        state_ = module::ModuleState::Running;
        return true;
    }

    void stop(const module::ModuleStopOptions&) override
    {
        state_ = module::ModuleState::Stopped;
    }

    void detach() override
    {
        state_ = module::ModuleState::Detached;
    }

    void handlePacket(const protocol::PacketEnvelope& packet) override
    {
        if (state_ != module::ModuleState::Running)
            return;

        if (packet.messageKind == protocol::MessageKind::Request) {
            if (hasPayload(packet, "FDTP/1 PING")) {
                makeResponse(packet, protocol::MessageKind::Response, protocol::ResponseStatus::Ok, "FDTP/1 PONG");
                ++requestsHandled_;
                return;
            }

            const protocol::ResponseStatus status =
                payloadStartsWith(packet, "FDTP/1") ? protocol::ResponseStatus::Unsupported :
                                                       protocol::ResponseStatus::ProtocolError;
            makeResponse(packet, protocol::MessageKind::Error, status, "FDTP/1 ERROR");
            ++requestsRejected_;
            return;
        }

        if (packet.messageKind == protocol::MessageKind::Response && hasPayload(packet, "FDTP/1 PONG"))
            ++responsesHandled_;
    }

    std::string diagnostics() const override
    {
        return "test-protocol";
    }

    protocol::PacketEnvelope makePing(protocol::MessageId messageId) const
    {
        return makeRequest(messageId, "FDTP/1 PING");
    }

    protocol::PacketEnvelope makeRequest(protocol::MessageId messageId,
                                         const std::string& payload) const
    {
        protocol::PacketEnvelope packet;
        packet.sessionId = runtime_.session.sessionId;
        packet.traceId = runtime_.session.traceId;
        packet.messageId = messageId;
        packet.correlationId = messageId;
        packet.channelId = kControlChannelId;
        packet.channelType = protocol::ChannelType::Control;
        packet.packetType = protocol::PacketType::Exchange;
        packet.messageKind = protocol::MessageKind::Request;
        packet.priority = protocol::PacketPriority::Critical;
        packet.timeoutMs = 1000;
        packet.flags = protocol::PacketFlagResponseRequired;
        packet.payload = bytes(payload);
        return packet;
    }

    const protocol::PacketEnvelope& lastResponse() const
    {
        return lastResponse_;
    }

    int requestsHandled() const
    {
        return requestsHandled_;
    }

    int responsesHandled() const
    {
        return responsesHandled_;
    }

    int requestsRejected() const
    {
        return requestsRejected_;
    }

    const std::string& peerCompatibilityMode() const
    {
        return peerCompatibilityMode_;
    }

private:
    void makeResponse(const protocol::PacketEnvelope& request,
                      protocol::MessageKind kind,
                      protocol::ResponseStatus status,
                      const std::string& payload)
    {
        lastResponse_ = request;
        lastResponse_.messageId = request.messageId + 1000;
        lastResponse_.messageKind = kind;
        lastResponse_.responseStatus = status;
        lastResponse_.responseTo = request.messageId;
        lastResponse_.flags = protocol::PacketFlagNone;
        lastResponse_.payload = bytes(payload);
    }

    module::ModuleManifest manifest_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    protocol::PacketEnvelope lastResponse_;
    int requestsHandled_ = 0;
    int responsesHandled_ = 0;
    int requestsRejected_ = 0;
    std::string peerCompatibilityMode_;
};

void implementedRoleSpecificModulesDeclarePeerCompatibility()
{
    const module::ModuleManifest displayClient = module::catalog::displayScreenClient();
    assert(displayClient.version.major == 1);
    assert(displayClient.compatiblePeers.size() == 1);
    assert(displayClient.compatiblePeers.front().peerModuleId == "display.screen.agent");

    const module::ModuleManifest mouseClient =
        modules::input::inputClientManifest(modules::input::InputModuleKind::Mouse);
    assert(mouseClient.compatiblePeers.size() == 1);
    assert(mouseClient.compatiblePeers.front().peerModuleId == "input.mouse.agent");

    const module::ModuleManifest keyboardAgent =
        modules::input::inputAgentManifest(modules::input::InputModuleKind::Keyboard);
    assert(keyboardAgent.compatiblePeers.size() == 1);
    assert(keyboardAgent.compatiblePeers.front().peerModuleId == "input.keyboard.client");

}

void compatibilityUsesModuleDeclarationsOnly()
{
    const module::ModuleManifest client = testProtocolManifest(session::SessionRole::Client);
    const module::ModuleManifest agent = testProtocolManifest(session::SessionRole::Agent);

    const module::ModuleCompatibilityCheck compatible =
        module::checkModuleCompatibility(client, agent.moduleId, agent.version);
    assert(compatible.compatible);
    assert(compatible.compatibilityMode == "v1-family");

    const module::ModuleCompatibilityCheck rejectedVersion =
        module::checkModuleCompatibility(client, agent.moduleId, module::ModuleVersion{2, 0, 0});
    assert(!rejectedVersion.compatible);

    const module::ModuleCompatibilityCheck rejectedPeer =
        module::checkModuleCompatibility(client, "display.screen.agent", module::ModuleVersion{1, 0, 0});
    assert(!rejectedPeer.compatible);
}

void moduleHostCanStartVersionedProtocolModule()
{
    network::NetworkRouter router;
    network::ChannelRegistry registry(makeNegotiated());
    const network::ChannelSpec spec = controlSpec();
    auto channel = std::make_shared<FakeChannel>(spec.key.channelId, spec.key.channelType);

    assert(registry.registerSpec(spec).ok);
    assert(router.registerChannel(channel));
    assert(registry.bind(spec.key, channel).ok);
    assert(registry.markReady(spec.key, {}).ok);

    protocol::FeatureSet features;
    features.bits = 0;
    policy::StaticPolicyEngine policy(features);
    module::ModuleHost host(makeRuntime(session::SessionRole::Client, &registry, &router), &policy);

    auto testModule = std::make_shared<TestProtocolModule>(
        testProtocolManifest(session::SessionRole::Client));
    assert(host.addModule(testModule));

    module::ModuleStartOptions options;
    options.peerVersions.push_back(module::ModulePeerVersion{
        "test.protocol.agent",
        module::ModuleVersion{1, 1, 0},
        true,
        "v1-family"});

    const std::vector<module::ModuleStartReport> reports = host.startAllowedModules(options);
    assert(reports.size() == 1);
    assert(reports.front().started);
    assert(testModule->state() == module::ModuleState::Running);
    assert(testModule->peerCompatibilityMode() == "v1-family");

    protocol::ProtocolValidator validator;
    const protocol::PacketEnvelope request = testModule->makePing(77);
    assert(validator.validate(request).valid);
    router.submitIncoming(request);
    assert(testModule->requestsHandled() == 1);
    assert(validator.validate(testModule->lastResponse()).valid);
}

void moduleHostRejectsIncompatiblePeerVersionBeforeStart()
{
    network::NetworkRouter router;
    network::ChannelRegistry registry(makeNegotiated());
    const network::ChannelSpec spec = controlSpec();
    auto channel = std::make_shared<FakeChannel>(spec.key.channelId, spec.key.channelType);

    assert(registry.registerSpec(spec).ok);
    assert(router.registerChannel(channel));
    assert(registry.bind(spec.key, channel).ok);
    assert(registry.markReady(spec.key, {}).ok);

    protocol::FeatureSet features;
    features.bits = 0;
    policy::StaticPolicyEngine policy(features);
    module::ModuleHost host(makeRuntime(session::SessionRole::Client, &registry, &router), &policy);

    auto testModule = std::make_shared<TestProtocolModule>(
        testProtocolManifest(session::SessionRole::Client));
    assert(host.addModule(testModule));

    module::ModuleStartOptions options;
    options.peerVersions.push_back(module::ModulePeerVersion{
        "test.protocol.agent",
        module::ModuleVersion{2, 0, 0},
        true,
        "caller-supplied-mode"});

    const std::vector<module::ModuleStartReport> reports = host.startAllowedModules(options);
    assert(reports.size() == 1);
    assert(!reports.front().started);
    assert(reports.front().decision.reason == policy::DenyReason::ModuleVersionMismatch);
    assert(testModule->state() == module::ModuleState::Attached);
    assert(testModule->peerCompatibilityMode().empty());
}

void protocolCompatibilityAfterStartIsOwnedInsideModule()
{
    TestProtocolModule client(testProtocolManifest(session::SessionRole::Client));
    TestProtocolModule agent(testProtocolManifest(session::SessionRole::Agent));

    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    network::ChannelRegistry clientRegistry(makeNegotiated());
    network::ChannelRegistry agentRegistry(makeNegotiated());
    client.attach(makeRuntime(session::SessionRole::Client, &clientRegistry, &clientRouter));
    agent.attach(makeRuntime(session::SessionRole::Agent, &agentRegistry, &agentRouter));

    const module::ModuleCompatibilityCheck compatibility =
        module::checkModuleCompatibility(client.manifest(), agent.manifest().moduleId, agent.manifest().version);

    module::ModuleStartOptions clientOptions;
    clientOptions.peerVersions.push_back(module::ModulePeerVersion{
        agent.manifest().moduleId,
        agent.manifest().version,
        compatibility.compatible,
        compatibility.compatibilityMode});

    module::ModuleStartOptions agentOptions;
    agentOptions.peerVersions.push_back(module::ModulePeerVersion{
        client.manifest().moduleId,
        client.manifest().version,
        true,
        "v1-family"});

    assert(client.start(clientOptions));
    assert(agent.start(agentOptions));
    assert(client.peerCompatibilityMode() == "v1-family");

    const protocol::PacketEnvelope request = client.makePing(42);
    agent.handlePacket(request);
    assert(agent.requestsHandled() == 1);

    const protocol::PacketEnvelope response = agent.lastResponse();
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseStatus == protocol::ResponseStatus::Ok);
    assert(response.responseTo == request.messageId);
    assert(response.correlationId == request.correlationId);
    assert(response.timeoutMs == request.timeoutMs);
    assert(hasPayload(response, "FDTP/1 PONG"));

    protocol::ProtocolValidator validator;
    assert(validator.validate(request).valid);
    assert(validator.validate(response).valid);

    client.handlePacket(response);
    assert(client.responsesHandled() == 1);

    const protocol::PacketEnvelope unsupported = client.makeRequest(43, "FDTP/1 NEGOTIATE");
    agent.handlePacket(unsupported);
    assert(agent.requestsRejected() == 1);
    const protocol::PacketEnvelope unsupportedResponse = agent.lastResponse();
    assert(unsupportedResponse.messageKind == protocol::MessageKind::Error);
    assert(unsupportedResponse.responseStatus == protocol::ResponseStatus::Unsupported);
    assert(unsupportedResponse.responseTo == unsupported.messageId);
    assert(validator.validate(unsupportedResponse).valid);

    const protocol::PacketEnvelope malformed = client.makeRequest(44, "BAD");
    agent.handlePacket(malformed);
    assert(agent.requestsRejected() == 2);
    const protocol::PacketEnvelope malformedResponse = agent.lastResponse();
    assert(malformedResponse.messageKind == protocol::MessageKind::Error);
    assert(malformedResponse.responseStatus == protocol::ResponseStatus::ProtocolError);
    assert(malformedResponse.responseTo == malformed.messageId);
    assert(validator.validate(malformedResponse).valid);
}

} // namespace

int main()
{
    implementedRoleSpecificModulesDeclarePeerCompatibility();
    compatibilityUsesModuleDeclarationsOnly();
    moduleHostCanStartVersionedProtocolModule();
    moduleHostRejectsIncompatiblePeerVersionBeforeStart();
    protocolCompatibilityAfterStartIsOwnedInsideModule();
    return 0;
}
