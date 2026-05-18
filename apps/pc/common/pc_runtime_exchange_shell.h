#ifndef FUSIONDESK_APPS_PC_COMMON_PC_RUNTIME_EXCHANGE_SHELL_H
#define FUSIONDESK_APPS_PC_COMMON_PC_RUNTIME_EXCHANGE_SHELL_H

#include <string>
#include <vector>

#include "pc_app_shell.h"
#include "pc_profile_dependencies.h"
#include "fusiondesk/core/network/channel.h"
#include "fusiondesk/core/policy/policy_types.h"
#include "fusiondesk/core/protocol/protocol_types.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/runtime/connection/module_inventory_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"
#include "fusiondesk/runtime/runtime_host.h"
#include "fusiondesk/runtime/session/link_channel_binding_report.h"

namespace fusiondesk {
namespace runtime {
namespace feature {
class ClipboardRuntimeService;
class FeatureRuntimeService;
} // namespace feature
} // namespace runtime

namespace apps {
namespace pc {

void writeLinkChannelBindingReport(
    const runtime::LinkChannelBindingReport& report);

const char* denyReasonName(policy::DenyReason value);

void pollSessionTransports(runtime::qt::QtRuntimeTransportManager& transportManager,
                           protocol::SessionId sessionId);

void runBoundedEventLoop(
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    int durationMs,
    runtime::feature::FeatureRuntimeService* featureService = nullptr,
    runtime::feature::ClipboardRuntimeService* clipboardService = nullptr);

bool waitForRequiredModuleChannels(
    session::Session& session,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    int timeoutMs,
    const std::vector<network::ChannelKey>& listeningChannels);

bool runClientPeerProfileExchange(
    int argc,
    char** argv,
    protocol::SessionId sessionId,
    const std::vector<network::ChannelSpec>& specs,
    const runtime::ProductDisplayCodecPolicy& policy,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    runtime::qt::QtPeerProfileRuntimeService& peerProfileService,
    DisplayCodecPeerNegotiationState* peerCodecNegotiation);

bool waitForAgentDisplayCodecPeerProfileNegotiation(
    int argc,
    char** argv,
    protocol::SessionId sessionId,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    const DisplayCodecPeerNegotiationState& peerCodecNegotiation);

runtime::connection::ModuleInventory makeLocalModuleInventory(
    protocol::SessionId sessionId,
    session::Session& session);

void writeModuleInventoryDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::connection::ModuleInventoryRuntimeService* service,
    const char* phase);

struct ModuleInventoryWaitResult
{
    bool ok = false;
    bool terminalFailure = false;
    bool timedOut = false;
    std::vector<std::string> messages;
};

ModuleInventoryWaitResult waitForModuleInventory(
    runtime::connection::ModuleInventoryRuntimeService& service,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    bool waitForResponseCompletion,
    bool waitForResponderRemote,
    int timeoutMs,
    runtime::connection::ModuleInventory& remoteInventory);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_RUNTIME_EXCHANGE_SHELL_H
