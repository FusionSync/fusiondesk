#include "pc_runtime_exchange_shell.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include "pc_display_diagnostics.h"
#include "pc_profile_options.h"
#include "pc_session_diagnostics.h"
#include "pc_shell_options.h"

#include "fusiondesk/adapters/qt/qt_tcp_transport_socket.h"
#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/runtime/display/display_codec_peer_profile.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"
#include "fusiondesk/runtime/feature/feature_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_timer_bridge.h"
#include "fusiondesk/runtime/session/link_channel_binding_report.h"

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

std::string joinedModules(const std::vector<std::string>& modules)
{
    std::string result;
    for (const std::string& module : modules) {
        if (!result.empty())
            result += ",";
        result += module;
    }
    return result;
}

} // namespace

const char* messageKindName(protocol::MessageKind value)
{
    switch (value) {
    case protocol::MessageKind::Event:
        return "Event";
    case protocol::MessageKind::Request:
        return "Request";
    case protocol::MessageKind::Response:
        return "Response";
    case protocol::MessageKind::Ack:
        return "Ack";
    case protocol::MessageKind::Error:
        return "Error";
    case protocol::MessageKind::Progress:
        return "Progress";
    case protocol::MessageKind::StreamStart:
        return "StreamStart";
    case protocol::MessageKind::StreamChunk:
        return "StreamChunk";
    case protocol::MessageKind::StreamEnd:
        return "StreamEnd";
    case protocol::MessageKind::Cancel:
        return "Cancel";
    }
    return "Unknown";
}

const char* responseStatusName(protocol::ResponseStatus value)
{
    switch (value) {
    case protocol::ResponseStatus::Ok:
        return "Ok";
    case protocol::ResponseStatus::Accepted:
        return "Accepted";
    case protocol::ResponseStatus::Progress:
        return "Progress";
    case protocol::ResponseStatus::InvalidArgument:
        return "InvalidArgument";
    case protocol::ResponseStatus::Unauthorized:
        return "Unauthorized";
    case protocol::ResponseStatus::DeniedByPolicy:
        return "DeniedByPolicy";
    case protocol::ResponseStatus::Unsupported:
        return "Unsupported";
    case protocol::ResponseStatus::NotFound:
        return "NotFound";
    case protocol::ResponseStatus::Conflict:
        return "Conflict";
    case protocol::ResponseStatus::Busy:
        return "Busy";
    case protocol::ResponseStatus::Timeout:
        return "Timeout";
    case protocol::ResponseStatus::Cancelled:
        return "Cancelled";
    case protocol::ResponseStatus::TooLarge:
        return "TooLarge";
    case protocol::ResponseStatus::BackPressure:
        return "BackPressure";
    case protocol::ResponseStatus::ChannelUnavailable:
        return "ChannelUnavailable";
    case protocol::ResponseStatus::Failed:
        return "Failed";
    case protocol::ResponseStatus::InternalError:
        return "InternalError";
    case protocol::ResponseStatus::ProtocolError:
        return "ProtocolError";
    }
    return "Unknown";
}

const char* denyReasonName(policy::DenyReason value)
{
    switch (value) {
    case policy::DenyReason::None:
        return "None";
    case policy::DenyReason::FeatureNotLicensed:
        return "FeatureNotLicensed";
    case policy::DenyReason::FeatureDisabledByPolicy:
        return "FeatureDisabledByPolicy";
    case policy::DenyReason::UnsupportedPlatform:
        return "UnsupportedPlatform";
    case policy::DenyReason::MissingDependency:
        return "MissingDependency";
    case policy::DenyReason::ModuleVersionMismatch:
        return "ModuleVersionMismatch";
    case policy::DenyReason::TransportNotAllowed:
        return "TransportNotAllowed";
    case policy::DenyReason::RuntimeHealthBlocked:
        return "RuntimeHealthBlocked";
    }
    return "Unknown";
}

void writeLinkChannelBindingReport(
    const runtime::LinkChannelBindingReport& report)
{
    for (const std::string& message : report.messages)
        writeShellError("link channel report: " + message);

    for (const runtime::LinkChannelBindingItem& channel : report.channels) {
        if (!channel.blocked)
            continue;

        writeShellError("link channel blocked: name=" + channel.name +
                        " id=" + std::to_string(channel.key.channelId) +
                        " type=" +
                        std::to_string(static_cast<unsigned int>(channel.key.channelType)) +
                        " listening=" + boolValue(channel.listening) +
                        " bound=" + boolValue(channel.bound) +
                        " ready=" + boolValue(channel.ready) +
                        " modules=" + joinedModules(channel.requiredByModules) +
                        " message=" + channel.message);
    }
}

void pollSessionTransports(runtime::qt::QtRuntimeTransportManager& transportManager,
                           protocol::SessionId sessionId)
{
    runtime::qt::QtSessionTransportConnector* connector = transportManager.connector(sessionId);
    if (connector == nullptr)
        return;

    for (const std::shared_ptr<adapters::qt::QtTcpTransportSocket>& transport : connector->transports())
        transport->poll();
}

void runBoundedEventLoop(
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    int durationMs,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (durationMs <= 0)
        return;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        pollSessionTransports(transportManager, sessionId);
        if (featureService != nullptr)
            featureService->pumpOnce();
        if (clipboardService != nullptr) {
            clipboardService->pumpOnce();
            clipboardService->expirePendingReads(
                runtime::qt::QtTimerBridge::monotonicNowUsec());
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
}

bool waitForRequiredModuleChannels(session::Session& session,
                                   runtime::qt::QtRuntimeTransportManager& transportManager,
                                   protocol::SessionId sessionId,
                                   int timeoutMs,
                                   const std::vector<network::ChannelKey>& listeningChannels)
{
    runtime::LinkChannelBindingReport report =
        runtime::buildLinkChannelBindingReport(
            session,
            makeLinkReportOptions(listeningChannels));
    if (report.ok)
        return true;

    if (timeoutMs <= 0) {
        writeLinkChannelBindingReport(report);
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        pollSessionTransports(transportManager, sessionId);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
        report = runtime::buildLinkChannelBindingReport(
            session,
            makeLinkReportOptions(listeningChannels));
        if (report.ok)
            return true;
    }

    report = runtime::buildLinkChannelBindingReport(
        session,
        makeLinkReportOptions(listeningChannels));
    if (!report.ok)
        writeLinkChannelBindingReport(report);
    return report.ok;
}

const network::ChannelSpec* findChannelSpecByName(
    const std::vector<network::ChannelSpec>& specs,
    const std::string& name)
{
    for (const network::ChannelSpec& spec : specs) {
        if (spec.name == name)
            return &spec;
    }
    return nullptr;
}

bool appendPeerProfileChannel(const std::string& value,
                              const std::vector<network::ChannelSpec>& specs,
                              const std::string& clientReadyPrefix,
                              const std::string& agentReadyPrefix,
                              runtime::connection::PeerProfileExchangeRequest& request,
                              std::vector<std::string>& messages)
{
    const std::size_t delimiter = value.find('=');
    if (delimiter == std::string::npos || delimiter == 0 || delimiter + 1 >= value.size()) {
        messages.push_back("peer profile channel must use name=endpoint");
        return false;
    }

    const std::string name = value.substr(0, delimiter);
    const std::string endpoint = value.substr(delimiter + 1);
    const network::ChannelSpec* spec = findChannelSpecByName(specs, name);
    if (spec == nullptr) {
        messages.push_back("unknown peer profile channel: " + name);
        return false;
    }

    runtime::connection::PeerConnectionChannelRequest channel;
    channel.key = spec->key;
    channel.endpoint = endpoint;
    channel.clientReadyEndpoint = clientReadyPrefix + "-" + spec->name;
    channel.agentReadyEndpoint = agentReadyPrefix + "-" + spec->name;
    request.connectionPlan.channels.push_back(std::move(channel));
    return true;
}

runtime::connection::PeerProfileExchangeRequest makePeerProfileRequest(
    int argc,
    char** argv,
    protocol::SessionId sessionId,
    const std::vector<network::ChannelSpec>& specs,
    const runtime::ProductDisplayCodecPolicy& policy,
    std::vector<std::string>& messages)
{
    runtime::connection::PeerProfileExchangeRequest request;
    request.connectionPlan.knownSpecs = specs;
    request.clientSessionId = sessionId;
    request.agentSessionId = sessionIdOptionValue(argc,
                                                  argv,
                                                  "--peer-profile-agent-session",
                                                  sessionId);

    std::string clientReadyPrefix = optionValue(argc, argv, "--peer-profile-client-ready-prefix");
    if (clientReadyPrefix.empty())
        clientReadyPrefix = "pc-peer-client";
    std::string agentReadyPrefix = optionValue(argc, argv, "--peer-profile-agent-ready-prefix");
    if (agentReadyPrefix.empty())
        agentReadyPrefix = "pc-peer-agent";

    for (const std::string& channel :
         optionValues(argc, argv, "--peer-profile-channel")) {
        appendPeerProfileChannel(channel,
                                 specs,
                                 clientReadyPrefix,
                                 agentReadyPrefix,
                                 request,
                                 messages);
    }

    if (request.connectionPlan.channels.empty())
        messages.push_back("peer profile request requires --peer-profile-channel");

    if (displayCodecFdppNegotiationRequested(argc, argv))
        appendClientDisplayCodecPeerProfileExtension(argc,
                                                     argv,
                                                     policy,
                                                     request,
                                                     messages);

    return request;
}

bool runClientPeerProfileExchange(
    int argc,
    char** argv,
    protocol::SessionId sessionId,
    const std::vector<network::ChannelSpec>& specs,
    const runtime::ProductDisplayCodecPolicy& policy,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    runtime::qt::QtPeerProfileRuntimeService& peerProfileService,
    DisplayCodecPeerNegotiationState* peerCodecNegotiation)
{
    std::vector<std::string> messages;
    runtime::connection::PeerProfileExchangeRequest request =
        makePeerProfileRequest(argc, argv, sessionId, specs, policy, messages);
    if (!messages.empty()) {
        writeShellMessages(messages);
        return false;
    }

    runtime::connection::PeerProfileRuntimeExchangeOptions options;
    options.wire.messageId = 0;
    options.wire.timeoutMs = static_cast<std::uint32_t>(
        intOptionValue(argc, argv, "--peer-profile-timeout-ms", 3000));
    options.wire.monotonicTimestampUsec = runtime::qt::QtTimerBridge::monotonicNowUsec();
    const runtime::connection::PeerProfileRuntimeDispatchResult dispatched =
        peerProfileService.requestPeerProfile(request, options);
    if (!dispatched.ok) {
        writeShellMessages(dispatched.messages);
        return false;
    }

    const int waitMs = intOptionValue(argc, argv, "--peer-profile-wait-ms", 5000);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < waitMs) {
        pollSessionTransports(transportManager, sessionId);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        const runtime::qt::QtPeerProfileRuntimeApplyResult applied =
            peerProfileService.applyCompletedClientProfiles();
        if (applied.appliedCompletions > 0) {
            if (!applied.ok)
                writeShellMessages(applied.messages);
            if (applied.ok &&
                displayCodecFdppNegotiationRequested(argc, argv) &&
                peerCodecNegotiation != nullptr) {
                std::vector<std::string> codecMessages;
                if (!readClientDisplayCodecPeerProfileCompletion(argc,
                                                                 argv,
                                                                 peerProfileService,
                                                                 *peerCodecNegotiation,
                                                                 codecMessages)) {
                    writeShellMessages(codecMessages);
                    return false;
                }
            }
            return applied.ok;
        }
    }

    writeShellError("peer profile exchange timed out");
    return false;
}

bool waitForAgentDisplayCodecPeerProfileNegotiation(
    int argc,
    char** argv,
    protocol::SessionId sessionId,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    const DisplayCodecPeerNegotiationState& peerCodecNegotiation)
{
    const int waitMs = intOptionValue(argc,
                                      argv,
                                      "--display-codec-fdpp-wait-ms",
                                      intOptionValue(argc,
                                                     argv,
                                                     "--peer-profile-wait-ms",
                                                     5000));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < waitMs) {
        pollSessionTransports(transportManager, sessionId);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (peerCodecNegotiation.attempted) {
            if (!peerCodecNegotiation.ok)
                writeShellMessages(peerCodecNegotiation.messages);
            return peerCodecNegotiation.ok;
        }
    }

    writeShellError("display codec FDPP negotiation timed out");
    return false;
}

runtime::connection::ModuleInventory makeLocalModuleInventory(
    protocol::SessionId sessionId,
    session::Session& session)
{
    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr)
        return runtime::connection::makeModuleInventory(sessionId, {});

    return runtime::connection::makeModuleInventory(sessionId, host->manifests());
}

std::size_t latestCompletionModuleCount(
    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot& snapshot)
{
    for (auto it = snapshot.completions.rbegin();
         it != snapshot.completions.rend();
         ++it) {
        if (it->ok)
            return it->inventory.manifests.size();
    }
    return 0;
}

bool hasOkCompletion(
    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot& snapshot)
{
    for (const runtime::connection::ModuleInventoryRuntimeCompletion& completion :
         snapshot.completions) {
        if (completion.ok)
            return true;
    }
    return false;
}

bool latestTerminalCompletion(
    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot& snapshot,
    runtime::connection::ModuleInventoryRuntimeCompletion& completion)
{
    for (auto it = snapshot.completions.rbegin();
         it != snapshot.completions.rend();
         ++it) {
        if (!it->terminal)
            continue;

        completion = *it;
        return true;
    }
    return false;
}

std::vector<std::string> moduleInventoryCompletionFailureMessages(
    const runtime::connection::ModuleInventoryRuntimeCompletion& completion)
{
    std::vector<std::string> messages;
    messages.push_back(
        std::string("module inventory exchange failed: kind=") +
        messageKindName(completion.response.messageKind) +
        " status=" + responseStatusName(completion.response.responseStatus) +
        " responseTo=" + std::to_string(completion.response.responseTo));

    for (const std::string& message : completion.messages)
        messages.push_back("module inventory completion: " + message);

    return messages;
}

bool moduleInventoryDiagnosticsRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--print-module-inventory-diagnostics") ||
           hasArg(argc, argv, "--print-session-diagnostics");
}

void writeModuleInventoryDiagnostics(
    const runtime::connection::ModuleInventoryRuntimeService& service,
    const char* phase)
{
    const runtime::connection::ModuleInventoryRuntimeServiceSnapshot snapshot =
        service.snapshot();
    const std::size_t responseRemoteModules = latestCompletionModuleCount(snapshot);
    const bool ok = hasOkCompletion(snapshot) ||
                    snapshot.responder.hasLastRemoteInventory;

    std::cout << "module.inventory"
              << " phase=" << phase
              << " ok=" << boolValue(ok)
              << " active=" << boolValue(snapshot.active)
              << " pending=" << snapshot.pendingRequests
              << " completed=" << snapshot.completedResponses
              << " expired=" << snapshot.expiredRequests
              << " responderReceived=" << boolValue(snapshot.responder.hasLastRemoteInventory)
              << " responderRemoteSession=" << snapshot.responder.lastRemoteSessionId
              << " responderRemote=" << snapshot.responder.lastRemoteModuleCount
              << " responseRemote=" << responseRemoteModules
              << " messages=" << snapshot.messages.size()
              << std::endl;

    for (const std::string& message : snapshot.messages)
        std::cout << "module.inventory.message"
                  << " phase=" << phase
                  << " text=" << message
                  << std::endl;

    for (std::size_t index = 0; index < snapshot.completions.size(); ++index) {
        const runtime::connection::ModuleInventoryRuntimeCompletion& completion =
            snapshot.completions[index];
        std::cout << "module.inventory.completion"
                  << " phase=" << phase
                  << " index=" << index
                  << " ok=" << boolValue(completion.ok)
                  << " terminal=" << boolValue(completion.terminal)
                  << " kind=" << messageKindName(completion.response.messageKind)
                  << " status=" << responseStatusName(completion.response.responseStatus)
                  << " responseTo=" << completion.response.responseTo
                  << " remoteModules=" << completion.inventory.manifests.size()
                  << " messages=" << completion.messages.size()
                  << std::endl;

        for (const std::string& message : completion.messages)
            std::cout << "module.inventory.completion.message"
                      << " phase=" << phase
                      << " index=" << index
                      << " text=" << message
                      << std::endl;
    }
}

void writeModuleInventoryDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::connection::ModuleInventoryRuntimeService* service,
    const char* phase)
{
    if (service == nullptr || !moduleInventoryDiagnosticsRequested(argc, argv))
        return;

    writeModuleInventoryDiagnostics(*service, phase);
}

enum class ModuleInventoryWaitState
{
    Pending,
    Complete,
    Failed
};

ModuleInventoryWaitResult waitForModuleInventory(
    runtime::connection::ModuleInventoryRuntimeService& service,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    bool waitForResponseCompletion,
    bool waitForResponderRemote,
    int timeoutMs,
    runtime::connection::ModuleInventory& remoteInventory)
{
    ModuleInventoryWaitResult result;
    auto complete = [&]() {
        const runtime::connection::ModuleInventoryRuntimeServiceSnapshot snapshot =
            service.snapshot();
        if (waitForResponseCompletion) {
            runtime::connection::ModuleInventoryRuntimeCompletion completion;
            if (latestTerminalCompletion(snapshot, completion)) {
                if (!completion.ok) {
                    result.terminalFailure = true;
                    result.messages =
                        moduleInventoryCompletionFailureMessages(completion);
                    return ModuleInventoryWaitState::Failed;
                }

                remoteInventory = completion.inventory;
                result.ok = true;
                return ModuleInventoryWaitState::Complete;
            }
        }

        if (waitForResponderRemote) {
            const runtime::connection::ModuleInventory& responderInventory =
                service.lastRemoteInventoryFromResponder();
            if (snapshot.responder.hasLastRemoteInventory) {
                remoteInventory = responderInventory;
                result.ok = true;
                return ModuleInventoryWaitState::Complete;
            }
        }

        return ModuleInventoryWaitState::Pending;
    };

    ModuleInventoryWaitState state = complete();
    if (state != ModuleInventoryWaitState::Pending)
        return result;

    if (timeoutMs <= 0) {
        result.timedOut = true;
        return result;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        pollSessionTransports(transportManager, sessionId);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        service.expire(runtime::qt::QtTimerBridge::monotonicNowUsec());
        state = complete();
        if (state != ModuleInventoryWaitState::Pending)
            return result;
    }

    service.expire(runtime::qt::QtTimerBridge::monotonicNowUsec());
    state = complete();
    if (state != ModuleInventoryWaitState::Pending)
        return result;

    result.timedOut = true;
    return result;
}


} // namespace pc
} // namespace apps
} // namespace fusiondesk
