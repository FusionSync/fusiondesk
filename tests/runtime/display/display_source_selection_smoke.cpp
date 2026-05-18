#include <cassert>
#include <string>
#include <utility>

#include "fusiondesk/runtime/display/display_source_selection.h"

using namespace fusiondesk;

namespace {

modules::display::DisplaySourceInfo source(
    std::uint32_t sourceId,
    modules::display::DisplayCaptureSourceType sourceType,
    std::uint64_t nativeHandle,
    std::string name)
{
    modules::display::DisplaySourceInfo info;
    info.sourceId = sourceId;
    info.sourceType = sourceType;
    info.nativeSourceHandle = nativeHandle;
    info.name = std::move(name);
    info.geometry.width = 100 + sourceId;
    info.geometry.height = 80 + sourceId;
    return info;
}

modules::display::DisplayTopologySnapshot topology()
{
    modules::display::DisplayTopologySnapshot snapshot;
    snapshot.generation = 42;
    snapshot.sources.push_back(
        source(0, modules::display::DisplayCaptureSourceType::Monitor, 1000, "monitor-0"));
    snapshot.sources.push_back(
        source(1, modules::display::DisplayCaptureSourceType::Monitor, 1001, "monitor-1"));
    snapshot.sources.push_back(
        source(0, modules::display::DisplayCaptureSourceType::Window, 2000, "window-0"));
    snapshot.sources.push_back(
        source(1, modules::display::DisplayCaptureSourceType::Window, 2001, "window-1"));
    return snapshot;
}

void sourceIdIsScopedByType()
{
    runtime::display::DisplaySourceSelectionRequest request;
    request.sourceType = modules::display::DisplayCaptureSourceType::Window;
    request.sourceId = 0;

    const runtime::display::DisplaySourceSelectionResult result =
        runtime::display::selectDisplaySource(topology(), request);

    assert(result.ok);
    assert(result.hasSource);
    assert(result.sourceIndex == 2);
    assert(result.candidateCount == 2);
    assert(result.source.sourceType ==
           modules::display::DisplayCaptureSourceType::Window);
    assert(result.source.sourceId == 0);
    assert(result.source.nativeSourceHandle == 2000);
}

void nativeHandleTakesPrecedenceOverSourceId()
{
    runtime::display::DisplaySourceSelectionRequest request;
    request.sourceType = modules::display::DisplayCaptureSourceType::Window;
    request.sourceId = 0;
    request.nativeSourceHandle = 2001;

    const runtime::display::DisplaySourceSelectionResult result =
        runtime::display::selectDisplaySource(topology(), request);

    assert(result.ok);
    assert(result.hasSource);
    assert(result.sourceIndex == 3);
    assert(result.candidateCount == 2);
    assert(result.source.sourceId == 1);
    assert(result.source.nativeSourceHandle == 2001);
}

void missingIdReportsCandidates()
{
    runtime::display::DisplaySourceSelectionRequest request;
    request.sourceType = modules::display::DisplayCaptureSourceType::Monitor;
    request.sourceId = 99;

    const runtime::display::DisplaySourceSelectionResult result =
        runtime::display::selectDisplaySource(topology(), request);

    assert(!result.ok);
    assert(!result.hasSource);
    assert(result.candidateCount == 2);
    assert(result.messages.size() == 1);
    assert(result.messages.front().find("source id") != std::string::npos);
}

void missingNativeHandleReportsCandidates()
{
    runtime::display::DisplaySourceSelectionRequest request;
    request.sourceType = modules::display::DisplayCaptureSourceType::Window;
    request.nativeSourceHandle = 9999;

    const runtime::display::DisplaySourceSelectionResult result =
        runtime::display::selectDisplaySource(topology(), request);

    assert(!result.ok);
    assert(!result.hasSource);
    assert(result.candidateCount == 2);
    assert(result.messages.size() == 1);
    assert(result.messages.front().find("native handle") != std::string::npos);
}

void missingTypeAndEmptyTopologyAreDiagnosed()
{
    runtime::display::DisplaySourceSelectionRequest request;
    request.sourceType = modules::display::DisplayCaptureSourceType::VirtualDisplay;

    const runtime::display::DisplaySourceSelectionResult noType =
        runtime::display::selectDisplaySource(topology(), request);
    assert(!noType.ok);
    assert(noType.candidateCount == 0);
    assert(noType.messages.size() == 1);

    modules::display::DisplayTopologySnapshot empty;
    request.sourceType = modules::display::DisplayCaptureSourceType::Monitor;
    const runtime::display::DisplaySourceSelectionResult noSources =
        runtime::display::selectDisplaySource(empty, request);
    assert(!noSources.ok);
    assert(noSources.candidateCount == 0);
    assert(noSources.messages.size() == 1);
}

void rowMatchingUsesNativeHandleWhenPresent()
{
    modules::display::DisplaySourceInfo selected =
        source(0, modules::display::DisplayCaptureSourceType::Window, 2000, "selected");
    modules::display::DisplaySourceInfo sameHandle =
        source(9, modules::display::DisplayCaptureSourceType::Window, 2000, "same");
    modules::display::DisplaySourceInfo differentHandle =
        source(0, modules::display::DisplayCaptureSourceType::Window, 9999, "different");
    modules::display::DisplaySourceInfo noNativeSelected =
        source(7, modules::display::DisplayCaptureSourceType::Monitor, 0, "monitor");
    modules::display::DisplaySourceInfo noNativeSame =
        source(7, modules::display::DisplayCaptureSourceType::Monitor, 0, "monitor");

    assert(runtime::display::displaySourceMatchesSelection(sameHandle, selected));
    assert(!runtime::display::displaySourceMatchesSelection(differentHandle, selected));
    assert(runtime::display::displaySourceMatchesSelection(noNativeSame,
                                                           noNativeSelected));
}

} // namespace

int main()
{
    sourceIdIsScopedByType();
    nativeHandleTakesPrecedenceOverSourceId();
    missingIdReportsCandidates();
    missingNativeHandleReportsCandidates();
    missingTypeAndEmptyTopologyAreDiagnosed();
    rowMatchingUsesNativeHandleWhenPresent();
    return 0;
}
