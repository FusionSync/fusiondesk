#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "pc_app_shell.h"

namespace {

struct ShellRunResult
{
    int exitCode = 0;
    std::string output;
};

ShellRunResult runAgentShell(std::vector<std::string> args)
{
    namespace pc = fusiondesk::apps::pc;

    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    int argc = static_cast<int>(argv.size());
    std::ostringstream stdoutCapture;
    std::streambuf* oldStdout = std::cout.rdbuf(stdoutCapture.rdbuf());
    const int result = pc::runPcShell(argc, argv.data(), pc::PcShellRole::Agent);
    std::cout.rdbuf(oldStdout);

    ShellRunResult shellResult;
    shellResult.exitCode = result;
    shellResult.output = stdoutCapture.str();
    return shellResult;
}

void startDisplayRequiresReadyChannels()
{
    ShellRunResult result = runAgentShell({
        "fusiondesk_pc_agent_start_display_requires_ready_channel_smoke",
        "--smoke",
        "--print-display-capture-plan",
        "--print-display-codec-plan",
        "--print-display-sources",
        "--print-session-diagnostics",
        "--display-codec-negotiate-local",
        "--start-display",
        "--display-no-cursor",
        "--display-capture-backend",
        "gdi",
        "--display-target-platform",
        "windows-desktop",
        "--display-source-type",
        "monitor",
        "--display-target-arch",
        "x86_64",
        "--display-target-soc",
        "generic",
        "--wait-channels-ms",
        "0",
    });
    assert(result.exitCode == 8);
    const std::string& output = result.output;
    assert(output.find("display.capture.plan") != std::string::npos);
    assert(output.find("display.codec.plan") != std::string::npos);
    assert(output.find("display.codec.negotiation") != std::string::npos);
    assert(output.find("display.codec.negotiation.attempt") != std::string::npos);
    assert(output.find("selectedEncoder=windows.raw_frame") != std::string::npos);
    assert(output.find("selectedDecoder=windows.raw_frame") != std::string::npos);
    assert(output.find("direction=encode") != std::string::npos);
    assert(output.find("preference=raw_bgra") != std::string::npos);
    assert(output.find("selected=windows.raw_frame") != std::string::npos);
    assert(output.find("backend=raw_frame") != std::string::npos);
    assert(output.find("codec=raw_bgra") != std::string::npos);
    assert(output.find("fallback=1") != std::string::npos);
    assert(output.find("display.codec.plan.candidate") != std::string::npos);
    assert(output.find("display.codec.plan.rejection") != std::string::npos);
    assert(output.find("MediaFoundation H.264 codec adapter rollout is not enabled") !=
           std::string::npos);
    assert(output.find("capabilitySource=probed_factory") != std::string::npos);
    assert(output.find("role=agent") != std::string::npos);
    assert(output.find("platform=windows_desktop") != std::string::npos);
    assert(output.find("source=monitor") != std::string::npos);
    assert(output.find("includeCursor=0") != std::string::npos);
    assert(output.find("requested=windows.gdi") != std::string::npos);
    assert(output.find("arch=x86_64") != std::string::npos);
    assert(output.find("soc=generic") != std::string::npos);
    assert(output.find("selected=windows.gdi") != std::string::npos);
    assert(output.find("display.capture.plan.candidate") != std::string::npos);
    assert(output.find("display.source.catalog") != std::string::npos);
    assert(output.find("ok=1") != std::string::npos);
    assert(output.find("source=monitor") != std::string::npos);
    assert(output.find("requested=windows.gdi") != std::string::npos);
    assert(output.find("backend=windows.gdi") != std::string::npos);
    assert(output.find("provider=1") != std::string::npos);
    assert(output.find("requestedSourceId=0") != std::string::npos);
    assert(output.find("sourceMatched=1") != std::string::npos);
    assert(output.find("display.source.selection") != std::string::npos);
    assert(output.find("selectedId=0") != std::string::npos);
    assert(output.find("display.source.catalog.rejection") != std::string::npos);
    assert(output.find("sources=") != std::string::npos);
    assert(output.find("selected=1") != std::string::npos);
    assert(output.find("type=monitor") != std::string::npos);
    assert(output.find("nativeHandle=") != std::string::npos);
    assert(output.find("session.diagnostics") != std::string::npos);
    assert(output.find("phase=profile_start_blocked") != std::string::npos);
    assert(output.find("linkReady=0") != std::string::npos);
    assert(output.find("blocked=1") != std::string::npos);
    assert(output.find("mounted=1") != std::string::npos);
    assert(output.find("running=0") != std::string::npos);
    assert(output.find("session.diagnostics.display_capture") != std::string::npos);
    assert(output.find("backend=windows.gdi") != std::string::npos);
    assert(output.find("includeCursor=0") != std::string::npos);
    assert(output.find("capturedFrames=0") != std::string::npos);
    assert(output.find("sentFrames=0") != std::string::npos);
    assert(output.find("capturedPixelBytes=0") != std::string::npos);
    assert(output.find("encodedPayloadBytes=0") != std::string::npos);
    assert(output.find("sentPayloadBytes=0") != std::string::npos);
    assert(output.find("lastSentPayloadBytes=0") != std::string::npos);
    assert(output.find("captureErrors=0") != std::string::npos);
    assert(output.find("code=2") != std::string::npos);
    assert(output.find("status=NotOpen") != std::string::npos);
    assert(output.find("action=reopen_capture") != std::string::npos);
    assert(output.find("message=display capture is not open") != std::string::npos);
    assert(output.find("session.diagnostics.display_codec") != std::string::npos);
    assert(output.find("direction=encode") != std::string::npos);
    assert(output.find("adapter=windows.raw_frame") != std::string::npos);
    assert(output.find("codec=raw_bgra") != std::string::npos);
    assert(output.find("backend=raw_frame") != std::string::npos);
    assert(output.find("selectionMode=default") != std::string::npos);
    assert(output.find("fallbackReason=windows.media_foundation.h264") !=
           std::string::npos);
    assert(output.find("session.diagnostics.display_codec.message") !=
           std::string::npos);
    assert(output.find("session.diagnostics.display_health") !=
           std::string::npos);
    assert(output.find("usable=0") != std::string::npos);
    assert(output.find("health=blocked") != std::string::npos);
    assert(output.find("status=display.channel_blocked") != std::string::npos);
    assert(output.find("action=network.bind_required_channels") !=
           std::string::npos);
    assert(output.find("displayModules=1") != std::string::npos);
    assert(output.find("runningDisplayModules=0") != std::string::npos);
    assert(output.find("codecAdapter=windows.raw_frame") != std::string::npos);
    assert(output.find("codecFallback=1") != std::string::npos);
    assert(output.find("captureState=capture.not_open.windows.gdi") !=
           std::string::npos);
    assert(output.find("codecState=codec.selected.windows.raw_frame.default") !=
           std::string::npos);
    assert(output.find("codecFallbackWarning=1") != std::string::npos);
    assert(output.find("codecLatencyWarning=0") != std::string::npos);
}

void windowCaptureDoesNotFallbackToMonitorOnlyBackends()
{
    ShellRunResult result = runAgentShell({
        "fusiondesk_pc_agent_start_display_requires_ready_channel_smoke",
        "--smoke",
        "--print-display-capture-plan",
        "--print-display-sources",
        "--print-session-diagnostics",
        "--mount-display",
        "--display-source-type",
        "window",
    });
    assert(result.exitCode == 7);
    const std::string& output = result.output;
    assert(output.find("display.capture.plan") != std::string::npos);
    assert(output.find("capabilitySource=probed_factory") != std::string::npos);
    assert(output.find("source=window") != std::string::npos);
    assert(output.find("requested=auto") != std::string::npos);
    assert(output.find("selected=") != std::string::npos);
    assert(output.find("rejected=3") != std::string::npos);
    assert(output.find("windows.dxgi.desktop_duplication") != std::string::npos);
    assert(output.find("windows.graphics_capture") != std::string::npos);
    assert(output.find("windows.gdi") != std::string::npos);
    assert(output.find("capture backend does not support requested source type") !=
           std::string::npos);
    assert(output.find("Windows Graphics Capture adapter rollout is not enabled") !=
           std::string::npos);
    assert(output.find("display.source.catalog") != std::string::npos);
    assert(output.find("ok=0") != std::string::npos);
    assert(output.find("source=window") != std::string::npos);
    assert(output.find("requested=auto") != std::string::npos);
    assert(output.find("provider=0") != std::string::npos);
    assert(output.find("sourceMatched=0") != std::string::npos);
    assert(output.find("display.source.catalog.rejection") != std::string::npos);
    assert(output.find("display.source.catalog.message") != std::string::npos);
    assert(output.find("display.source.selection.message") != std::string::npos);
    assert(output.find("phase=profile_mount_failed") != std::string::npos);
}

} // namespace

int main()
{
    startDisplayRequiresReadyChannels();
    windowCaptureDoesNotFallbackToMonitorOnlyBackends();
    return 0;
}
