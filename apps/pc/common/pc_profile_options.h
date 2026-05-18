#ifndef FUSIONDESK_APPS_PC_COMMON_PC_PROFILE_OPTIONS_H
#define FUSIONDESK_APPS_PC_COMMON_PC_PROFILE_OPTIONS_H

#include <memory>
#include <string>
#include <vector>

#include "pc_app_shell.h"
#include "fusiondesk/core/protocol/protocol_types.h"
#include "fusiondesk/core/session/session_context.h"
#include "fusiondesk/core/session/session_manager.h"
#include "fusiondesk/modules/display/display_types.h"
#include "fusiondesk/runtime/display/display_capture_backend_selection.h"
#include "fusiondesk/runtime/display/display_codec_backend_factory.h"
#include "fusiondesk/runtime/display/display_codec_negotiation.h"
#include "fusiondesk/runtime/runtime_host.h"

namespace fusiondesk {
namespace apps {
namespace pc {

modules::display::DisplayScaleMode displayScaleModeOptionValue(int argc,
                                                               char** argv);

std::string displayCaptureBackendOptionValue(int argc, char** argv);

bool displayCapturePlanDiagnosticsRequested(int argc, char** argv);

bool displaySourceCatalogDiagnosticsRequested(int argc, char** argv);

bool displayCodecPlanDiagnosticsRequested(int argc, char** argv);

bool displayCodecLocalNegotiationRequested(int argc, char** argv);

bool displayCodecFdppNegotiationRequested(int argc, char** argv);

bool displayIncludeCursorOptionValue(int argc, char** argv);

runtime::display::DisplayPlatformFamily displayTargetPlatformOptionValue(
    int argc,
    char** argv);

runtime::display::DisplayCaptureSourceType displayCaptureSourceTypeOptionValue(
    int argc,
    char** argv);

runtime::display::DisplayTargetArchitecture displayTargetArchitectureOptionValue(
    int argc,
    char** argv);

runtime::display::DisplayTargetSocProfile displayTargetSocProfileOptionValue(
    int argc,
    char** argv);

runtime::ProductDisplayCodecPolicy effectiveDisplayCodecPolicy(
    runtime::ProductDisplayCodecPolicy policy);

runtime::display::DisplayCodecSelectionRequest makeDisplayCodecSelectionRequest(
    int argc,
    char** argv,
    runtime::display::DisplayCodecDirection direction,
    const runtime::ProductDisplayCodecPolicy& policy);

runtime::display::DisplayCodecNegotiationRequest
makeDisplayCodecNegotiationRequest(
    int argc,
    char** argv,
    const runtime::ProductDisplayCodecPolicy& policy,
    std::vector<runtime::display::DisplayCodecCapability> candidates);

std::shared_ptr<runtime::display::IDisplayCodecBackendFactory>
makeDisplayCodecBackendFactory(
    runtime::display::DisplayPlatformFamily targetPlatform,
    const runtime::ProductDisplayCodecPolicy& policy);

protocol::SessionId sessionIdOptionValue(int argc,
                                         char** argv,
                                         const std::string& name,
                                         protocol::SessionId fallback);

runtime::RuntimeOptions makeRuntimeOptions(int argc, char** argv);

struct RuntimeOptionsBuildResult
{
    bool ok = true;
    runtime::RuntimeOptions options;
    std::vector<std::string> messages;
};

RuntimeOptionsBuildResult makeRuntimeOptionsResult(int argc, char** argv);

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host,
                                                 PcShellRole role,
                                                 int argc,
                                                 char** argv);

session::SessionRole sessionRoleFor(PcShellRole role);

const char* applicationName(PcShellRole role);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_PROFILE_OPTIONS_H
