#include <cassert>
#include <memory>
#include <vector>

#include "fusiondesk/core/diagnostics/diagnostics_sink.h"
#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/policy/policy_engine.h"
#include "fusiondesk/modules/input/input_factory.h"
#include "fusiondesk/modules/input/input_modules.h"

using namespace fusiondesk;

namespace {

class BridgeChannel : public network::IChannel
{
public:
    BridgeChannel(network::ChannelKey key, network::NetworkRouter* peer)
        : key_(key),
          peer_(peer)
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
            return {network::SendStatus::ChannelClosed, "peer router is missing"};

        peer_->submitIncoming(packet);
        return network::SendResult::sent();
    }

    std::vector<protocol::PacketEnvelope> sentPackets;

private:
    network::ChannelKey key_;
    network::NetworkRouter* peer_ = nullptr;
};

class FakeInputInjector : public modules::input::IInputInjector
{
public:
    bool injectMouse(const modules::input::MouseInputEvent& event) override
    {
        lastMouse = event;
        ++mouseEvents;
        return true;
    }

    bool injectKeyboard(const modules::input::KeyboardInputEvent& event) override
    {
        lastKeyboard = event;
        ++keyboardEvents;
        return true;
    }

    int mouseEvents = 0;
    int keyboardEvents = 0;
    modules::input::MouseInputEvent lastMouse;
    modules::input::KeyboardInputEvent lastKeyboard;
};

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Standard};
    capabilities.packetTypes = {protocol::PacketType::Mouse,
                                protocol::PacketType::Keyboard};
    capabilities.messageKinds = {protocol::MessageKind::Event,
                                 protocol::MessageKind::Error};
    return capabilities;
}

void prepareChannel(network::NetworkRouter& localRouter,
                    network::NetworkRouter& peerRouter,
                    network::ChannelRegistry& registry,
                    const network::ChannelSpec& spec,
                    std::shared_ptr<BridgeChannel>& channel)
{
    const network::ChannelRegistryResult specResult = registry.registerSpec(spec);
    assert(specResult.ok || specResult.status == network::ChannelRegistryStatus::AlreadyRegistered);

    channel = std::make_shared<BridgeChannel>(spec.key, &peerRouter);
    assert(localRouter.registerChannel(channel));
    assert(registry.bind(spec.key, channel).ok);
    assert(registry.markReady(spec.key, {}).ok);
}

module::ModuleRuntime makeRuntime(session::SessionRole role,
                                  network::ChannelRegistry* registry,
                                  network::INetworkRouter* router,
                                  diagnostics::DiagnosticsSink* diagnostics)
{
    module::ModuleRuntime runtime;
    runtime.session.sessionId = role == session::SessionRole::Client ? 10 : 20;
    runtime.session.traceId = role == session::SessionRole::Client ? 100 : 200;
    runtime.session.role = role;
    runtime.session.localPlatform = "windows";
    runtime.session.allowedFeatures.bits =
        protocol::feature::Mouse |
        protocol::feature::Keyboard;
    runtime.session.policyVersion = "feature-contract-smoke";
    runtime.channels = registry;
    runtime.network = router;
    runtime.diagnostics = diagnostics;
    return runtime;
}

policy::StaticPolicyEngine makePolicy()
{
    protocol::FeatureSet features;
    features.bits =
        protocol::feature::Mouse |
        protocol::feature::Keyboard;
    return policy::StaticPolicyEngine(features);
}

bool allStarted(const std::vector<module::ModuleStartReport>& reports)
{
    if (reports.empty())
        return false;

    for (const module::ModuleStartReport& report : reports) {
        if (!report.started)
            return false;
    }
    return true;
}

void inputContractsRouteThroughModuleHost()
{
    network::NetworkRouter clientRouter;
    network::NetworkRouter agentRouter;
    network::ChannelRegistry clientRegistry(makeNegotiated());
    network::ChannelRegistry agentRegistry(makeNegotiated());

    const network::ChannelSpec smallSpec = network::defaultMvpChannelSpecs()[1];
    std::shared_ptr<BridgeChannel> clientSmall;
    std::shared_ptr<BridgeChannel> agentSmall;
    prepareChannel(clientRouter, agentRouter, clientRegistry, smallSpec, clientSmall);
    prepareChannel(agentRouter, clientRouter, agentRegistry, smallSpec, agentSmall);

    diagnostics::DiagnosticsSink clientDiagnostics;
    diagnostics::DiagnosticsSink agentDiagnostics;
    policy::StaticPolicyEngine clientPolicy = makePolicy();
    policy::StaticPolicyEngine agentPolicy = makePolicy();
    module::ModuleHost clientHost(makeRuntime(session::SessionRole::Client,
                                             &clientRegistry,
                                             &clientRouter,
                                             &clientDiagnostics),
                                  &clientPolicy);
    module::ModuleHost agentHost(makeRuntime(session::SessionRole::Agent,
                                            &agentRegistry,
                                            &agentRouter,
                                            &agentDiagnostics),
                                 &agentPolicy);

    auto injector = std::make_shared<FakeInputInjector>();
    modules::input::InputModuleDependencies agentDependencies;
    agentDependencies.injector = injector;

    module::ModuleCreateOptions clientOptions;
    clientOptions.role = session::SessionRole::Client;
    clientOptions.localPlatform = "windows";
    module::ModuleCreateOptions agentOptions;
    agentOptions.role = session::SessionRole::Agent;
    agentOptions.localPlatform = "windows";

    modules::input::InputModuleFactory clientMouseFactory(modules::input::InputModuleKind::Mouse);
    modules::input::InputModuleFactory clientKeyboardFactory(modules::input::InputModuleKind::Keyboard);
    modules::input::InputModuleFactory agentMouseFactory(modules::input::InputModuleKind::Mouse,
                                                         agentDependencies);
    modules::input::InputModuleFactory agentKeyboardFactory(modules::input::InputModuleKind::Keyboard,
                                                            agentDependencies);

    assert(clientMouseFactory.supports("input.mouse", clientOptions));
    assert(agentMouseFactory.supports("input.mouse", agentOptions));
    assert(!clientMouseFactory.supports("input.mouse.agent", clientOptions));
    assert(!agentMouseFactory.supports("input.mouse.client", agentOptions));
    assert(clientMouseFactory.manifest(clientOptions).moduleId == "input.mouse.client");
    assert(agentMouseFactory.manifest(agentOptions).moduleId == "input.mouse.agent");

    auto clientMouse = std::dynamic_pointer_cast<modules::input::InputClientModule>(
        clientMouseFactory.create(clientOptions));
    auto clientKeyboard = std::dynamic_pointer_cast<modules::input::InputClientModule>(
        clientKeyboardFactory.create(clientOptions));
    auto agentMouse = std::dynamic_pointer_cast<modules::input::InputAgentModule>(
        agentMouseFactory.create(agentOptions));
    auto agentKeyboard = std::dynamic_pointer_cast<modules::input::InputAgentModule>(
        agentKeyboardFactory.create(agentOptions));
    assert(clientMouse);
    assert(clientKeyboard);
    assert(agentMouse);
    assert(agentKeyboard);

    assert(clientHost.addModule(clientMouse));
    assert(clientHost.addModule(clientKeyboard));
    assert(agentHost.addModule(agentMouse));
    assert(agentHost.addModule(agentKeyboard));
    assert(allStarted(clientHost.startAllowedModules()));
    assert(allStarted(agentHost.startAllowedModules()));

    modules::input::MouseInputEvent mouse;
    mouse.sequence = 7;
    mouse.monotonicTimestampUsec = 7000;
    mouse.action = modules::input::MouseAction::Move;
    mouse.coordinateSpace = modules::input::InputCoordinateSpace::Pixel;
    mouse.x = 128;
    mouse.y = 256;
    assert(clientMouse->sendMouseEvent(mouse));

    modules::input::KeyboardInputEvent keyboard;
    keyboard.sequence = 8;
    keyboard.monotonicTimestampUsec = 8000;
    keyboard.action = modules::input::KeyboardAction::KeyDown;
    keyboard.virtualKey = 0x41;
    assert(clientKeyboard->sendKeyboardEvent(keyboard));

    assert(injector->mouseEvents == 1);
    assert(injector->keyboardEvents == 1);
    assert(injector->lastMouse.sequence == 7);
    assert(injector->lastMouse.x == 128);
    assert(injector->lastKeyboard.virtualKey == 0x41);
    assert(clientMouse->snapshot().eventsSent == 1);
    assert(agentMouse->snapshot().eventsInjected == 1);
    assert(clientKeyboard->snapshot().eventsSent == 1);
    assert(agentKeyboard->snapshot().eventsInjected == 1);
    assert(clientSmall->sentPackets.size() == 2);
    assert(clientSmall->sentPackets[0].packetType == protocol::PacketType::Mouse);
    assert(clientSmall->sentPackets[0].messageKind == protocol::MessageKind::Event);
    assert((clientSmall->sentPackets[0].flags & protocol::PacketFlagNoResponseRequired) != 0);
    assert((clientSmall->sentPackets[0].flags & protocol::PacketFlagCoalescable) != 0);
    assert(clientSmall->sentPackets[1].packetType == protocol::PacketType::Keyboard);
    assert((clientSmall->sentPackets[1].flags & protocol::PacketFlagNoResponseRequired) != 0);
}

} // namespace

int main()
{
    inputContractsRouteThroughModuleHost();
    return 0;
}
