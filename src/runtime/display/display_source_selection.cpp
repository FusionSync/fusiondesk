#include "fusiondesk/runtime/display/display_source_selection.h"

namespace fusiondesk {
namespace runtime {
namespace display {

namespace {

bool sameSourceType(const modules::display::DisplaySourceInfo& source,
                    DisplayCaptureSourceType sourceType)
{
    return source.sourceType == sourceType;
}

} // namespace

bool displaySourceMatchesSelection(
    const modules::display::DisplaySourceInfo& source,
    const modules::display::DisplaySourceInfo& selected)
{
    if (source.sourceType != selected.sourceType)
        return false;
    if (source.nativeSourceHandle != 0 || selected.nativeSourceHandle != 0)
        return source.nativeSourceHandle == selected.nativeSourceHandle;
    return source.sourceId == selected.sourceId;
}

DisplaySourceSelectionResult selectDisplaySource(
    const modules::display::DisplayTopologySnapshot& topology,
    const DisplaySourceSelectionRequest& request)
{
    DisplaySourceSelectionResult result;

    if (request.sourceType == DisplayCaptureSourceType::Unknown) {
        result.messages.push_back("display source selection requires a source type");
        return result;
    }

    if (topology.sources.empty()) {
        result.messages.push_back("display source selection has no sources");
        return result;
    }

    bool foundSource = false;
    modules::display::DisplaySourceInfo selectedSource;
    std::size_t selectedSourceIndex = 0;

    for (std::size_t index = 0; index < topology.sources.size(); ++index) {
        const modules::display::DisplaySourceInfo& source =
            topology.sources[index];
        if (!sameSourceType(source, request.sourceType))
            continue;

        ++result.candidateCount;
        bool sourceMatches = false;
        if (request.nativeSourceHandle != 0) {
            sourceMatches =
                source.nativeSourceHandle == request.nativeSourceHandle;
        } else if (source.sourceId == request.sourceId) {
            sourceMatches = true;
        }

        if (!foundSource && sourceMatches) {
            foundSource = true;
            selectedSource = source;
            selectedSourceIndex = index;
        }
    }

    if (foundSource) {
        result.ok = true;
        result.hasSource = true;
        result.source = selectedSource;
        result.sourceIndex = selectedSourceIndex;
        return result;
    }

    if (result.candidateCount == 0) {
        result.messages.push_back(
            "display source selection found no sources of the requested type");
    } else if (request.nativeSourceHandle != 0) {
        result.messages.push_back(
            "display source native handle was not found");
    } else {
        result.messages.push_back("display source id was not found");
    }
    return result;
}

} // namespace display
} // namespace runtime
} // namespace fusiondesk
