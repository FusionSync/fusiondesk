#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/session/session_manager.h"

using namespace fusiondesk;

namespace {

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.protocolMajor = protocol::CurrentProtocolMajor;
    capabilities.protocolMinor = protocol::CurrentProtocolMinor;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event};
    return capabilities;
}

session::SessionCreateOptions makeCreateOptions()
{
    session::SessionCreateOptions options;
    options.context.traceId = 99;
    options.context.tenantId = "tenant-a";
    options.context.userId = "user-a";
    options.context.clientDeviceId = "client-device";
    options.context.agentDeviceId = "agent-device";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "linux";
    options.context.localCpuArch = "x86_64";
    options.context.remoteCpuArch = "x86_64";
    options.context.transportMode = session::TransportMode::Lan;
    options.context.securityMode = session::SecurityMode::Enterprise;
    options.context.policyVersion = "policy-1";
    options.context.requestedFeatures.bits = protocol::feature::Display | protocol::feature::Mouse;
    options.context.licensedFeatures.bits = protocol::feature::Display;
    options.context.policyFeatures.bits = protocol::feature::Display;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = network::defaultMvpChannelSpecs();
    return options;
}

bool hasDiagnostic(const diagnostics::DiagnosticsSink& sink,
                   protocol::SessionId sessionId,
                   const std::string& code)
{
    std::vector<diagnostics::DiagnosticEvent> events = sink.eventsForSession(sessionId);
    return std::any_of(events.begin(), events.end(), [&code](const diagnostics::DiagnosticEvent& event) {
        return event.code == code;
    });
}

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

    network::SendResult send(const protocol::PacketEnvelope&) override
    {
        ++sentPackets_;
        return network::SendResult::sent();
    }

    int sentPackets() const
    {
        return sentPackets_;
    }

private:
    protocol::ChannelId id_ = 0;
    protocol::ChannelType type_ = protocol::ChannelType::Standard;
    int sentPackets_ = 0;
};

class ReconnectAwareSessionModule : public module::IModule, public module::IReconnectAwareModule
{
public:
    explicit ReconnectAwareSessionModule(module::ModuleManifest manifest)
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

    bool start(const module::ModuleStartOptions&) override
    {
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

    void pauseForReconnect(const module::ModuleReconnectOptions& options) override
    {
        lastPauseOptions_ = options;
        ++pauses_;
    }

    void resumeAfterReconnect(const module::ModuleReconnectOptions& options) override
    {
        lastResumeOptions_ = options;
        ++resumes_;
    }

    std::string diagnostics() const override
    {
        return manifest_.moduleId;
    }

    int pauses() const
    {
        return pauses_;
    }

    int resumes() const
    {
        return resumes_;
    }

    const module::ModuleReconnectOptions& lastPauseOptions() const
    {
        return lastPauseOptions_;
    }

    const module::ModuleReconnectOptions& lastResumeOptions() const
    {
        return lastResumeOptions_;
    }

private:
    module::ModuleManifest manifest_;
    module::ModuleState state_ = module::ModuleState::Created;
    module::ModuleReconnectOptions lastPauseOptions_;
    module::ModuleReconnectOptions lastResumeOptions_;
    int pauses_ = 0;
    int resumes_ = 0;
};

module::ModuleManifest reconnectAwareDisplayManifest()
{
    module::ModuleManifest manifest;
    manifest.moduleId = "test.reconnect.display";
    manifest.displayName = "Reconnect Test Display";
    manifest.feature = protocol::feature::Display;
    manifest.roleFlags = module::ModuleRoleClient;
    manifest.supportedPlatforms = {"windows"};

    module::ChannelBinding video;
    video.name = "main_screen";
    video.channelId = static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen);
    video.channelType = protocol::ChannelType::Video;
    video.required = true;
    video.consumes = {protocol::PacketType::Video};
    video.produces = {protocol::PacketType::PayloadAck};
    manifest.channels = {video};
    return manifest;
}

void createsStartsAndClosesClientSession()
{
    session::SessionManager manager;
    protocol::SessionId id = manager.createClientSession(makeCreateOptions());
    session::Session* target = manager.find(id);

    const network::ChannelKey screenChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video};
    const network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};

    assert(target != nullptr);
    assert(target->role() == session::SessionRole::Client);
    assert(target->state() == session::SessionState::Created);
    assert(target->network() != nullptr);
    assert(target->network()->registry().snapshots().size() == 3);
    assert(!target->network()->registry().isReady(network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
        protocol::ChannelType::Video}));
    assert(target->policyEngine() != nullptr);
    assert(target->moduleHost() == nullptr);
    assert(hasDiagnostic(manager.diagnostics(), id, "session.created"));

    assert(target->start());
    assert(target->state() == session::SessionState::Running);
    assert(target->context().allowedFeatures.has(protocol::feature::Display));
    assert(!target->context().allowedFeatures.has(protocol::feature::Mouse));
    assert(target->moduleHost() != nullptr);
    assert(hasDiagnostic(manager.diagnostics(), id, "session.state_changed"));
    assert(hasDiagnostic(manager.diagnostics(), id, "policy.features_authorized"));

    auto control = std::make_shared<FakeChannel>(controlChannel.channelId, controlChannel.channelType);
    assert(target->network()->bindChannel(control).ok);
    assert(target->network()->markReady(controlChannel, {}).ok);
    auto oldScreen = std::make_shared<FakeChannel>(screenChannel.channelId, screenChannel.channelType);
    auto newScreen = std::make_shared<FakeChannel>(screenChannel.channelId, screenChannel.channelType);
    assert(target->network()->bindChannel(oldScreen).ok);
    assert(target->network()->markReady(screenChannel, {}).ok);
    auto reconnectAwareModule =
        std::make_shared<ReconnectAwareSessionModule>(reconnectAwareDisplayManifest());
    assert(target->moduleHost()->addModule(reconnectAwareModule));
    const std::vector<module::ModuleStartReport> reports = target->moduleHost()->startAllowedModules();
    assert(reports.size() == 1);
    assert(reports.front().started);

    session::ReconnectRequest reconnect;
    reconnect.reason = "network changed";
    reconnect.degradedChannels = {screenChannel};
    session::ReconnectChannelReplacement replacement;
    replacement.channel = newScreen;
    replacement.ready.endpoint = "replacement-video";
    reconnect.replacementChannels = {replacement};
    assert(manager.reconnect(id, reconnect));
    assert(target->state() == session::SessionState::Running);
    assert(target->context().reconnectCount == 1);
    assert(hasDiagnostic(manager.diagnostics(), id, "network.reconnect_started"));
    assert(hasDiagnostic(manager.diagnostics(), id, "network.reconnect_channel_degraded"));
    assert(hasDiagnostic(manager.diagnostics(), id, "network.reconnect_channel_rebound"));
    assert(hasDiagnostic(manager.diagnostics(), id, "display.keyframe_requested"));
    assert(hasDiagnostic(manager.diagnostics(), id, "module.reconnect_paused"));
    assert(hasDiagnostic(manager.diagnostics(), id, "module.reconnect_resumed"));
    assert(hasDiagnostic(manager.diagnostics(), id, "network.reconnect_finished"));
    assert(reconnectAwareModule->pauses() == 1);
    assert(reconnectAwareModule->resumes() == 1);
    assert(reconnectAwareModule->lastPauseOptions().reason == "network changed");
    assert(reconnectAwareModule->lastPauseOptions().affectedChannels.size() == 1);
    assert(reconnectAwareModule->lastPauseOptions().reconnectCount == 1);
    assert(reconnectAwareModule->lastPauseOptions().requestFreshState);
    assert(reconnectAwareModule->lastResumeOptions().reason == "network changed");
    const session::ReconnectReport& reconnectReport = target->lastReconnectReport();
    assert(reconnectReport.attempted);
    assert(reconnectReport.ok);
    assert(reconnectReport.reconnectCount == 1);
    assert(reconnectReport.reason == "network changed");
    assert(reconnectReport.requestedFreshState);
    assert(reconnectReport.degradedChannels.size() == 1);
    assert(reconnectReport.degradedChannels.front().ok);
    assert(reconnectReport.degradedChannels.front().operation == session::ReconnectChannelOperation::Degrade);
    assert(reconnectReport.degradedChannels.front().key.channelId == screenChannel.channelId);
    assert(reconnectReport.reboundChannels.size() == 1);
    assert(reconnectReport.reboundChannels.front().ok);
    assert(reconnectReport.reboundChannels.front().operation == session::ReconnectChannelOperation::Rebind);
    assert(reconnectReport.reboundChannels.front().key.channelId == screenChannel.channelId);
    assert(reconnectReport.pausedModules.size() == 1);
    assert(reconnectReport.pausedModules.front().moduleId == "test.reconnect.display");
    assert(reconnectReport.replayedIngress.size() == 1);
    assert(reconnectReport.replayedIngress.front().moduleId == "test.reconnect.display");
    assert(reconnectReport.replayedIngress.front().replayed);
    assert(reconnectReport.replayedIngress.front().tokenCount == 1);
    assert(reconnectReport.resumedModules.size() == 1);
    assert(reconnectReport.resumedModules.front().moduleId == "test.reconnect.display");
    const network::ChannelSnapshot screenSnapshot = target->network()->registry().snapshot(screenChannel);
    assert(screenSnapshot.state == network::ChannelLifecycleState::Ready);
    assert(screenSnapshot.ready);
    assert(screenSnapshot.readyInfo.endpoint == "replacement-video");
    assert(target->network()->registry().isReady(controlChannel));

    protocol::PacketEnvelope frame;
    frame.channelId = screenChannel.channelId;
    frame.channelType = screenChannel.channelType;
    frame.packetType = protocol::PacketType::Video;
    frame.messageKind = protocol::MessageKind::Event;
    assert(target->network()->enqueue(frame).queued());
    assert(target->network()->flushNext().status == network::SendStatus::Sent);
    assert(oldScreen->sentPackets() == 0);
    assert(newScreen->sentPackets() == 1);

    std::vector<session::SessionSnapshot> snapshots = manager.snapshots();
    assert(snapshots.size() == 1);
    assert(snapshots.front().state == session::SessionState::Running);
    assert(snapshots.front().diagnosticsCount >= 3);
    assert(snapshots.front().lastReconnect.ok);
    assert(snapshots.front().lastReconnect.reboundChannels.size() == 1);
    assert(snapshots.front().lastReconnect.replayedIngress.size() == 1);

    assert(manager.close(id, {"done"}));
    assert(manager.find(id) == nullptr);
}

void recordsReconnectFailuresInStructuredReport()
{
    session::SessionManager manager;
    protocol::SessionId id = manager.createClientSession(makeCreateOptions());
    session::Session* target = manager.find(id);
    assert(target != nullptr);
    assert(target->start());

    session::ReconnectRequest reconnect;
    reconnect.reason = "bad replacement";
    session::ReconnectChannelReplacement replacement;
    reconnect.replacementChannels = {replacement};
    assert(!manager.reconnect(id, reconnect));
    assert(target->state() == session::SessionState::Running);
    assert(hasDiagnostic(manager.diagnostics(), id, "network.reconnect_channel_rebound_failed"));
    assert(hasDiagnostic(manager.diagnostics(), id, "network.reconnect_finished"));

    const session::ReconnectReport& reconnectReport = target->lastReconnectReport();
    assert(reconnectReport.attempted);
    assert(!reconnectReport.ok);
    assert(reconnectReport.reason == "bad replacement");
    assert(reconnectReport.reboundChannels.size() == 1);
    assert(!reconnectReport.reboundChannels.front().ok);
    assert(reconnectReport.reboundChannels.front().operation == session::ReconnectChannelOperation::Rebind);
    assert(reconnectReport.reboundChannels.front().registryStatus ==
           network::ChannelRegistryStatus::InvalidArgument);
    assert(reconnectReport.replayedIngress.empty());

    const std::vector<session::SessionSnapshot> snapshots = manager.snapshots();
    assert(snapshots.size() == 1);
    assert(!snapshots.front().lastReconnect.ok);
    assert(snapshots.front().lastReconnect.reboundChannels.size() == 1);
}

void createsAgentSessionWithDistinctRole()
{
    session::SessionManager manager;
    protocol::SessionId id = manager.createAgentSession(makeCreateOptions());
    session::Session* target = manager.find(id);

    assert(target != nullptr);
    assert(target->role() == session::SessionRole::Agent);
    assert(target->context().sessionId == id);
    assert(target->context().protocolMajor == protocol::CurrentProtocolMajor);
}

} // namespace

int main()
{
    createsStartsAndClosesClientSession();
    recordsReconnectFailuresInStructuredReport();
    createsAgentSessionWithDistinctRole();
    return 0;
}
