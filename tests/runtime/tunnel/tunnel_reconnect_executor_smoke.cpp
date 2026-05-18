#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/connection/reconnect_coordinator.h"
#include "fusiondesk/runtime/tunnel/tunnel_candidate_profile.h"
#include "fusiondesk/runtime/tunnel/tunnel_reconnect_executor.h"

using namespace fusiondesk;

namespace {

network::ChannelKey screenKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video};
}

runtime::connection::ReconnectOrchestrationRequest reconnectRequest()
{
    runtime::connection::ReconnectOrchestrationRequest request;
    request.profile.connectionPlan.knownSpecs = network::defaultMvpChannelSpecs();
    request.profile.connectionPlan.channels = {
        runtime::connection::PeerConnectionChannelRequest{
            screenKey(),
            "direct-peer:main-screen",
            "client-direct-ready",
            "agent-direct-ready"},
    };
    request.profile.clientSessionId = 9101;
    request.profile.agentSessionId = 9102;
    request.degradedChannels = {screenKey()};
    request.reason = "tunnel reconnect smoke";
    request.requestDisplayKeyframe = true;
    return request;
}

network::ChannelSpec screenSpec()
{
    const std::vector<network::ChannelSpec> specs =
        network::defaultMvpChannelSpecs();
    for (const network::ChannelSpec& spec : specs) {
        if (spec.key == screenKey())
            return spec;
    }
    assert(false);
    return {};
}

runtime::tunnel::TunnelReplacementCandidate candidate(
    runtime::tunnel::TunnelTransportMode mode,
    bool listener,
    const std::string& endpoint,
    const std::string& readyEndpoint)
{
    runtime::tunnel::TunnelReplacementCandidate result;
    result.spec = screenSpec();
    result.mode = mode;
    result.listener = listener;
    result.endpoint = endpoint;
    result.readyEndpoint = readyEndpoint;
    result.encrypted = true;
    return result;
}

class FakeChannel : public network::IChannel
{
public:
    explicit FakeChannel(network::ChannelKey key)
        : key_(key)
    {
    }

    protocol::ChannelId id() const override
    {
        return key_.channelId;
    }

    protocol::ChannelType type() const override
    {
        return key_.channelType;
    }

    bool isOpen() const override
    {
        return true;
    }

    network::SendResult send(const protocol::PacketEnvelope&) override
    {
        return network::SendResult::sent();
    }

private:
    network::ChannelKey key_;
};

class FakeTunnelTransportFactory : public runtime::tunnel::ITunnelTransportFactory
{
public:
    runtime::tunnel::TunnelTransportFactoryResult prepareClientChannel(
        const runtime::tunnel::TunnelTransportFactoryRequest& request) override
    {
        clientRequests.push_back(request);
        return prepared(request);
    }

    runtime::tunnel::TunnelTransportFactoryResult prepareAgentListener(
        const runtime::tunnel::TunnelTransportFactoryRequest& request) override
    {
        agentRequests.push_back(request);
        return prepared(request);
    }

    std::vector<runtime::tunnel::TunnelTransportFactoryRequest> clientRequests;
    std::vector<runtime::tunnel::TunnelTransportFactoryRequest> agentRequests;

private:
    static runtime::tunnel::TunnelTransportFactoryResult prepared(
        const runtime::tunnel::TunnelTransportFactoryRequest& request)
    {
        runtime::tunnel::TunnelTransportFactoryResult result;
        result.ok = true;
        result.preparedChannels.push_back(request.candidate.spec.key);
        result.channels.push_back(
            std::make_unique<FakeChannel>(request.candidate.spec.key));
        return result;
    }
};

class FakeTunnelBackend : public runtime::tunnel::ITunnelReplacementBackend
{
public:
    runtime::connection::ReconnectReplacementExecutionResult startAgentReplacements(
        const runtime::tunnel::TunnelReplacementRequest& request) override
    {
        agentRequests.push_back(request);
        runtime::connection::ReconnectReplacementExecutionResult result;
        result.ok = true;
        result.channels = request.degradedChannels;
        result.messages.push_back("agent tunnel replacement started");
        return result;
    }

    runtime::connection::ReconnectReplacementExecutionResult reconnectClientReplacements(
        const runtime::tunnel::TunnelReplacementRequest& request) override
    {
        clientRequests.push_back(request);
        runtime::connection::ReconnectReplacementExecutionResult result;
        result.ok = true;
        result.channels = request.degradedChannels;
        result.hasSessionReport = true;
        result.sessionReport.attempted = true;
        result.sessionReport.ok = true;
        result.sessionReport.reason = request.reason;
        result.sessionReport.requestedFreshState = request.requestDisplayKeyframe;
        return result;
    }

    std::vector<runtime::tunnel::TunnelReplacementRequest> agentRequests;
    std::vector<runtime::tunnel::TunnelReplacementRequest> clientRequests;
};

void coordinatorCanUseTunnelReconnectExecutor()
{
    FakeTunnelBackend backend;
    runtime::tunnel::TunnelReconnectExecutorOptions tunnelOptions;
    tunnelOptions.preferredMode = runtime::tunnel::TunnelTransportMode::DirectP2P;
    tunnelOptions.allowLanTcpFallback = false;
    runtime::tunnel::TunnelReconnectExecutor executor(backend, tunnelOptions);
    runtime::connection::ReconnectCoordinator coordinator(executor);

    runtime::connection::ReconnectCoordinatorOptions options;
    options.dispatchClientTeardown = false;
    const runtime::connection::ReconnectCoordinatorRunResult run =
        coordinator.run(reconnectRequest(), options);

    assert(run.ok);
    assert(run.hasSessionReport);
    assert(run.sessionReport.reason == "tunnel reconnect smoke");
    assert(backend.agentRequests.size() == 1);
    assert(backend.clientRequests.size() == 1);

    const runtime::tunnel::TunnelReplacementRequest& agent =
        backend.agentRequests.front();
    assert(agent.side == runtime::tunnel::TunnelReplacementSide::Agent);
    assert(agent.sessionId == 9102);
    assert(agent.candidates.size() == 1);
    assert(agent.candidates.front().listener);
    assert(agent.candidates.front().mode ==
           runtime::tunnel::TunnelTransportMode::DirectP2P);
    assert(agent.candidates.front().endpoint == "direct-peer:main-screen");
    assert(agent.options.requireEncryptedTransport);

    const runtime::tunnel::TunnelReplacementRequest& client =
        backend.clientRequests.front();
    assert(client.side == runtime::tunnel::TunnelReplacementSide::Client);
    assert(client.sessionId == 9101);
    assert(client.candidates.size() == 1);
    assert(!client.candidates.front().listener);
    assert(client.candidates.front().readyEndpoint == "client-direct-ready");
    assert(client.teardownAfterSuccessfulRebind.size() == 1);
    assert(std::string(runtime::tunnel::tunnelTransportModeName(
               runtime::tunnel::TunnelTransportMode::Relay)) == "relay");
    assert(std::string(runtime::tunnel::tunnelReplacementSideName(
               runtime::tunnel::TunnelReplacementSide::Agent)) == "agent");
}

void buildRejectsMissingTunnelCandidates()
{
    runtime::connection::ReconnectOrchestrationSidePlan sidePlan;
    sidePlan.sessionId = 77;
    const runtime::tunnel::TunnelReplacementRequestBuildResult built =
        runtime::tunnel::buildTunnelReplacementRequest(
            sidePlan,
            runtime::tunnel::TunnelReplacementSide::Client);
    assert(!built.ok);
    assert(!built.messages.empty());
}

void negotiatesTunnelCandidatesAndBuildsFactoryRequests()
{
    runtime::tunnel::TunnelCandidateProfile client;
    client.sessionId = 9101;
    client.candidates = {
        candidate(runtime::tunnel::TunnelTransportMode::Relay,
                  false,
                  "relay://client/main-screen",
                  "client-relay-ready"),
        candidate(runtime::tunnel::TunnelTransportMode::DirectP2P,
                  false,
                  "p2p://client/main-screen",
                  "client-p2p-ready"),
    };

    runtime::tunnel::TunnelCandidateProfile agent;
    agent.sessionId = 9102;
    agent.candidates = {
        candidate(runtime::tunnel::TunnelTransportMode::Relay,
                  true,
                  "relay://agent/main-screen",
                  "agent-relay-ready"),
        candidate(runtime::tunnel::TunnelTransportMode::DirectP2P,
                  true,
                  "p2p://agent/main-screen",
                  "agent-p2p-ready"),
    };

    runtime::tunnel::TunnelCandidateNegotiationRequest request;
    request.client = client;
    request.agent = agent;
    request.requestedChannels = {screenKey()};
    request.options.preferredMode = runtime::tunnel::TunnelTransportMode::DirectP2P;

    const runtime::tunnel::TunnelCandidateNegotiationResult negotiated =
        runtime::tunnel::negotiateTunnelCandidates(request);
    assert(negotiated.ok);
    assert(negotiated.selections.size() == 1);
    assert(negotiated.selections.front().mode ==
           runtime::tunnel::TunnelTransportMode::DirectP2P);
    assert(!negotiated.selections.front().client.listener);
    assert(negotiated.selections.front().agent.listener);
    assert(negotiated.selections.front().client.endpoint ==
           "p2p://client/main-screen");

    const runtime::tunnel::TunnelCandidateSelection& selection =
        negotiated.selections.front();

    runtime::tunnel::TunnelReplacementRequest clientReplacement;
    clientReplacement.side = runtime::tunnel::TunnelReplacementSide::Client;
    clientReplacement.sessionId = client.sessionId;
    clientReplacement.reason = "factory smoke";
    clientReplacement.requestDisplayKeyframe = true;
    clientReplacement.candidates.push_back(selection.client);

    FakeTunnelTransportFactory factory;
    const runtime::tunnel::TunnelTransportFactoryRequest factoryRequest =
        runtime::tunnel::makeTunnelTransportFactoryRequest(
            clientReplacement,
            selection);
    const runtime::tunnel::TunnelTransportFactoryResult prepared =
        factory.prepareClientChannel(factoryRequest);
    assert(prepared.ok);
    assert(prepared.preparedChannels.size() == 1);
    assert(prepared.channels.size() == 1);
    assert(prepared.channels.front()->id() == screenKey().channelId);
    assert(factoryRequest.hasPeerCandidate);
    assert(factoryRequest.candidate.endpoint == "p2p://client/main-screen");
    assert(factoryRequest.peerCandidate.endpoint == "p2p://agent/main-screen");
    assert(factory.clientRequests.size() == 1);
    assert(factory.clientRequests.front().reason == "factory smoke");

    runtime::tunnel::TunnelReplacementRequest agentReplacement;
    agentReplacement.side = runtime::tunnel::TunnelReplacementSide::Agent;
    agentReplacement.sessionId = agent.sessionId;
    agentReplacement.reason = "agent factory smoke";
    agentReplacement.requestDisplayKeyframe = true;
    agentReplacement.candidates.push_back(selection.agent);

    const runtime::tunnel::TunnelTransportFactoryRequest agentFactoryRequest =
        runtime::tunnel::makeTunnelTransportFactoryRequest(
            agentReplacement,
            selection);
    const runtime::tunnel::TunnelTransportFactoryResult agentPrepared =
        factory.prepareAgentListener(agentFactoryRequest);
    assert(agentPrepared.ok);
    assert(agentFactoryRequest.hasPeerCandidate);
    assert(agentFactoryRequest.candidate.endpoint == "p2p://agent/main-screen");
    assert(agentFactoryRequest.peerCandidate.endpoint ==
           "p2p://client/main-screen");
    assert(factory.agentRequests.size() == 1);
    assert(factory.agentRequests.front().reason == "agent factory smoke");
}

} // namespace

int main()
{
    coordinatorCanUseTunnelReconnectExecutor();
    buildRejectsMissingTunnelCandidates();
    negotiatesTunnelCandidatesAndBuildsFactoryRequests();
    return 0;
}
