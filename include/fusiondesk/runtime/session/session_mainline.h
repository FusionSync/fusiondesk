#ifndef FUSIONDESK_RUNTIME_SESSION_SESSION_MAINLINE_H
#define FUSIONDESK_RUNTIME_SESSION_SESSION_MAINLINE_H

#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/network/network_manager.h"
#include "fusiondesk/runtime/runtime_host.h"
#include "fusiondesk/runtime/session/link_channel_binding_report.h"

namespace fusiondesk {
namespace runtime {

struct SessionMainlineChannel
{
    std::shared_ptr<network::IChannel> channel;
    network::ChannelReadyInfo ready;
    bool markReady = true;
};

struct SessionMainlineOptions
{
    RuntimeHost* host = nullptr;
    session::SessionRole role = session::SessionRole::Client;
    session::SessionCreateOptions sessionOptions;
    DisplayMvpDependencies moduleDependencies;
    std::vector<SessionMainlineChannel> channels;
    LinkChannelBindingReportOptions linkReportOptions;
    module::ModuleStartOptions moduleStartOptions;
    bool startSession = true;
    bool mountProfileModules = true;
    bool startModules = true;
};

struct SessionMainlineModuleOptions
{
    RuntimeHost* host = nullptr;
    session::Session* session = nullptr;
    protocol::SessionId sessionId = 0;
    DisplayMvpDependencies moduleDependencies;
    LinkChannelBindingReportOptions linkReportOptions;
    module::ModuleStartOptions moduleStartOptions;
    bool mountProfileModules = true;
    bool startModules = true;
};

struct SessionMainlineChannelReport
{
    network::ChannelKey key;
    bool bound = false;
    bool ready = false;
    std::string message;
};

struct SessionMainlineReport
{
    bool ok = false;
    protocol::SessionId sessionId = 0;
    session::Session* session = nullptr;
    session::SessionState sessionState = session::SessionState::Created;
    std::vector<std::string> messages;
    std::vector<SessionMainlineChannelReport> channels;
    LinkChannelBindingReport linkChannels;
    ProfileMountReport mount;
    std::vector<module::ModuleStartReport> moduleStarts;
    std::vector<std::string> startedModules;
    std::vector<std::string> blockedModules;
};

class SessionMainline
{
public:
    static SessionMainlineReport start(SessionMainlineOptions options);
    static SessionMainlineReport mountAndStart(SessionMainlineModuleOptions options);
};

} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_SESSION_SESSION_MAINLINE_H
