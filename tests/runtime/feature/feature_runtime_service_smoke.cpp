#include <cassert>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/input/input_modules.h"
#include "fusiondesk/runtime/feature/feature_runtime_service.h"
#include "fusiondesk/runtime/runtime_host.h"

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

class FakeInputCapture : public modules::input::IInputCapture
{
public:
    bool open() override
    {
        ++openCalls;
        opened = true;
        return true;
    }

    void close() override
    {
        ++closeCalls;
        opened = false;
    }

    bool pollMouseEvent(modules::input::MouseInputEvent& event) override
    {
        if (mouseEvents.empty())
            return false;

        event = mouseEvents.front();
        mouseEvents.pop_front();
        return true;
    }

    bool pollKeyboardEvent(modules::input::KeyboardInputEvent& event) override
    {
        if (keyboardEvents.empty())
            return false;

        event = keyboardEvents.front();
        keyboardEvents.pop_front();
        return true;
    }

    bool opened = false;
    int openCalls = 0;
    int closeCalls = 0;
    std::deque<modules::input::MouseInputEvent> mouseEvents;
    std::deque<modules::input::KeyboardInputEvent> keyboardEvents;
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

class GateFeaturePolicy : public runtime::feature::IFeatureRuntimePolicy
{
public:
    runtime::feature::FeatureRuntimePolicyDecision authorize(
        const runtime::feature::FeatureRuntimePolicyContext& context) override
    {
        if (context.operation == runtime::feature::FeatureRuntimeOperation::InputKeyboardEvent) {
            return runtime::feature::FeatureRuntimePolicyDecision::deny(
                protocol::ResponseStatus::DeniedByPolicy,
                "keyboard input denied by smoke policy");
        }

        return runtime::feature::FeatureRuntimePolicyDecision::allow(false);
    }

    void audit(const runtime::feature::FeatureRuntimeAuditEvent& event) override
    {
        auditEvents.push_back(event);
    }

    std::vector<runtime::feature::FeatureRuntimeAuditEvent> auditEvents;
};

class MouseDenyPolicy : public runtime::feature::IFeatureRuntimePolicy
{
public:
    runtime::feature::FeatureRuntimePolicyDecision authorize(
        const runtime::feature::FeatureRuntimePolicyContext& context) override
    {
        if (context.operation == runtime::feature::FeatureRuntimeOperation::InputMouseEvent) {
            return runtime::feature::FeatureRuntimePolicyDecision::deny(
                protocol::ResponseStatus::DeniedByPolicy,
                "mouse input denied by smoke policy");
        }

        return runtime::feature::FeatureRuntimePolicyDecision::allow(false);
    }

    void audit(const runtime::feature::FeatureRuntimeAuditEvent& event) override
    {
        auditEvents.push_back(event);
    }

    std::vector<runtime::feature::FeatureRuntimeAuditEvent> auditEvents;
};

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video,
                                protocol::PacketType::Mouse,
                                protocol::PacketType::Keyboard};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Error};
    return capabilities;
}

runtime::RuntimeOptions makeRuntimeOptions()
{
    runtime::RuntimeOptions options;
    options.profile.profileId = "feature-runtime-service-smoke";
    options.profile.defaultFeatures.bits =
        protocol::feature::Mouse |
        protocol::feature::Keyboard;
    options.profile.requiredModules = {
        "input.mouse",
        "input.keyboard",
    };
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    return options;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host)
{
    session::SessionCreateOptions options;
    options.context.userId = "feature-user";
    options.context.tenantId = "feature-tenant";
    options.context.clientDeviceId = "feature-client";
    options.context.agentDeviceId = "feature-agent";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

std::shared_ptr<BridgeChannel> bindBridgeChannel(session::Session& local,
                                                 session::Session& peer,
                                                 network::ChannelKey key)
{
    std::shared_ptr<BridgeChannel> channel =
        std::make_shared<BridgeChannel>(key, &peer.network()->router());
    assert(local.network()->bindChannel(channel).ok);
    network::ChannelReadyInfo ready;
    ready.endpoint = "feature-runtime-smoke";
    assert(local.network()->markReady(key, ready).ok);
    return channel;
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

void mountInputModules(runtime::RuntimeHost& host,
                       session::Session& client,
                       session::Session& agent,
                       std::shared_ptr<modules::input::IInputInjector> injector)
{
    runtime::DisplayMvpDependencies clientDependencies;
    runtime::DisplayMvpDependencies agentDependencies;
    agentDependencies.inputInjector = std::move(injector);
    assert(host.mountProfileModules(client, clientDependencies).ok());
    assert(host.mountProfileModules(agent, agentDependencies).ok());
    assert(allStarted(client.moduleHost()->startAllowedModules()));
    assert(allStarted(agent.moduleHost()->startAllowedModules()));
}

void featureRuntimeServicePumpsInputLifecycle()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    const protocol::SessionId clientId =
        host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId =
        host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* client = host.sessions().find(clientId);
    session::Session* agent = host.sessions().find(agentId);
    assert(client != nullptr);
    assert(agent != nullptr);
    assert(client->start());
    assert(agent->start());

    const network::ChannelKey smallData{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
    std::shared_ptr<BridgeChannel> clientSmall =
        bindBridgeChannel(*client, *agent, smallData);
    bindBridgeChannel(*agent, *client, smallData);

    auto capture = std::make_shared<FakeInputCapture>();
    auto injector = std::make_shared<FakeInputInjector>();
    mountInputModules(host, *client, *agent, injector);

    modules::input::MouseInputEvent mouse;
    mouse.sequence = 10;
    mouse.monotonicTimestampUsec = 1000;
    mouse.action = modules::input::MouseAction::Move;
    mouse.coordinateSpace = modules::input::InputCoordinateSpace::Pixel;
    mouse.x = 320;
    mouse.y = 240;
    capture->mouseEvents.push_back(mouse);

    modules::input::KeyboardInputEvent keyboard;
    keyboard.sequence = 11;
    keyboard.monotonicTimestampUsec = 1100;
    keyboard.action = modules::input::KeyboardAction::KeyDown;
    keyboard.virtualKey = 65;
    capture->keyboardEvents.push_back(keyboard);

    runtime::feature::FeatureRuntimeServiceOptions serviceOptions;
    serviceOptions.session = client;
    serviceOptions.inputCapture = capture;
    serviceOptions.maxInputEventsPerPump = 4;
    runtime::feature::FeatureRuntimeService service(serviceOptions);
    assert(service.start().ok);

    const runtime::feature::FeatureRuntimePumpResult pump = service.pumpOnce();
    assert(pump.active);
    assert(pump.mouseEventsSent == 1);
    assert(pump.keyboardEventsSent == 1);
    assert(injector->mouseEvents == 1);
    assert(injector->keyboardEvents == 1);
    assert(injector->lastMouse.x == 320);
    assert(injector->lastKeyboard.virtualKey == 65);
    assert(clientSmall->sentPackets.size() == 2);
    assert(clientSmall->sentPackets[0].packetType == protocol::PacketType::Mouse);
    assert(clientSmall->sentPackets[0].messageKind == protocol::MessageKind::Event);
    assert((clientSmall->sentPackets[0].flags & protocol::PacketFlagNoResponseRequired) != 0);
    assert((clientSmall->sentPackets[0].flags & protocol::PacketFlagCoalescable) != 0);
    assert(clientSmall->sentPackets[1].packetType == protocol::PacketType::Keyboard);
    assert((clientSmall->sentPackets[1].flags & protocol::PacketFlagNoResponseRequired) != 0);

    const runtime::feature::FeatureRuntimeServiceSnapshot snapshot = service.snapshot();
    assert(snapshot.active);
    assert(snapshot.pumpCount == 1);
    assert(snapshot.mouseEventsSent == 1);
    assert(snapshot.keyboardEventsSent == 1);

    service.stop();
    capture->mouseEvents.push_back(mouse);
    const runtime::feature::FeatureRuntimePumpResult stoppedPump = service.pumpOnce();
    assert(!stoppedPump.active);
    assert(injector->mouseEvents == 1);
}

void featureRuntimeServiceOwnsCaptureLifecycleAndAppliesPolicy()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    const protocol::SessionId clientId =
        host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId =
        host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* client = host.sessions().find(clientId);
    session::Session* agent = host.sessions().find(agentId);
    assert(client != nullptr);
    assert(agent != nullptr);
    assert(client->start());
    assert(agent->start());

    const network::ChannelKey smallData{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
    bindBridgeChannel(*client, *agent, smallData);
    bindBridgeChannel(*agent, *client, smallData);

    auto capture = std::make_shared<FakeInputCapture>();
    auto injector = std::make_shared<FakeInputInjector>();
    mountInputModules(host, *client, *agent, injector);
    assert(capture->openCalls == 0);

    modules::input::MouseInputEvent mouse;
    mouse.sequence = 21;
    mouse.action = modules::input::MouseAction::ButtonDown;
    mouse.button = modules::input::MouseButton::Left;
    capture->mouseEvents.push_back(mouse);

    modules::input::KeyboardInputEvent keyboard;
    keyboard.sequence = 22;
    keyboard.action = modules::input::KeyboardAction::KeyDown;
    keyboard.virtualKey = 66;
    capture->keyboardEvents.push_back(keyboard);

    auto policy = std::make_shared<GateFeaturePolicy>();
    runtime::feature::FeatureRuntimeServiceOptions serviceOptions;
    serviceOptions.session = client;
    serviceOptions.inputCapture = capture;
    serviceOptions.policy = policy;
    serviceOptions.maxInputEventsPerPump = 4;
    serviceOptions.manageInputCaptureLifecycle = true;
    runtime::feature::FeatureRuntimeService service(serviceOptions);

    assert(service.start().ok);
    assert(capture->opened);
    assert(capture->openCalls == 1);

    const runtime::feature::FeatureRuntimePumpResult pump = service.pumpOnce();
    assert(pump.mouseEventsSent == 1);
    assert(pump.keyboardEventsSent == 0);
    assert(pump.policyDenials == 1);
    assert(injector->mouseEvents == 1);
    assert(injector->keyboardEvents == 0);

    const runtime::feature::FeatureRuntimeServiceSnapshot snapshot = service.snapshot();
    assert(snapshot.inputCaptureOpenedByService);
    assert(snapshot.policyDenials == 1);
    assert(snapshot.auditEvents == 1);
    assert(policy->auditEvents.size() == 1);
    assert(policy->auditEvents.front().context.operation ==
           runtime::feature::FeatureRuntimeOperation::InputKeyboardEvent);

    service.stop();
    assert(!capture->opened);
    assert(capture->closeCalls == 1);
}

void featureRuntimeServicePolicyDenialDoesNotStarveKeyboard()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    const protocol::SessionId clientId =
        host.sessions().createClientSession(makeSessionOptions(host));
    const protocol::SessionId agentId =
        host.sessions().createAgentSession(makeSessionOptions(host));
    session::Session* client = host.sessions().find(clientId);
    session::Session* agent = host.sessions().find(agentId);
    assert(client != nullptr);
    assert(agent != nullptr);
    assert(client->start());
    assert(agent->start());

    const network::ChannelKey smallData{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
        protocol::ChannelType::Standard};
    bindBridgeChannel(*client, *agent, smallData);
    bindBridgeChannel(*agent, *client, smallData);

    auto capture = std::make_shared<FakeInputCapture>();
    auto injector = std::make_shared<FakeInputInjector>();
    mountInputModules(host, *client, *agent, injector);

    modules::input::MouseInputEvent mouse;
    mouse.sequence = 41;
    mouse.action = modules::input::MouseAction::Move;
    mouse.coordinateSpace = modules::input::InputCoordinateSpace::Pixel;
    mouse.x = 1;
    mouse.y = 2;
    capture->mouseEvents.push_back(mouse);

    modules::input::KeyboardInputEvent keyboard;
    keyboard.sequence = 42;
    keyboard.action = modules::input::KeyboardAction::KeyDown;
    keyboard.virtualKey = 67;
    capture->keyboardEvents.push_back(keyboard);

    auto policy = std::make_shared<MouseDenyPolicy>();
    runtime::feature::FeatureRuntimeServiceOptions serviceOptions;
    serviceOptions.session = client;
    serviceOptions.inputCapture = capture;
    serviceOptions.policy = policy;
    serviceOptions.maxInputEventsPerPump = 4;
    runtime::feature::FeatureRuntimeService service(serviceOptions);

    assert(service.start().ok);
    const runtime::feature::FeatureRuntimePumpResult pump = service.pumpOnce();
    assert(pump.mouseEventsSent == 0);
    assert(pump.keyboardEventsSent == 1);
    assert(pump.policyDenials == 1);
    assert(injector->mouseEvents == 0);
    assert(injector->keyboardEvents == 1);
    assert(policy->auditEvents.size() == 1);
    assert(policy->auditEvents.front().context.operation ==
           runtime::feature::FeatureRuntimeOperation::InputMouseEvent);
}

void featureRuntimeServiceDestructorStopsOwnedAdapters()
{
    runtime::RuntimeHost host;
    assert(host.initialize(makeRuntimeOptions()));

    const protocol::SessionId clientId =
        host.sessions().createClientSession(makeSessionOptions(host));
    session::Session* client = host.sessions().find(clientId);
    assert(client != nullptr);
    assert(client->start());

    auto capture = std::make_shared<FakeInputCapture>();
    {
        runtime::feature::FeatureRuntimeServiceOptions serviceOptions;
        serviceOptions.session = client;
        serviceOptions.inputCapture = capture;
        serviceOptions.manageInputCaptureLifecycle = true;
        runtime::feature::FeatureRuntimeService service(serviceOptions);

        assert(service.start().ok);
        assert(service.start().ok);
        assert(capture->opened);
        assert(capture->openCalls == 1);
    }

    assert(!capture->opened);
    assert(capture->closeCalls == 1);
}

} // namespace

int main()
{
    featureRuntimeServicePumpsInputLifecycle();
    featureRuntimeServiceOwnsCaptureLifecycleAndAppliesPolicy();
    featureRuntimeServicePolicyDenialDoesNotStarveKeyboard();
    featureRuntimeServiceDestructorStopsOwnedAdapters();
    return 0;
}
