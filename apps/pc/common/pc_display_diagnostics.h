#ifndef FUSIONDESK_APPS_PC_COMMON_PC_DISPLAY_DIAGNOSTICS_H
#define FUSIONDESK_APPS_PC_COMMON_PC_DISPLAY_DIAGNOSTICS_H

#include <string>
#include <vector>

namespace fusiondesk {
namespace modules {
namespace display {
struct DisplayCaptureOpenOptions;
} // namespace display
} // namespace modules

namespace runtime {
namespace display {
class IDisplayCaptureBackendFactory;
struct DisplayCapturePlatformPlan;
struct DisplayCodecNegotiationResult;
struct DisplayCodecSelectionRequest;
struct DisplayCodecSelectionResult;
} // namespace display
} // namespace runtime

namespace apps {
namespace pc {

const char* boolValue(bool value);

void writeDisplayCapturePlanDiagnostics(
    const runtime::display::DisplayCapturePlatformPlan& plan,
    const char* phase,
    bool includeCursor);

void writeDisplayCodecPlanDiagnostics(
    const runtime::display::DisplayCodecSelectionRequest& request,
    const runtime::display::DisplayCodecSelectionResult& selection,
    const std::vector<std::string>& messages,
    const char* phase);

void writeDisplayCodecNegotiationDiagnostics(
    const runtime::display::DisplayCodecNegotiationResult& negotiation,
    const char* phase);

void writeDisplaySourceCatalogDiagnostics(
    const runtime::display::DisplayCapturePlatformPlan& plan,
    const char* phase,
    const runtime::display::IDisplayCaptureBackendFactory& factory,
    const modules::display::DisplayCaptureOpenOptions& options);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_DISPLAY_DIAGNOSTICS_H
