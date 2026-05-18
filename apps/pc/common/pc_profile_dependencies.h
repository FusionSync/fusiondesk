#ifndef FUSIONDESK_APPS_PC_COMMON_PC_PROFILE_DEPENDENCIES_H
#define FUSIONDESK_APPS_PC_COMMON_PC_PROFILE_DEPENDENCIES_H

#include <memory>
#include <string>
#include <vector>

#include "pc_app_shell.h"
#include "fusiondesk/core/protocol/packet_envelope.h"
#include "fusiondesk/modules/clipboard/clipboard_drag_coordinates.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/runtime/connection/peer_profile_runtime_service.h"
#include "fusiondesk/runtime/display/display_capture_backend_factory.h"
#include "fusiondesk/runtime/display/display_capture_backend_selection.h"
#include "fusiondesk/runtime/display/display_codec_negotiation.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_runtime_service.h"
#include "fusiondesk/runtime/runtime_host.h"

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace display {
class QtImageDisplayRenderer;
} // namespace display
} // namespace qt
} // namespace adapters

namespace apps {
namespace pc {

struct DisplayCaptureRuntimeContext
{
    std::shared_ptr<runtime::display::IDisplayCaptureBackendFactory> backendFactory;
    runtime::display::DisplayCaptureBackendSelectionRequest selectionRequest;
#if defined(FUSIONDESK_PC_HAS_QT_IMAGE_DISPLAY)
    std::shared_ptr<adapters::qt::display::QtImageDisplayRenderer> imageRenderer;
#endif
};

struct DisplayCodecPeerNegotiationState
{
    bool attempted = false;
    bool ok = false;
    runtime::display::DisplayCodecNegotiationResult negotiation;
    std::vector<std::string> messages;
};

bool appendClientDisplayCodecPeerProfileExtension(
    int argc,
    char** argv,
    const runtime::ProductDisplayCodecPolicy& policy,
    runtime::connection::PeerProfileExchangeRequest& request,
    std::vector<std::string>& messages);

runtime::connection::PeerProfileExchangeResult
handleAgentDisplayCodecPeerProfileExchange(
    int argc,
    char** argv,
    const runtime::ProductDisplayCodecPolicy& policy,
    const runtime::connection::PeerProfileExchangeRequest& request,
    const runtime::connection::PeerProfileExchangeResult& exchange,
    DisplayCodecPeerNegotiationState& state);

bool readClientDisplayCodecPeerProfileCompletion(
    int argc,
    char** argv,
    const runtime::qt::QtPeerProfileRuntimeService& peerProfileService,
    DisplayCodecPeerNegotiationState& state,
    std::vector<std::string>& messages);

runtime::DisplayMvpDependencies makeProfileDependencies(
    PcShellRole role,
    int argc,
    char** argv,
    const runtime::ProductProfile& profile,
    DisplayCaptureRuntimeContext* captureRuntime,
    const DisplayCodecPeerNegotiationState* peerCodecNegotiation,
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> clipboardRemoteReader,
    std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
        clipboardDragCoordinateMapper = {});

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_PROFILE_DEPENDENCIES_H
