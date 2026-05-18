#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module_compatibility.h"
#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/policy/policy_engine.h"
#include "fusiondesk/core/protocol/protocol_validator.h"
#include "fusiondesk/modules/test/test_echo_factory.h"
#include "fusiondesk/modules/test/test_echo_modules.h"
#include "fusiondesk/runtime/runtime_host.h"

using namespace fusiondesk;

namespace {

class FakeChannel : public network::IChannel
{
public:
    FakeChannel(protocol::ChannelId id, protocol::ChannelType type)
        : id_(id),
          type_(type)
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

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control};
    capabilities.packetTypes = {protocol::PacketType::Exchange,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::ChannelInit};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Error,
                                 protocol::MessageKind::Event};
    return capabilities;
}

network::ChannelSpec controlSpec()
{
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().front();
    spec.key.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain);
    spec.key.channelType = protocol::ChannelType::Control;
    spec.name = "control";
    spec.allowlist = {protocol::PacketType::Exchange,
                      protocol::PacketType::Heartbeat,
                      protocol::PacketType::ChannelInit};
    spec.ownerModuleId = "network.control";
    spec.required = true;
    return spec;
}

module::ModuleRuntime makeRuntime(session::SessionRole role,
                                  network::ChannelRegistry* registry,
                                  network::INetworkRouter* router)
{
    module::ModuleRuntime runtime;
    runtime.session.sessionId = role == session::SessionRole::Client ? 10 : 20;
    runtime.session.traceId = role == session::SessionRole::Client ? 100 : 200;
    runtime.session.role = role;
    runtime.session.localPlatform = "windows";
    runtime.session.allowedFeatures.bits = 0;
    runtime.session.policyFeatures.bits = 0;
    runtime.session.licensedFeatures.bits = 0;
    runtime.session.negotiatedCapabilities = makeNegotiated();
    runtime.network = router;
    runtime.channels = registry;
    return runtime;
}

void bindReadyControl(network::NetworkRouter& router,
                      network::ChannelRegistry& registry,
                      const std::shared_ptr<FakeChannel>& channel)
{
    const network::ChannelSpec spec = controlSpec();
    assert(registry.registerSpec(spec).ok);
    assert(router.registerChannel(channel));
    assert(registry.bind(spec.key, channel).ok);
    assert(registry.markReady(spec.key, {}).ok);
}

module::ModuleStartOptions compatiblePeerOptions(
    const module::ModuleManifest& local,
    const module::ModuleManifest& peer)
{
    const module::ModuleCompatibilityCheck compatibility =
        module::checkModuleCompatibility(local, peer.moduleId, peer.version);
    assert(compatibility.compatible);

    module::ModuleStartOptions options;
    options.peerVersions.push_back(module::ModulePeerVersion{
        peer.moduleId,
        peer.version,
        compatibility.compatible,
        compatibility.compatibilityMode});
    return options;
}

void factoryCreatesRoleSpecificEchoModules()
{
    modules::test::TestEchoModuleFactory factory;

    module::ModuleCreateOptions clientOptions;
    clientOptions.role = session::SessionRole::Client;
    clientOptions.localPlatform = "android";
    assert(factory.supports("test.echo", clientOptions));
    assert(factory.supports("test.echo.client", clientOptions));
    assert(!factory.supports("test.echo.agent", clientOptions));

    std::shared_ptr<module::IModule> client = factory.create(clientOptions);
    assert(client != nullptr);
    assert(client->manifest().moduleId == "test.echo.client");
    assert(client->manifest().compatiblePeers.size() == 1);
    assert(client->manifest().compatiblePeers.front().peerModuleId == "test.echo.agent");

    module::ModuleCreateOptions agentOptions;
    agentOptions.role = session::SessionRole::Agent;
    agentOptions.localPlatform = "linux";
    std::shared_ptr<module::IModule> agent = factory.create(agentOptions);
    assert(agent != nullptr);
    assert(agent->manifest().moduleId == "test.echo.agent");
    assert(agent->manifest().channels.size() == 1);
    assert(agent->manifest().channels.front().channelType == protocol::ChannelType::Control);
}

void echoRoundTripUsesModuleHostIngressAndUnifiedResponse()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    network::ChannelRegistry clientRegistry(makeNegotiated());
    network::ChannelRegistry agentRegistry(makeNegotiated());
    auto clientChannel = std::make_shared<FakeChannel>(
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control);
    auto agentChannel = std::make_shared<FakeChannel>(
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control);
    bindReadyControl(clientRouter, clientRegistry, clientChannel);
    bindReadyControl(agentRouter, agentRegistry, agentChannel);

    protocol::FeatureSet features;
    policy::StaticPolicyEngine clientPolicy(features);
    policy::StaticPolicyEngine agentPolicy(features);
    module::ModuleHost clientHost(
        makeRuntime(session::SessionRole::Client, &clientRegistry, &clientRouter),
        &clientPolicy);
    module::ModuleHost agentHost(
        makeRuntime(session::SessionRole::Agent, &agentRegistry, &agentRouter),
        &agentPolicy);

    auto client = std::make_shared<modules::test::TestEchoModule>(
        modules::test::testEchoClientManifest());
    auto agent = std::make_shared<modules::test::TestEchoModule>(
        modules::test::testEchoAgentManifest());
    assert(clientHost.addModule(client));
    assert(agentHost.addModule(agent));

    const std::vector<module::ModuleStartReport> clientReports =
        clientHost.startAllowedModules(compatiblePeerOptions(client->manifest(),
                                                             agent->manifest()));
    const std::vector<module::ModuleStartReport> agentReports =
        agentHost.startAllowedModules(compatiblePeerOptions(agent->manifest(),
                                                            client->manifest()));
    assert(clientReports.size() == 1 && clientReports.front().started);
    assert(agentReports.size() == 1 && agentReports.front().started);
    assert(client->snapshot().peerCompatibilityMode == "echo-v1");
    assert(agent->snapshot().peerCompatibilityMode == "echo-v1");

    const protocol::MessageId pingId = client->sendPing("hello");
    assert(pingId != 0);
    assert(clientChannel->sentPackets.size() == 1);

    protocol::ProtocolValidator validator;
    const protocol::PacketEnvelope request = clientChannel->sentPackets.back();
    assert(validator.validate(request).valid);
    assert(request.messageKind == protocol::MessageKind::Request);
    assert(request.flags == protocol::PacketFlagResponseRequired);

    agentRouter.submitIncoming(request);
    assert(agent->snapshot().requestsReceived == 1);
    assert(agentChannel->sentPackets.size() == 1);

    const protocol::PacketEnvelope response = agentChannel->sentPackets.back();
    assert(validator.validate(response).valid);
    assert(response.messageKind == protocol::MessageKind::Response);
    assert(response.responseStatus == protocol::ResponseStatus::Ok);
    assert(response.responseTo == pingId);
    assert(response.correlationId == request.correlationId);

    clientRouter.submitIncoming(response);
    assert(client->snapshot().responsesReceived == 1);
    assert(client->snapshot().lastResponseTo == pingId);
}

void malformedEchoRequestReturnsProtocolError()
{
    network::NetworkRouter agentRouter;
    network::ChannelRegistry agentRegistry(makeNegotiated());
    auto agentChannel = std::make_shared<FakeChannel>(
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control);
    bindReadyControl(agentRouter, agentRegistry, agentChannel);

    protocol::FeatureSet features;
    policy::StaticPolicyEngine policy(features);
    module::ModuleHost agentHost(
        makeRuntime(session::SessionRole::Agent, &agentRegistry, &agentRouter),
        &policy);
    auto agent = std::make_shared<modules::test::TestEchoModule>(
        modules::test::testEchoAgentManifest());
    assert(agentHost.addModule(agent));
    assert(agentHost.startAllowedModules().front().started);

    protocol::PacketEnvelope request;
    request.sessionId = 10;
    request.traceId = 100;
    request.messageId = 99;
    request.correlationId = 99;
    request.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain);
    request.channelType = protocol::ChannelType::Control;
    request.packetType = protocol::PacketType::Exchange;
    request.messageKind = protocol::MessageKind::Request;
    request.priority = protocol::PacketPriority::Interactive;
    request.timeoutMs = 1000;
    request.flags = protocol::PacketFlagResponseRequired;
    request.payload = {'F', 'D', 'E', 'T', '/', '1', ' ', 'B', 'A', 'D'};

    agentRouter.submitIncoming(request);
    assert(agent->snapshot().decodeFailures == 1);
    assert(agent->snapshot().errorsSent == 1);
    assert(agentChannel->sentPackets.size() == 1);
    assert(agentChannel->sentPackets.back().messageKind == protocol::MessageKind::Error);
    assert(agentChannel->sentPackets.back().responseStatus == protocol::ResponseStatus::ProtocolError);
    assert(agentChannel->sentPackets.back().responseTo == request.messageId);

    protocol::PacketEnvelope otherControlRequest = request;
    otherControlRequest.messageId = 100;
    otherControlRequest.correlationId = 100;
    otherControlRequest.payload = {'F', 'D', 'M', 'I', '/', '1', ' ', 'R', 'E', 'Q'};
    agentRouter.submitIncoming(otherControlRequest);
    assert(agent->snapshot().decodeFailures == 1);
    assert(agent->snapshot().errorsSent == 1);
    assert(agentChannel->sentPackets.size() == 1);
}

void runtimeHostCanMountEchoFromProductProfile()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "test-echo-profile";
    options.profile.defaultFeatures.bits = 0;
    options.profile.requiredModules = {"test.echo"};
    options.profile.minimumChannels = {controlSpec()};

    runtime::RuntimeHost host;
    assert(host.initialize(options));

    session::SessionCreateOptions sessionOptions;
    sessionOptions.context.userId = "user-a";
    sessionOptions.context.tenantId = "tenant-a";
    sessionOptions.context.localPlatform = "windows";
    sessionOptions.context.negotiatedCapabilities = makeNegotiated();
    sessionOptions.minimumChannels = options.profile.minimumChannels;

    const protocol::SessionId sessionId =
        host.sessions().createClientSession(sessionOptions);
    session::Session* session = host.sessions().find(sessionId);
    assert(session != nullptr);
    assert(session->start());

    const runtime::ProfileMountReport report =
        host.mountProfileModules(*session, runtime::DisplayMvpDependencies{});
    assert(report.ok());
    assert(report.mountedModules.size() == 1);
    assert(session->moduleHost()->module("test.echo.client") != nullptr);
}

} // namespace

int main()
{
    factoryCreatesRoleSpecificEchoModules();
    echoRoundTripUsesModuleHostIngressAndUnifiedResponse();
    malformedEchoRequestReturnsProtocolError();
    runtimeHostCanMountEchoFromProductProfile();
    return 0;
}
