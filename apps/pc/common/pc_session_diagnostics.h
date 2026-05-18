#ifndef FUSIONDESK_APPS_PC_COMMON_PC_SESSION_DIAGNOSTICS_H
#define FUSIONDESK_APPS_PC_COMMON_PC_SESSION_DIAGNOSTICS_H

#include <vector>

#include "fusiondesk/core/network/channel.h"
#include "fusiondesk/core/protocol/protocol_types.h"

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace display {
class QtImageDisplayWindow;
} // namespace display
} // namespace qt
} // namespace adapters

namespace runtime {
class RuntimeHost;
struct LinkChannelBindingReportOptions;
namespace display {
class DisplayRuntimeService;
} // namespace display
namespace qt {
class QtReconnectRuntimeService;
} // namespace qt
} // namespace runtime

namespace apps {
namespace pc {

runtime::LinkChannelBindingReportOptions makeLinkReportOptions(
    const std::vector<network::ChannelKey>& listeningChannels);

void writeReconnectDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::qt::QtReconnectRuntimeService& service,
    const char* phase);

void writeSessionDiagnosticsIfRequested(
    int argc,
    char** argv,
    runtime::RuntimeHost& host,
    protocol::SessionId sessionId,
    const std::vector<network::ChannelKey>& listeningChannels,
    const char* phase);

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
void updateDisplayWindowHealth(
    adapters::qt::display::QtImageDisplayWindow* window,
    runtime::RuntimeHost& host,
    protocol::SessionId sessionId,
    const std::vector<network::ChannelKey>& listeningChannels);
#endif

void writeDisplayRuntimeDiagnosticsIfRequested(
    int argc,
    char** argv,
    const runtime::display::DisplayRuntimeService* service,
    const char* phase);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_SESSION_DIAGNOSTICS_H
