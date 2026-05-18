#ifndef FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_SOURCE_SELECTION_H
#define FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_SOURCE_SELECTION_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/modules/display/display_types.h"

namespace fusiondesk {
namespace runtime {
namespace display {

using DisplayCaptureSourceType =
    modules::display::DisplayCaptureSourceType;

struct DisplaySourceSelectionRequest
{
    DisplayCaptureSourceType sourceType = DisplayCaptureSourceType::Monitor;
    std::uint32_t sourceId = 0;
    std::uint64_t nativeSourceHandle = 0;
};

struct DisplaySourceSelectionResult
{
    bool ok = false;
    bool hasSource = false;
    modules::display::DisplaySourceInfo source;
    std::size_t sourceIndex = 0;
    std::size_t candidateCount = 0;
    std::vector<std::string> messages;
};

bool displaySourceMatchesSelection(
    const modules::display::DisplaySourceInfo& source,
    const modules::display::DisplaySourceInfo& selected);

DisplaySourceSelectionResult selectDisplaySource(
    const modules::display::DisplayTopologySnapshot& topology,
    const DisplaySourceSelectionRequest& request);

} // namespace display
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_DISPLAY_DISPLAY_SOURCE_SELECTION_H
