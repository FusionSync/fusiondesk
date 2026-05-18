#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/policy/policy_engine.h"

using namespace fusiondesk;

namespace {

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
        return network::SendResult::sent();
    }

private:
    protocol::ChannelId id_ = 0;
    protocol::ChannelType type_ = protocol::ChannelType::Standard;
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
        attached_ = true;
        state_ = module::ModuleState::Attached;
        return true;
    }

    bool start(const module::ModuleStartOptions&) override
    {
        started_ = true;
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

    void handlePacket(const protocol::PacketEnvelope&) override
    {
        ++receivedPackets_;
    }

    std::string diagnostics() const override
    {
        return "fake";
    }

    bool attached() const
    {
        return attached_;
    }

    bool started() const
    {
        return started_;
    }

    int receivedPackets() const
    {
        return receivedPackets_;
    }

private:
    module::ModuleManifest manifest_;
    module::ModuleState state_ = module::ModuleState::Created;
    bool attached_ = false;
    bool started_ = false;
    int receivedPackets_ = 0;
};

class ReconnectAwareFakeModule : public FakeModule, public module::IReconnectAwareModule
{
public:
    ReconnectAwareFakeModule(module::ModuleManifest manifest, std::vector<std::string>* events)
        : FakeModule(std::move(manifest))
        , events_(events)
    {
    }

    void pauseForReconnect(const module::ModuleReconnectOptions& options) override
    {
        lastPauseOptions_ = options;
        ++pauses_;
        if (events_ != nullptr)
            events_->push_back("pause:" + manifest().moduleId);
    }

    void resumeAfterReconnect(const module::ModuleReconnectOptions& options) override
    {
        lastResumeOptions_ = options;
        ++resumes_;
        if (events_ != nullptr)
            events_->push_back("resume:" + manifest().moduleId);
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
    std::vector<std::string>* events_ = nullptr;
    module::ModuleReconnectOptions lastPauseOptions_;
    module::ModuleReconnectOptions lastResumeOptions_;
    int pauses_ = 0;
    int resumes_ = 0;
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
                                protocol::PacketType::Video};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event};
    return capabilities;
}

module::ModuleManifest displayManifest()
{
    module::ModuleManifest manifest;
    manifest.moduleId = "display.screen.client";
    manifest.displayName = "Display Client";
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

module::ModuleManifest versionedDisplayManifest()
{
    module::ModuleManifest manifest = displayManifest();
    module::ModulePeerCompatibility peer;
    peer.peerModuleId = "display.screen.agent";
    peer.minPeerVersion = module::ModuleVersion{1, 0, 0};
    peer.maxPeerVersion = module::ModuleVersion{1, 99, 99};
    peer.compatibilityMode = "v1-family";
    manifest.compatiblePeers = {peer};
    return manifest;
}

module::ModuleManifest simpleManifest(const std::string& moduleId)
{
    module::ModuleManifest manifest;
    manifest.moduleId = moduleId;
    manifest.displayName = moduleId;
    manifest.feature = protocol::feature::Display;
    manifest.roleFlags = module::ModuleRoleClient;
    manifest.supportedPlatforms = {"windows"};
    return manifest;
}

module::ModuleManifest screenManifest(const std::string& moduleId)
{
    module::ModuleManifest manifest = displayManifest();
    manifest.moduleId = moduleId;
    manifest.displayName = moduleId;
    return manifest;
}

network::ChannelKey screenKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::DesktopScreen),
                               protocol::ChannelType::Video};
}

module::ModuleRuntime makeRuntime(network::ChannelRegistry* registry, network::INetworkRouter* router)
{
    module::ModuleRuntime runtime;
    runtime.session.sessionId = 7;
    runtime.session.role = session::SessionRole::Client;
    runtime.session.localPlatform = "windows";
    runtime.session.allowedFeatures.bits = protocol::feature::Display;
    runtime.channels = registry;
    runtime.network = router;
    return runtime;
}

policy::StaticPolicyEngine makePolicy()
{
    protocol::FeatureSet features;
    features.bits = protocol::feature::Display;
    return policy::StaticPolicyEngine(features);
}

void rejectsUnsupportedRoleBeforeAttach()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleRuntime runtime = makeRuntime(&registry, &router);
    runtime.session.role = session::SessionRole::Agent;
    module::ModuleHost host(runtime, &policy);

    auto module = std::make_shared<FakeModule>(displayManifest());
    assert(!host.addModule(module));
    assert(!module->attached());
}

void rejectsUnsupportedPlatformBeforeAttach()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleRuntime runtime = makeRuntime(&registry, &router);
    runtime.session.localPlatform = "linux";
    module::ModuleHost host(runtime, &policy);

    auto module = std::make_shared<FakeModule>(displayManifest());
    assert(!host.addModule(module));
    assert(!module->attached());
}

void deniesStartWhenRequiredChannelIsNotReady()
{
    network::ChannelRegistry registry(makeNegotiated());
    registry.registerSpec(network::defaultMvpChannelSpecs().back());
    network::NetworkRouter router;
    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    auto module = std::make_shared<FakeModule>(displayManifest());
    assert(host.addModule(module));

    std::vector<module::ModuleStartReport> reports = host.startAllowedModules();
    assert(reports.size() == 1);
    assert(!reports.front().started);
    assert(reports.front().decision.reason == policy::DenyReason::MissingDependency);
    assert(!module->started());
}

void startsWhenPolicyAndRequiredChannelAreReady()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(registry.registerSpec(spec).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    auto module = std::make_shared<FakeModule>(displayManifest());
    assert(host.addModule(module));

    std::vector<module::ModuleStartReport> reports = host.startAllowedModules();
    assert(reports.size() == 1);
    assert(reports.front().started);
    assert(reports.front().decision.allowed);
    assert(module->started());

    std::vector<module::ModuleSnapshot> snapshots = host.snapshots();
    assert(snapshots.size() == 1);
    assert(snapshots.front().moduleId == "display.screen.client");
    assert(snapshots.front().state == module::ModuleState::Running);
    assert(snapshots.front().manifest.moduleId == "display.screen.client");
    assert(snapshots.front().diagnostics == "fake");
}

void deniesMissingModuleDependency()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(registry.registerSpec(spec).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    module::ModuleManifest manifest = displayManifest();
    manifest.requiredModules = {"missing.base"};
    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    assert(host.addModule(std::make_shared<FakeModule>(manifest)));
    std::vector<module::ModuleStartReport> reports = host.startAllowedModules();
    assert(reports.size() == 1);
    assert(!reports.front().started);
    assert(reports.front().decision.reason == policy::DenyReason::MissingDependency);
}

void routesIngressOnlyWhileModuleIsRunning()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(registry.registerSpec(spec).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);
    auto target = std::make_shared<FakeModule>(displayManifest());
    assert(host.addModule(target));
    assert(host.startAllowedModules().front().started);

    protocol::PacketEnvelope video;
    video.channelId = screenKey().channelId;
    video.channelType = screenKey().channelType;
    video.packetType = protocol::PacketType::Video;
    video.messageKind = protocol::MessageKind::Event;
    router.submitIncoming(video);
    assert(target->receivedPackets() == 1);

    host.stopAll(module::ModuleStopOptions{});
    router.submitIncoming(video);
    assert(target->receivedPackets() == 1);
}

void replaysRunningModuleIngressForAffectedChannels()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(registry.registerSpec(spec).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);
    auto target = std::make_shared<FakeModule>(displayManifest());
    assert(host.addModule(target));
    assert(host.startAllowedModules().front().started);

    protocol::PacketEnvelope video;
    video.channelId = screenKey().channelId;
    video.channelType = screenKey().channelType;
    video.packetType = protocol::PacketType::Video;
    video.messageKind = protocol::MessageKind::Event;
    router.submitIncoming(video);
    assert(target->receivedPackets() == 1);

    module::ModuleReconnectOptions options;
    options.reason = "rebind video";
    options.affectedChannels = {screenKey()};
    const std::vector<module::ModuleIngressReplayReport> reports =
        host.replayRunningModuleIngressForReconnect(options);
    assert(reports.size() == 1);
    assert(reports.front().moduleId == "display.screen.client");
    assert(reports.front().replayed);
    assert(reports.front().tokenCount == 1);

    router.submitIncoming(video);
    assert(target->receivedPackets() == 2);
}

void startsModulesInAddOrder()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    assert(host.addModule(std::make_shared<FakeModule>(simpleManifest("module.z"))));
    assert(host.addModule(std::make_shared<FakeModule>(simpleManifest("module.a"))));

    const std::vector<module::ModuleStartReport> reports = host.startAllowedModules();
    assert(reports.size() == 2);
    assert(reports[0].moduleId == "module.z");
    assert(reports[1].moduleId == "module.a");

    const std::vector<module::ModuleSnapshot> snapshots = host.snapshots();
    assert(snapshots.size() == 2);
    assert(snapshots[0].moduleId == "module.z");
    assert(snapshots[1].moduleId == "module.a");
}

void rejectsWholeBatchBeforeStartWhenPartialStartIsDisabled()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(registry.registerSpec(spec).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    auto display = std::make_shared<FakeModule>(versionedDisplayManifest());
    auto simple = std::make_shared<FakeModule>(simpleManifest("input.mouse.client"));
    assert(host.addModule(display));
    assert(host.addModule(simple));

    module::ModuleStartOptions options;
    options.peerVersions.push_back(
        module::ModulePeerVersion{"display.screen.agent", module::ModuleVersion{2, 0, 0}, false, {}});

    const std::vector<module::ModuleStartReport> reports = host.startAllowedModules(options);
    assert(reports.size() == 2);
    assert(!reports[0].started);
    assert(reports[0].decision.reason == policy::DenyReason::ModuleVersionMismatch);
    assert(!reports[1].started);
    assert(reports[1].decision.reason == policy::DenyReason::RuntimeHealthBlocked);
    assert(display->state() == module::ModuleState::Attached);
    assert(simple->state() == module::ModuleState::Attached);
}

void allowsIndependentStartWhenPartialStartIsEnabled()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(registry.registerSpec(spec).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);

    auto display = std::make_shared<FakeModule>(versionedDisplayManifest());
    auto simple = std::make_shared<FakeModule>(simpleManifest("input.mouse.client"));
    assert(host.addModule(display));
    assert(host.addModule(simple));

    module::ModuleStartOptions options;
    options.allowPartialStart = true;
    options.peerVersions.push_back(
        module::ModulePeerVersion{"display.screen.agent", module::ModuleVersion{2, 0, 0}, false, {}});

    const std::vector<module::ModuleStartReport> reports = host.startAllowedModules(options);
    assert(reports.size() == 2);
    assert(!reports[0].started);
    assert(reports[0].decision.reason == policy::DenyReason::ModuleVersionMismatch);
    assert(reports[1].started);
    assert(display->state() == module::ModuleState::Attached);
    assert(simple->state() == module::ModuleState::Running);
}

void notifiesReconnectAwareRunningModulesInLifecycleOrder()
{
    network::ChannelRegistry registry(makeNegotiated());
    network::NetworkRouter router;
    network::ChannelSpec spec = network::defaultMvpChannelSpecs().back();
    assert(registry.registerSpec(spec).ok);
    assert(registry.bind(screenKey(), std::make_shared<FakeChannel>(screenKey().channelId, screenKey().channelType)).ok);
    assert(registry.markReady(screenKey(), {}).ok);

    policy::StaticPolicyEngine policy = makePolicy();
    module::ModuleHost host(makeRuntime(&registry, &router), &policy);
    std::vector<std::string> events;
    auto first = std::make_shared<ReconnectAwareFakeModule>(screenManifest("module.first"), &events);
    auto unaware = std::make_shared<FakeModule>(screenManifest("module.unaware"));
    auto second = std::make_shared<ReconnectAwareFakeModule>(screenManifest("module.second"), &events);
    auto unaffected = std::make_shared<ReconnectAwareFakeModule>(simpleManifest("module.unaffected"), &events);

    assert(host.addModule(first));
    assert(host.addModule(unaware));
    assert(host.addModule(second));
    assert(host.addModule(unaffected));

    const std::vector<module::ModuleStartReport> startReports = host.startAllowedModules();
    assert(startReports.size() == 4);
    for (const module::ModuleStartReport& report : startReports)
        assert(report.started);

    module::ModuleReconnectOptions options;
    options.reason = "socket replaced";
    options.affectedChannels = {screenKey()};
    options.reconnectCount = 3;
    options.requestFreshState = true;

    const std::vector<module::ModuleReconnectReport> pauseReports =
        host.pauseRunningModulesForReconnect(options);
    assert(pauseReports.size() == 3);
    assert(pauseReports[0].moduleId == "module.second");
    assert(pauseReports[0].notified);
    assert(pauseReports[1].moduleId == "module.unaware");
    assert(!pauseReports[1].notified);
    assert(pauseReports[2].moduleId == "module.first");
    assert(pauseReports[2].notified);

    const std::vector<module::ModuleReconnectReport> resumeReports =
        host.resumeRunningModulesAfterReconnect(options);
    assert(resumeReports.size() == 3);
    assert(resumeReports[0].moduleId == "module.first");
    assert(resumeReports[0].notified);
    assert(resumeReports[1].moduleId == "module.unaware");
    assert(!resumeReports[1].notified);
    assert(resumeReports[2].moduleId == "module.second");
    assert(resumeReports[2].notified);

    assert(first->pauses() == 1);
    assert(first->resumes() == 1);
    assert(second->pauses() == 1);
    assert(second->resumes() == 1);
    assert(unaffected->pauses() == 0);
    assert(unaffected->resumes() == 0);
    assert(first->lastPauseOptions().reason == "socket replaced");
    assert(first->lastPauseOptions().affectedChannels.size() == 1);
    assert(first->lastPauseOptions().reconnectCount == 3);
    assert(first->lastPauseOptions().requestFreshState);
    assert(second->lastResumeOptions().reason == "socket replaced");
    assert(events.size() == 4);
    assert(events[0] == "pause:module.second");
    assert(events[1] == "pause:module.first");
    assert(events[2] == "resume:module.first");
    assert(events[3] == "resume:module.second");
}

} // namespace

int main()
{
    rejectsUnsupportedRoleBeforeAttach();
    rejectsUnsupportedPlatformBeforeAttach();
    deniesStartWhenRequiredChannelIsNotReady();
    startsWhenPolicyAndRequiredChannelAreReady();
    deniesMissingModuleDependency();
    routesIngressOnlyWhileModuleIsRunning();
    replaysRunningModuleIngressForAffectedChannels();
    startsModulesInAddOrder();
    rejectsWholeBatchBeforeStartWhenPartialStartIsDisabled();
    allowsIndependentStartWhenPartialStartIsEnabled();
    notifiesReconnectAwareRunningModulesInLifecycleOrder();
    return 0;
}
