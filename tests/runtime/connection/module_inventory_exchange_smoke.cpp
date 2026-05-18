#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/module/module_catalog.h"
#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/policy/policy_engine.h"
#include "fusiondesk/core/protocol/protocol_validator.h"
#include "fusiondesk/runtime/connection/module_inventory_exchange.h"
#include "fusiondesk/runtime/connection/module_inventory_runtime_service.h"

using namespace fusiondesk;

namespace {

class BridgeChannel : public network::IChannel
{
public:
    BridgeChannel(network::ChannelKey key, network::NetworkRouter* peer)
        : key_(key)
        , peer_(peer)
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
        return peer_ != nullptr;
    }

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        if (peer_ == nullptr)
            return {network::SendStatus::ChannelClosed, "peer router missing"};

        peer_->submitIncoming(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;

private:
    network::ChannelKey key_;
    network::NetworkRouter* peer_ = nullptr;
};

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

class DroppingChannel : public network::IChannel
{
public:
    explicit DroppingChannel(network::ChannelKey key)
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

    network::SendResult send(const protocol::PacketEnvelope& packet) override
    {
        sentPackets.push_back(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;

private:
    network::ChannelKey key_;
};

class FakeModule : public module::IModule
{
public:
    explicit FakeModule(module::ModuleManifest manifest)
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

    bool attach(const module::ModuleRuntime&) override
    {
        state_ = module::ModuleState::Attached;
        return true;
    }

    bool start(const module::ModuleStartOptions& options) override
    {
        peerVersions = options.peerVersions;
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

    std::string diagnostics() const override
    {
        return manifest_.moduleId;
    }

    std::vector<module::ModulePeerVersion> peerVersions;

private:
    module::ModuleManifest manifest_;
    module::ModuleState state_ = module::ModuleState::Created;
};

network::ChannelKey controlKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video};
}

network::ChannelKey smallDataKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
}

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Error,
                                 protocol::MessageKind::Event};
    return capabilities;
}

module::ModuleRuntime makeRuntime(network::ChannelRegistry* registry,
                                  network::INetworkRouter* router)
{
    module::ModuleRuntime runtime;
    runtime.session.sessionId = 1001;
    runtime.session.traceId = 777;
    runtime.session.role = session::SessionRole::Client;
    runtime.session.localPlatform = "windows";
    runtime.session.allowedFeatures.bits = protocol::feature::Screen;
    runtime.channels = registry;
    runtime.network = router;
    return runtime;
}

void prepareDisplayChannel(network::ChannelRegistry& registry,
                           network::NetworkRouter& router)
{
    const std::vector<network::ChannelSpec> specs = network::defaultMvpChannelSpecs();
    assert(registry.registerSpec(specs[1]).ok);
    assert(registry.registerSpec(specs[2]).ok);
    auto smallDataChannel = std::make_shared<FakeChannel>(smallDataKey());
    auto screenChannel = std::make_shared<FakeChannel>(screenKey());
    assert(router.registerChannel(smallDataChannel));
    assert(router.registerChannel(screenChannel));
    assert(registry.bind(smallDataKey(), smallDataChannel).ok);
    assert(registry.bind(screenKey(), screenChannel).ok);
    assert(registry.markReady(smallDataKey(), {}).ok);
    assert(registry.markReady(screenKey(), {}).ok);
}

void codecRoundtripsModuleInventory()
{
    const module::ModuleManifest client = module::catalog::displayScreenClient();
    const runtime::connection::ModuleInventory inventory =
        runtime::connection::makeModuleInventory(1001, {client});

    protocol::PacketEnvelope request =
        runtime::connection::makeModuleInventoryRequestPacket(inventory);
    assert(protocol::ProtocolValidator().validate(request).valid);

    const runtime::connection::ModuleInventoryDecodeResult decoded =
        runtime::connection::decodeModuleInventoryRequestPacket(request);
    assert(decoded.ok);
    assert(decoded.inventory.sessionId == 1001);
    assert(decoded.inventory.manifests.size() == 1);
    assert(decoded.inventory.manifests.front().moduleId == "display.screen.client");
    assert(decoded.inventory.manifests.front().version.major == 1);
    assert(decoded.inventory.manifests.front().compatiblePeers.size() == 1);
    assert(decoded.inventory.manifests.front().channels.size() == 3);
}

void codecRejectsMalformedInventoryEnvelope()
{
    const runtime::connection::ModuleInventory inventory =
        runtime::connection::makeModuleInventory(
            1001,
            {module::catalog::displayScreenClient()});

    protocol::PacketEnvelope request =
        runtime::connection::makeModuleInventoryRequestPacket(inventory);

    protocol::PacketEnvelope missingResponseFlag = request;
    missingResponseFlag.flags = protocol::PacketFlagNone;
    assert(!runtime::connection::decodeModuleInventoryRequestPacket(missingResponseFlag).ok);

    protocol::PacketEnvelope missingMessageId = request;
    missingMessageId.messageId = 0;
    assert(!runtime::connection::decodeModuleInventoryRequestPacket(missingMessageId).ok);

    protocol::PacketEnvelope missingCorrelation = request;
    missingCorrelation.correlationId = 0;
    assert(!runtime::connection::decodeModuleInventoryRequestPacket(missingCorrelation).ok);

    protocol::PacketEnvelope missingTimeout = request;
    missingTimeout.timeoutMs = 0;
    assert(!runtime::connection::decodeModuleInventoryRequestPacket(missingTimeout).ok);

    protocol::PacketEnvelope wrongChannel = request;
    wrongChannel.channelType = protocol::ChannelType::Standard;
    assert(!runtime::connection::decodeModuleInventoryRequestPacket(wrongChannel).ok);
}

void codecRejectsNonOkInventoryResponse()
{
    const runtime::connection::ModuleInventory inventory =
        runtime::connection::makeModuleInventory(
            2202,
            {module::catalog::displayScreenAgent()});

    protocol::PacketEnvelope request =
        runtime::connection::makeModuleInventoryRequestPacket(inventory);
    runtime::connection::ModuleInventoryWireResponseOptions responseOptions;
    responseOptions.messageId = 900;
    responseOptions.status = protocol::ResponseStatus::Unsupported;

    const protocol::PacketEnvelope response =
        runtime::connection::makeModuleInventoryResponsePacket(
            request,
            inventory,
            responseOptions);
    assert(protocol::ProtocolValidator().validate(response).valid);
    assert(!runtime::connection::decodeModuleInventoryResponsePacket(response).ok);
}

void serviceExchangesInventoryOverControlChannel()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    const network::ChannelKey key = controlKey();
    auto clientChannel = std::make_shared<BridgeChannel>(key, &agentRouter);
    auto agentChannel = std::make_shared<BridgeChannel>(key, &clientRouter);
    assert(clientRouter.registerChannel(clientChannel));
    assert(agentRouter.registerChannel(agentChannel));

    const runtime::connection::ModuleInventory agentInventory =
        runtime::connection::makeModuleInventory(
            2202,
            {module::catalog::displayScreenAgent()});

    runtime::connection::ModuleInventoryService service(agentRouter);
    runtime::connection::ModuleInventoryServiceStartOptions startOptions;
    startOptions.localInventory = agentInventory;
    startOptions.firstResponseMessageId = 900;
    assert(service.start(startOptions).ok);

    protocol::PacketEnvelope capturedResponse;
    int responseCount = 0;
    network::RouteMatch responseRoute;
    responseRoute.channelId = key.channelId;
    responseRoute.channelType = key.channelType;
    responseRoute.packetType = protocol::PacketType::Exchange;
    responseRoute.messageKind = protocol::MessageKind::Response;
    clientRouter.subscribe(responseRoute,
                           [&capturedResponse, &responseCount](const protocol::PacketEnvelope& packet) {
                               capturedResponse = packet;
                               ++responseCount;
                           });

    const runtime::connection::ModuleInventory clientInventory =
        runtime::connection::makeModuleInventory(
            1101,
            {module::catalog::displayScreenClient()});
    runtime::connection::ModuleInventoryWireOptions wire;
    wire.messageId = 42;
    wire.sessionId = 1101;
    wire.traceId = 77;
    const protocol::PacketEnvelope request =
        runtime::connection::makeModuleInventoryRequestPacket(clientInventory, wire);
    assert(protocol::ProtocolValidator().validate(request).valid);
    assert(clientRouter.send(request).status == network::SendStatus::Sent);

    assert(responseCount == 1);
    assert(protocol::ProtocolValidator().validate(capturedResponse).valid);
    const runtime::connection::ModuleInventoryDecodeResult decoded =
        runtime::connection::decodeModuleInventoryResponsePacket(capturedResponse);
    assert(decoded.ok);
    assert(decoded.inventory.sessionId == 2202);
    assert(decoded.inventory.manifests.size() == 1);
    assert(decoded.inventory.manifests.front().moduleId == "display.screen.agent");

    const runtime::connection::ModuleInventoryServiceSnapshot snapshot = service.snapshot();
    assert(snapshot.handledRequests == 1);
    assert(snapshot.sentResponses == 1);
    assert(snapshot.hasLastRemoteInventory);
    assert(snapshot.lastRemoteSessionId == 1101);
    assert(snapshot.lastRemoteModuleCount == 1);
    assert(service.lastRemoteInventory().manifests.front().moduleId == "display.screen.client");
}

void serviceRecordsEmptyRemoteInventoryAsReceived()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    const network::ChannelKey key = controlKey();
    auto clientChannel = std::make_shared<BridgeChannel>(key, &agentRouter);
    auto agentChannel = std::make_shared<BridgeChannel>(key, &clientRouter);
    assert(clientRouter.registerChannel(clientChannel));
    assert(agentRouter.registerChannel(agentChannel));

    runtime::connection::ModuleInventoryService service(agentRouter);
    runtime::connection::ModuleInventoryServiceStartOptions startOptions;
    startOptions.localInventory =
        runtime::connection::makeModuleInventory(
            2202,
            {module::catalog::displayScreenAgent()});
    assert(service.start(startOptions).ok);

    runtime::connection::ModuleInventoryWireOptions wire;
    wire.messageId = 4200;
    wire.sessionId = 1101;
    const protocol::PacketEnvelope request =
        runtime::connection::makeModuleInventoryRequestPacket(
            runtime::connection::makeModuleInventory(1101, {}),
            wire);
    assert(clientRouter.send(request).status == network::SendStatus::Sent);

    const runtime::connection::ModuleInventoryServiceSnapshot snapshot = service.snapshot();
    assert(snapshot.handledRequests == 1);
    assert(snapshot.hasLastRemoteInventory);
    assert(snapshot.lastRemoteSessionId == 1101);
    assert(snapshot.lastRemoteModuleCount == 0);
    assert(service.lastRemoteInventory().manifests.empty());
    assert(agentChannel->sentPackets.size() == 1);
}

void serviceRejectsMalformedInventoryRequestEnvelope()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    const network::ChannelKey key = controlKey();
    auto clientChannel = std::make_shared<BridgeChannel>(key, &agentRouter);
    auto agentChannel = std::make_shared<BridgeChannel>(key, &clientRouter);
    assert(clientRouter.registerChannel(clientChannel));
    assert(agentRouter.registerChannel(agentChannel));

    runtime::connection::ModuleInventoryService service(agentRouter);
    runtime::connection::ModuleInventoryServiceStartOptions startOptions;
    startOptions.localInventory =
        runtime::connection::makeModuleInventory(
            2202,
            {module::catalog::displayScreenAgent()});
    startOptions.firstResponseMessageId = 900;
    assert(service.start(startOptions).ok);

    protocol::PacketEnvelope capturedError;
    int errorCount = 0;
    network::RouteMatch errorRoute;
    errorRoute.channelId = key.channelId;
    errorRoute.channelType = key.channelType;
    errorRoute.packetType = protocol::PacketType::Exchange;
    errorRoute.messageKind = protocol::MessageKind::Error;
    clientRouter.subscribe(errorRoute,
                           [&capturedError, &errorCount](const protocol::PacketEnvelope& packet) {
                               capturedError = packet;
                               ++errorCount;
                           });

    const runtime::connection::ModuleInventory clientInventory =
        runtime::connection::makeModuleInventory(
            1101,
            {module::catalog::displayScreenClient()});
    runtime::connection::ModuleInventoryWireOptions wire;
    wire.messageId = 43;
    wire.sessionId = 1101;
    wire.traceId = 77;
    protocol::PacketEnvelope request =
        runtime::connection::makeModuleInventoryRequestPacket(clientInventory, wire);
    request.flags = protocol::PacketFlagNone;
    assert(!runtime::connection::decodeModuleInventoryRequestPacket(request).ok);
    assert(clientRouter.send(request).status == network::SendStatus::Sent);

    assert(errorCount == 1);
    assert(capturedError.messageKind == protocol::MessageKind::Error);
    assert(capturedError.responseStatus == protocol::ResponseStatus::ProtocolError);
    assert(capturedError.responseTo == request.messageId);
    assert(protocol::ProtocolValidator().validate(capturedError).valid);

    const runtime::connection::ModuleInventoryServiceSnapshot snapshot = service.snapshot();
    assert(snapshot.handledRequests == 0);
    assert(snapshot.failedRequests == 1);
    assert(snapshot.sentResponses == 1);
    assert(snapshot.lastRemoteModuleCount == 0);
}

void runtimeServiceDispatchesTracksAndCompletesFdmiExchange()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    const network::ChannelKey key = controlKey();
    auto clientChannel = std::make_shared<BridgeChannel>(key, &agentRouter);
    auto agentChannel = std::make_shared<BridgeChannel>(key, &clientRouter);
    assert(clientRouter.registerChannel(clientChannel));
    assert(agentRouter.registerChannel(agentChannel));

    runtime::connection::ModuleInventoryRuntimeService clientService(clientRouter, 3000);
    runtime::connection::ModuleInventoryRuntimeService agentService(agentRouter, 5000);

    runtime::connection::ModuleInventoryRuntimeServiceStartOptions clientStart;
    clientStart.startResponder = false;
    assert(clientService.start(clientStart).ok);

    runtime::connection::ModuleInventoryRuntimeServiceStartOptions agentStart;
    agentStart.subscribeResponses = false;
    agentStart.responder.firstResponseMessageId = 7000;
    agentStart.responder.localInventory =
        runtime::connection::makeModuleInventory(
            2202,
            {module::catalog::displayScreenAgent()});
    assert(agentService.start(agentStart).ok);

    runtime::connection::ModuleInventoryRuntimeExchangeOptions options;
    options.wire.messageId = 0;
    options.wire.timeoutMs = 1500;
    options.wire.sessionId = 1101;
    options.wire.traceId = 88;
    options.wire.monotonicTimestampUsec = 10000;
    const runtime::connection::ModuleInventoryRuntimeDispatchResult dispatched =
        clientService.requestModuleInventory(
            runtime::connection::makeModuleInventory(
                1101,
                {module::catalog::displayScreenClient()}),
            options);
    assert(dispatched.ok);
    assert(dispatched.request.messageId == 3000);
    assert(dispatched.request.correlationId == 3000);
    assert(dispatched.request.timeoutMs == 1500);
    assert(clientChannel->sentPackets.size() == 1);
    assert(agentChannel->sentPackets.size() == 1);

    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot clientSnapshot =
        clientService.snapshot();
    assert(clientSnapshot.active);
    assert(clientSnapshot.pendingRequests == 0);
    assert(clientSnapshot.completedResponses == 1);
    assert(clientSnapshot.completions.front().ok);
    assert(clientSnapshot.completions.front().response.messageId == 7000);
    assert(clientSnapshot.completions.front().response.responseTo == 3000);
    assert(clientSnapshot.completions.front().inventory.sessionId == 2202);
    assert(clientSnapshot.completions.front().inventory.manifests.size() == 1);
    assert(clientSnapshot.completions.front().inventory.manifests.front().moduleId ==
           "display.screen.agent");
    const std::vector<module::ModulePeerVersion> peerVersions =
        runtime::connection::peerVersionsFromModuleInventory(
            clientSnapshot.completions.front().inventory);
    assert(peerVersions.size() == 1);
    assert(peerVersions.front().moduleId == "display.screen.agent");

    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot agentSnapshot =
        agentService.snapshot();
    assert(agentSnapshot.responder.active);
    assert(agentSnapshot.responder.handledRequests == 1);
    assert(agentSnapshot.responder.sentResponses == 1);
    assert(agentService.lastRemoteInventoryFromResponder().manifests.size() == 1);
    assert(agentService.lastRemoteInventoryFromResponder().manifests.front().moduleId ==
           "display.screen.client");
}

void runtimeServiceCompletesEmptyFdmiResponse()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    const network::ChannelKey key = controlKey();
    auto clientChannel = std::make_shared<BridgeChannel>(key, &agentRouter);
    auto agentChannel = std::make_shared<BridgeChannel>(key, &clientRouter);
    assert(clientRouter.registerChannel(clientChannel));
    assert(agentRouter.registerChannel(agentChannel));

    runtime::connection::ModuleInventoryRuntimeService clientService(clientRouter, 3100);
    runtime::connection::ModuleInventoryRuntimeService agentService(agentRouter, 5100);

    runtime::connection::ModuleInventoryRuntimeServiceStartOptions clientStart;
    clientStart.startResponder = false;
    assert(clientService.start(clientStart).ok);

    runtime::connection::ModuleInventoryRuntimeServiceStartOptions agentStart;
    agentStart.subscribeResponses = false;
    agentStart.responder.localInventory =
        runtime::connection::makeModuleInventory(2202, {});
    assert(agentService.start(agentStart).ok);

    runtime::connection::ModuleInventoryRuntimeExchangeOptions options;
    options.wire.messageId = 0;
    options.wire.timeoutMs = 1500;
    options.wire.sessionId = 1101;
    options.wire.monotonicTimestampUsec = 10000;
    const runtime::connection::ModuleInventoryRuntimeDispatchResult dispatched =
        clientService.requestModuleInventory(
            runtime::connection::makeModuleInventory(
                1101,
                {module::catalog::displayScreenClient()}),
            options);
    assert(dispatched.ok);

    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot snapshot =
        clientService.snapshot();
    assert(snapshot.pendingRequests == 0);
    assert(snapshot.completedResponses == 1);
    assert(snapshot.completions.front().ok);
    assert(snapshot.completions.front().inventory.sessionId == 2202);
    assert(snapshot.completions.front().inventory.manifests.empty());
}

void runtimeServiceExpiresUnansweredFdmiRequest()
{
    network::NetworkRouter clientRouter;
    auto clientControl = std::make_shared<DroppingChannel>(controlKey());
    assert(clientRouter.registerChannel(clientControl));

    runtime::connection::ModuleInventoryRuntimeService clientService(clientRouter, 8000);
    runtime::connection::ModuleInventoryRuntimeServiceStartOptions start;
    start.startResponder = false;
    assert(clientService.start(start).ok);

    runtime::connection::ModuleInventoryRuntimeExchangeOptions options;
    options.wire.timeoutMs = 10;
    options.wire.sessionId = 1101;
    options.wire.monotonicTimestampUsec = 20000;
    const runtime::connection::ModuleInventoryRuntimeDispatchResult dispatched =
        clientService.requestModuleInventory(
            runtime::connection::makeModuleInventory(
                1101,
                {module::catalog::displayScreenClient()}),
            options);
    assert(dispatched.ok);
    assert(clientService.snapshot().pendingRequests == 1);

    assert(clientService.expire(30000) == 1);
    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot snapshot =
        clientService.snapshot();
    assert(snapshot.pendingRequests == 0);
    assert(snapshot.expiredRequests == 1);
    assert(snapshot.completedResponses == 1);
    assert(!snapshot.completions.front().ok);
    assert(snapshot.completions.front().response.responseStatus ==
           protocol::ResponseStatus::Timeout);
}

void runtimeServiceCompletesTerminalFdmiError()
{
    network::NetworkRouter clientRouter;
    auto clientControl = std::make_shared<DroppingChannel>(controlKey());
    assert(clientRouter.registerChannel(clientControl));

    runtime::connection::ModuleInventoryRuntimeService clientService(clientRouter, 8100);
    runtime::connection::ModuleInventoryRuntimeServiceStartOptions start;
    start.startResponder = false;
    assert(clientService.start(start).ok);

    runtime::connection::ModuleInventoryRuntimeExchangeOptions options;
    options.wire.timeoutMs = 500;
    options.wire.sessionId = 1101;
    options.wire.monotonicTimestampUsec = 40000;
    const runtime::connection::ModuleInventoryRuntimeDispatchResult dispatched =
        clientService.requestModuleInventory(
            runtime::connection::makeModuleInventory(
                1101,
                {module::catalog::displayScreenClient()}),
            options);
    assert(dispatched.ok);
    assert(clientService.snapshot().pendingRequests == 1);

    protocol::PacketEnvelope error = dispatched.request;
    error.messageId = 9100;
    error.messageKind = protocol::MessageKind::Error;
    error.responseStatus = protocol::ResponseStatus::ProtocolError;
    error.responseTo = dispatched.request.messageId;
    error.timeoutMs = 0;
    error.flags = protocol::PacketFlagNone;
    error.payload = {'b', 'a', 'd'};
    clientRouter.submitIncoming(error);

    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot snapshot =
        clientService.snapshot();
    assert(snapshot.pendingRequests == 0);
    assert(snapshot.completedResponses == 1);
    assert(snapshot.completions.front().terminal);
    assert(!snapshot.completions.front().ok);
    assert(snapshot.completions.front().response.messageKind ==
           protocol::MessageKind::Error);
    assert(snapshot.completions.front().response.responseStatus ==
           protocol::ResponseStatus::ProtocolError);
    assert(snapshot.completions.front().messages.size() == 1);
    assert(snapshot.completions.front().messages.front() == "bad");
}

void runtimeServiceCompletesNonOkFdmiResponse()
{
    network::NetworkRouter clientRouter;
    auto clientControl = std::make_shared<DroppingChannel>(controlKey());
    assert(clientRouter.registerChannel(clientControl));

    runtime::connection::ModuleInventoryRuntimeService clientService(clientRouter, 8200);
    runtime::connection::ModuleInventoryRuntimeServiceStartOptions start;
    start.startResponder = false;
    assert(clientService.start(start).ok);

    runtime::connection::ModuleInventoryRuntimeExchangeOptions options;
    options.wire.timeoutMs = 500;
    options.wire.sessionId = 1101;
    options.wire.monotonicTimestampUsec = 50000;
    const runtime::connection::ModuleInventoryRuntimeDispatchResult dispatched =
        clientService.requestModuleInventory(
            runtime::connection::makeModuleInventory(
                1101,
                {module::catalog::displayScreenClient()}),
            options);
    assert(dispatched.ok);

    runtime::connection::ModuleInventoryWireResponseOptions responseOptions;
    responseOptions.messageId = 9200;
    responseOptions.status = protocol::ResponseStatus::DeniedByPolicy;
    const protocol::PacketEnvelope response =
        runtime::connection::makeModuleInventoryResponsePacket(
            dispatched.request,
            runtime::connection::makeModuleInventory(
                2202,
                {module::catalog::displayScreenAgent()}),
            responseOptions);
    clientRouter.submitIncoming(response);

    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot snapshot =
        clientService.snapshot();
    assert(snapshot.pendingRequests == 0);
    assert(snapshot.completedResponses == 1);
    assert(snapshot.completions.front().terminal);
    assert(!snapshot.completions.front().ok);
    assert(snapshot.completions.front().response.messageKind ==
           protocol::MessageKind::Response);
    assert(snapshot.completions.front().response.responseStatus ==
           protocol::ResponseStatus::DeniedByPolicy);
    assert(snapshot.completions.front().messages.size() == 1);
    assert(snapshot.completions.front().messages.front() ==
           "module inventory response status is not Ok");
}

void remoteInventoryVersionsFeedModuleHostStartOptions()
{
    network::NetworkRouter router;
    network::ChannelRegistry registry(makeNegotiated());
    prepareDisplayChannel(registry, router);

    protocol::FeatureSet features;
    features.bits = protocol::feature::Screen;
    policy::StaticPolicyEngine policy(features);
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    auto display = std::make_shared<FakeModule>(module::catalog::displayScreenClient());
    assert(host.addModule(display));

    const runtime::connection::ModuleInventory peerInventory =
        runtime::connection::makeModuleInventory(
            2202,
            {module::catalog::displayScreenAgent()});
    module::ModuleStartOptions options;
    options.peerVersions =
        runtime::connection::peerVersionsFromModuleInventory(peerInventory);

    const std::vector<module::ModuleStartReport> reports = host.startAllowedModules(options);
    assert(reports.size() == 1);
    assert(reports.front().started);
    assert(display->peerVersions.size() == 1);
    assert(display->peerVersions.front().moduleId == "display.screen.agent");
    assert(display->peerVersions.front().compatible);
    assert(display->peerVersions.front().compatibilityMode == "v1-family");
}

void incompatibleRemoteInventoryBlocksModuleStart()
{
    network::NetworkRouter router;
    network::ChannelRegistry registry(makeNegotiated());
    prepareDisplayChannel(registry, router);

    protocol::FeatureSet features;
    features.bits = protocol::feature::Screen;
    policy::StaticPolicyEngine policy(features);
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    auto display = std::make_shared<FakeModule>(module::catalog::displayScreenClient());
    assert(host.addModule(display));

    module::ModuleManifest incompatibleAgent = module::catalog::displayScreenAgent();
    incompatibleAgent.version = module::ModuleVersion{2, 0, 0};
    const runtime::connection::ModuleInventory peerInventory =
        runtime::connection::makeModuleInventory(2202, {incompatibleAgent});

    module::ModuleStartOptions options;
    options.peerVersions =
        runtime::connection::peerVersionsFromModuleInventory(peerInventory);

    const std::vector<module::ModuleStartReport> reports = host.startAllowedModules(options);
    assert(reports.size() == 1);
    assert(!reports.front().started);
    assert(reports.front().decision.reason == policy::DenyReason::ModuleVersionMismatch);
    assert(display->state() == module::ModuleState::Attached);
}

} // namespace

int main()
{
    codecRoundtripsModuleInventory();
    codecRejectsMalformedInventoryEnvelope();
    codecRejectsNonOkInventoryResponse();
    serviceExchangesInventoryOverControlChannel();
    serviceRecordsEmptyRemoteInventoryAsReceived();
    serviceRejectsMalformedInventoryRequestEnvelope();
    runtimeServiceDispatchesTracksAndCompletesFdmiExchange();
    runtimeServiceCompletesEmptyFdmiResponse();
    runtimeServiceExpiresUnansweredFdmiRequest();
    runtimeServiceCompletesTerminalFdmiError();
    runtimeServiceCompletesNonOkFdmiResponse();
    remoteInventoryVersionsFeedModuleHostStartOptions();
    incompatibleRemoteInventoryBlocksModuleStart();
    return 0;
}
