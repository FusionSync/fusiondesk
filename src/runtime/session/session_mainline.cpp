#include "fusiondesk/runtime/session/session_mainline.h"

#include <utility>

namespace fusiondesk {
namespace runtime {

namespace {

bool supportedMainlineRole(session::SessionRole role)
{
    return role == session::SessionRole::Client ||
           role == session::SessionRole::Agent;
}

void addMessage(SessionMainlineReport& report, std::string message)
{
    report.messages.push_back(std::move(message));
}

session::SessionCreateOptions normalizeSessionOptions(
    const RuntimeHost& host,
    session::SessionCreateOptions options)
{
    if (options.minimumChannels.empty())
        options.minimumChannels = host.profile().minimumChannels;

    if (options.context.requestedFeatures.bits == 0)
        options.context.requestedFeatures = host.profile().defaultFeatures;
    if (options.context.licensedFeatures.bits == 0)
        options.context.licensedFeatures = host.profile().defaultFeatures;
    if (options.context.policyFeatures.bits == 0)
        options.context.policyFeatures = host.profile().defaultFeatures;

    return options;
}

protocol::SessionId createRoleSession(RuntimeHost& host,
                                      session::SessionRole role,
                                      const session::SessionCreateOptions& options)
{
    if (role == session::SessionRole::Client)
        return host.sessions().createClientSession(options);
    if (role == session::SessionRole::Agent)
        return host.sessions().createAgentSession(options);
    return 0;
}

SessionMainlineChannelReport bindMainlineChannel(session::Session& session,
                                                 const SessionMainlineChannel& channel)
{
    SessionMainlineChannelReport report;
    if (channel.channel == nullptr) {
        report.message = "channel instance is required";
        return report;
    }

    report.key = network::ChannelKey{channel.channel->id(), channel.channel->type()};
    network::NetworkManager* network = session.network();
    if (network == nullptr) {
        report.message = "session network is missing";
        return report;
    }

    const network::ChannelRegistryResult bound =
        network->bindChannel(channel.channel);
    report.bound = bound.ok;
    report.message = bound.message;
    if (!bound.ok)
        return report;

    if (!channel.markReady) {
        report.ready = true;
        return report;
    }

    const network::ChannelRegistryResult ready =
        network->markReady(report.key, channel.ready);
    report.ready = ready.ok;
    report.message = ready.message;
    return report;
}

bool reportHasFailure(const SessionMainlineReport& report, bool requireLinkReady)
{
    if (!report.messages.empty())
        return true;
    if (requireLinkReady && !report.linkChannels.ok)
        return true;
    if (!report.mount.ok())
        return true;
    for (const SessionMainlineChannelReport& channel : report.channels) {
        if (!channel.bound || !channel.ready)
            return true;
    }
    for (const module::ModuleStartReport& started : report.moduleStarts) {
        if (!started.started)
            return true;
    }
    return false;
}

void setSessionReport(session::Session& session, SessionMainlineReport& report)
{
    report.sessionId = session.id();
    report.session = &session;
    report.sessionState = session.state();
}

void appendBlockedModulesFromLinkReport(SessionMainlineReport& report)
{
    for (const LinkChannelBindingItem& channel : report.linkChannels.channels) {
        if (!channel.blocked)
            continue;

        for (const std::string& moduleId : channel.requiredByModules) {
            bool exists = false;
            for (const std::string& blocked : report.blockedModules) {
                if (blocked == moduleId) {
                    exists = true;
                    break;
                }
            }

            if (!exists)
                report.blockedModules.push_back(moduleId);
        }
    }
}

bool mountProfileModules(RuntimeHost& host,
                         session::Session& session,
                         const DisplayMvpDependencies& dependencies,
                         SessionMainlineReport& report)
{
    report.mount = host.mountProfileModules(session, dependencies);
    if (!report.mount.ok()) {
        addMessage(report, "profile module mount failed");
        report.sessionState = session.state();
        return false;
    }
    return true;
}

bool startProfileModules(session::Session& session,
                         const module::ModuleStartOptions& startOptions,
                         SessionMainlineReport& report)
{
    module::ModuleHost* moduleHost = session.moduleHost();
    if (moduleHost == nullptr) {
        addMessage(report, "module host is missing");
        report.sessionState = session.state();
        return false;
    }

    if (!report.linkChannels.ok) {
        addMessage(report, "required module channels are not ready");
        appendBlockedModulesFromLinkReport(report);
        report.sessionState = session.state();
        return false;
    }

    report.moduleStarts = moduleHost->startAllowedModules(startOptions);
    for (const module::ModuleStartReport& module : report.moduleStarts) {
        if (module.started)
            report.startedModules.push_back(module.moduleId);
        else
            report.blockedModules.push_back(module.moduleId);
    }

    report.sessionState = session.state();
    return true;
}

} // namespace

SessionMainlineReport SessionMainline::start(SessionMainlineOptions options)
{
    SessionMainlineReport report;
    if (options.host == nullptr) {
        addMessage(report, "runtime host is required");
        return report;
    }

    RuntimeHost& host = *options.host;
    if (host.state() != RuntimeState::Initialized) {
        addMessage(report, "runtime host must be initialized before session mainline start");
        return report;
    }

    if (!supportedMainlineRole(options.role)) {
        addMessage(report, "session mainline currently supports client and agent roles only");
        return report;
    }

    options.sessionOptions = normalizeSessionOptions(host, std::move(options.sessionOptions));
    report.sessionId = createRoleSession(host, options.role, options.sessionOptions);
    report.session = host.sessions().find(report.sessionId);
    if (report.session == nullptr) {
        addMessage(report, "session creation failed");
        return report;
    }

    if (options.startSession && !report.session->start()) {
        report.sessionState = report.session->state();
        addMessage(report, "session start failed");
        return report;
    }
    report.sessionState = report.session->state();

    for (const SessionMainlineChannel& channel : options.channels)
        report.channels.push_back(bindMainlineChannel(*report.session, channel));

    if (options.mountProfileModules) {
        if (!mountProfileModules(host,
                                 *report.session,
                                 options.moduleDependencies,
                                 report)) {
            return report;
        }
    }

    report.linkChannels =
        buildLinkChannelBindingReport(*report.session, options.linkReportOptions);

    if (options.startModules) {
        if (!startProfileModules(*report.session, options.moduleStartOptions, report))
            return report;
    }

    report.sessionState = report.session->state();
    report.ok = !reportHasFailure(report, options.startModules);
    return report;
}

SessionMainlineReport SessionMainline::mountAndStart(
    SessionMainlineModuleOptions options)
{
    SessionMainlineReport report;
    if (options.host == nullptr) {
        addMessage(report, "runtime host is required");
        return report;
    }

    RuntimeHost& host = *options.host;
    if (host.state() != RuntimeState::Initialized) {
        addMessage(report, "runtime host must be initialized before session mainline start");
        return report;
    }

    session::Session* session = options.session;
    if (session == nullptr && options.sessionId != 0)
        session = host.sessions().find(options.sessionId);
    if (session == nullptr) {
        addMessage(report, "session is required");
        return report;
    }

    setSessionReport(*session, report);

    if (options.mountProfileModules) {
        if (!mountProfileModules(host,
                                 *session,
                                 options.moduleDependencies,
                                 report)) {
            return report;
        }
    }

    report.linkChannels =
        buildLinkChannelBindingReport(*session, options.linkReportOptions);

    if (options.startModules) {
        if (!startProfileModules(*session, options.moduleStartOptions, report))
            return report;
    }

    report.sessionState = session->state();
    report.ok = !reportHasFailure(report, options.startModules);
    return report;
}

} // namespace runtime
} // namespace fusiondesk
