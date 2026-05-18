#include "pc_profile_options.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "pc_clipboard_shell.h"
#include "pc_clipboard_policy_file.h"
#include "pc_shell_options.h"

#include "fusiondesk/core/network/channel_defaults.h"
#include "fusiondesk/runtime/display/display_codec_selection.h"

#if defined(FUSIONDESK_PC_HAS_WINDOWS_DISPLAY_CAPTURE)
#include "fusiondesk/platform/windows/display/windows_media_foundation_display_codec.h"
#endif

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

protocol::NegotiatedCapabilities makeNegotiated()
{
    protocol::NegotiatedCapabilities capabilities;
    capabilities.channelTypes = {protocol::ChannelType::Control,
                                 protocol::ChannelType::Standard,
                                 protocol::ChannelType::Video};
    capabilities.packetTypes = {protocol::PacketType::ChannelInit,
                                protocol::PacketType::Heartbeat,
                                protocol::PacketType::Exchange,
                                protocol::PacketType::PayloadAck,
                                protocol::PacketType::Video,
                                protocol::PacketType::Mouse,
                                protocol::PacketType::Keyboard,
                                protocol::PacketType::Clipboard,
                                protocol::PacketType::Filesystem,
                                protocol::PacketType::FilesystemControl,
                                protocol::PacketType::FilesystemIrp,
                                protocol::PacketType::Printer};
    capabilities.messageKinds = {protocol::MessageKind::Request,
                                 protocol::MessageKind::Response,
                                 protocol::MessageKind::Event,
                                 protocol::MessageKind::Ack,
                                 protocol::MessageKind::Error};
    return capabilities;
}

std::string defaultRawCodecAdapterId(
    runtime::display::DisplayPlatformFamily platform)
{
    switch (platform) {
    case runtime::display::DisplayPlatformFamily::WindowsDesktop:
        return "windows.raw_frame";
    case runtime::display::DisplayPlatformFamily::LinuxX11:
    case runtime::display::DisplayPlatformFamily::LinuxWayland:
    case runtime::display::DisplayPlatformFamily::LinuxEmbedded:
        return "linux.raw_frame";
    case runtime::display::DisplayPlatformFamily::MacOS:
        return "macos.raw_frame";
    case runtime::display::DisplayPlatformFamily::AndroidClient:
    case runtime::display::DisplayPlatformFamily::AndroidAgent:
        return "android.raw_frame";
    case runtime::display::DisplayPlatformFamily::RockchipLinux:
        return "linux.rockchip.raw_frame";
    case runtime::display::DisplayPlatformFamily::RockchipAndroid:
        return "android.rockchip.raw_frame";
    case runtime::display::DisplayPlatformFamily::HarmonyOS:
        return "harmonyos.raw_frame";
    case runtime::display::DisplayPlatformFamily::OpenHarmony:
        return "openharmony.raw_frame";
    case runtime::display::DisplayPlatformFamily::Unknown:
        break;
    }
    return "raw_frame";
}

std::string displayCodecBackendOptionValue(
    int argc,
    char** argv,
    runtime::display::DisplayPlatformFamily platform)
{
    const std::string value = optionValue(argc, argv, "--display-codec-backend");
    if (value.empty() || value == "auto")
        return {};
    if (value == "raw" || value == "raw_frame" || value == "raw-bgra")
        return defaultRawCodecAdapterId(platform);
    if (value == "mf-h264" || value == "mediafoundation-h264" ||
        value == "windows.media_foundation.h264")
        return "windows.media_foundation.h264";
    if (value == "mf-h265" || value == "mediafoundation-h265" ||
        value == "windows.media_foundation.h265")
        return "windows.media_foundation.h265";
    return value;
}

std::vector<runtime::display::DisplayCodecId> displayCodecPreferenceOptionValue(
    int argc,
    char** argv,
    const runtime::ProductDisplayCodecPolicy& policy)
{
    const std::string option = optionValue(argc, argv, "--display-codec");
    if (option.empty())
        return policy.codecPreference.empty()
                   ? std::vector<runtime::display::DisplayCodecId>{
                         runtime::display::DisplayCodecId::RawBgra}
                   : policy.codecPreference;

    const runtime::display::DisplayCodecId parsed =
        runtime::display::parseDisplayCodecId(option);
    if (parsed == runtime::display::DisplayCodecId::RawBgra)
        return {runtime::display::DisplayCodecId::RawBgra};
    if (parsed == runtime::display::DisplayCodecId::H264)
        return {runtime::display::DisplayCodecId::H264,
                runtime::display::DisplayCodecId::RawBgra};
    if (parsed == runtime::display::DisplayCodecId::H265)
        return {runtime::display::DisplayCodecId::H265,
                runtime::display::DisplayCodecId::H264,
                runtime::display::DisplayCodecId::RawBgra};
    if (parsed == runtime::display::DisplayCodecId::Av1)
        return {runtime::display::DisplayCodecId::Av1,
                runtime::display::DisplayCodecId::H264,
                runtime::display::DisplayCodecId::RawBgra};
    return {runtime::display::DisplayCodecId::H264,
            runtime::display::DisplayCodecId::H265,
            runtime::display::DisplayCodecId::RawBgra};
}

network::ChannelKey largeDataKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::LargeData),
        protocol::ChannelType::Standard};
}

bool containsChannelSpec(const std::vector<network::ChannelSpec>& specs,
                         const network::ChannelKey& key)
{
    for (const network::ChannelSpec& spec : specs) {
        if (spec.key == key)
            return true;
    }
    return false;
}

bool profileModuleNeedsLargeData(const std::string& moduleId)
{
    return startsWith(moduleId, "clipboard.redirect") ||
           startsWith(moduleId, "filesystem.redirect") ||
           startsWith(moduleId, "printer.redirect");
}

protocol::FeatureMask featureMaskForProfileModule(const std::string& moduleId)
{
    if (startsWith(moduleId, "display.screen"))
        return protocol::feature::Display | protocol::feature::Screen;
    if (startsWith(moduleId, "input.mouse"))
        return protocol::feature::Mouse;
    if (startsWith(moduleId, "input.keyboard"))
        return protocol::feature::Keyboard;
    if (startsWith(moduleId, "input.touch"))
        return protocol::feature::Touch;
    if (startsWith(moduleId, "input.gamepad"))
        return protocol::feature::Gamepad;
    if (startsWith(moduleId, "clipboard.redirect"))
        return protocol::feature::Clipboard;
    if (startsWith(moduleId, "filesystem.redirect"))
        return protocol::feature::Filesystem;
    if (startsWith(moduleId, "printer.redirect"))
        return protocol::feature::Printer;
    if (startsWith(moduleId, "audio.desktop"))
        return protocol::feature::Audio;
    if (startsWith(moduleId, "audio.microphone"))
        return protocol::feature::Microphone;
    if (startsWith(moduleId, "camera.redirect"))
        return protocol::feature::Camera;
    if (startsWith(moduleId, "peripheral.usb"))
        return protocol::feature::PeripheralUsb;
    return 0;
}

void appendProfileModule(runtime::ProductProfile& profile,
                         const std::string& moduleId)
{
    if (moduleId.empty())
        return;

    for (const std::string& existing : profile.requiredModules) {
        if (existing == moduleId)
            return;
    }

    profile.requiredModules.push_back(moduleId);
    profile.defaultFeatures.bits |= featureMaskForProfileModule(moduleId);
    if (profileModuleNeedsLargeData(moduleId) &&
        !containsChannelSpec(profile.minimumChannels, largeDataKey())) {
        profile.minimumChannels.push_back(network::defaultLargeDataChannelSpec());
    }
}

void applyDisplayCodecPolicyOption(runtime::ProductProfile& profile,
                                   int argc,
                                   char** argv)
{
    const std::string value = optionValue(argc, argv, "--display-codec-policy");
    if (value.empty() || value == "default")
        return;

    if (value == "raw" || value == "raw-stable") {
        profile.displayCodecPolicy.codecPreference = {
            runtime::display::DisplayCodecId::RawBgra};
        profile.displayCodecPolicy.enableWindowsMediaFoundationH264 = false;
        profile.displayCodecPolicy.selectWindowsMediaFoundationH264 = false;
        profile.displayCodecPolicy.enableH264DeltaFrames = false;
        profile.displayCodecPolicy.selectionMode = "default";
        return;
    }

    if (value == "windows-h264-production" ||
        value == "h264-production" ||
        value == "mf-h264-production") {
        profile.displayCodecPolicy.codecPreference = {
            runtime::display::DisplayCodecId::H264,
            runtime::display::DisplayCodecId::RawBgra};
        profile.displayCodecPolicy.enableWindowsMediaFoundationH264 = true;
        profile.displayCodecPolicy.selectWindowsMediaFoundationH264 = true;
        profile.displayCodecPolicy.enableH264DeltaFrames = true;
        profile.displayCodecPolicy.selectionMode = "production";
        return;
    }

    if (value == "windows-h264-validation" ||
        value == "h264-validation" ||
        value == "mf-h264-validation") {
        profile.displayCodecPolicy.codecPreference = {
            runtime::display::DisplayCodecId::H264,
            runtime::display::DisplayCodecId::RawBgra};
        profile.displayCodecPolicy.enableWindowsMediaFoundationH264 = true;
        profile.displayCodecPolicy.selectWindowsMediaFoundationH264 = true;
        profile.displayCodecPolicy.enableH264DeltaFrames =
            envFlagEnabled("FUSIONDESK_MF_H264_PFRAME");
        profile.displayCodecPolicy.selectionMode = "validation";
        return;
    }
}

bool profileModulesRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--mount-profile-modules") ||
           hasArg(argc, argv, "--start-profile-modules") ||
           hasArg(argc, argv, "--mount-display") ||
           hasArg(argc, argv, "--start-display") ||
           hasArg(argc, argv, "--show-display-window") ||
           hasArg(argc, argv, "--mount-input") ||
           clipboardProfileRequested(argc, argv) ||
           hasArg(argc, argv, "--pump-profile-modules") ||
           hasArg(argc, argv, "--module-inventory-service") ||
           hasArg(argc, argv, "--module-inventory-request") ||
           hasArg(argc, argv, "--send-test-echo") ||
           !optionValues(argc, argv, "--profile-module").empty();
}

} // namespace

modules::display::DisplayScaleMode displayScaleModeOptionValue(int argc,
                                                               char** argv)
{
    const std::string value = optionValue(argc, argv, "--display-scale-mode");
    if (value == "source")
        return modules::display::DisplayScaleMode::Source;
    if (value == "stretch")
        return modules::display::DisplayScaleMode::Stretch;
    return modules::display::DisplayScaleMode::Fit;
}

std::string displayCaptureBackendOptionValue(int argc, char** argv)
{
    const std::string value = optionValue(argc, argv, "--display-capture-backend");
    if (value.empty() || value == "auto")
        return {};
    if (value == "gdi" || value == "windows.gdi")
        return "windows.gdi";
    if (value == "dxgi" || value == "desktop_duplication" ||
        value == "windows.dxgi" ||
        value == "windows.dxgi.desktop_duplication")
        return "windows.dxgi.desktop_duplication";
    if (value == "wgc" || value == "windows.graphics_capture")
        return "windows.graphics_capture";
    return value;
}

bool displayCapturePlanDiagnosticsRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--print-display-capture-plan");
}

bool displaySourceCatalogDiagnosticsRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--print-display-sources");
}

bool displayCodecPlanDiagnosticsRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--print-display-codec-plan");
}

bool displayCodecLocalNegotiationRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--display-codec-negotiate-local");
}

bool displayCodecFdppNegotiationRequested(int argc, char** argv)
{
    return hasArg(argc, argv, "--display-codec-negotiate-fdpp");
}

bool displayIncludeCursorOptionValue(int argc, char** argv)
{
    return !hasArg(argc, argv, "--display-no-cursor");
}

runtime::display::DisplayPlatformFamily displayTargetPlatformOptionValue(
    int argc,
    char** argv)
{
    const runtime::display::DisplayPlatformFamily parsed =
        runtime::display::parseDisplayPlatformFamily(
            optionValue(argc, argv, "--display-target-platform"));
    return parsed == runtime::display::DisplayPlatformFamily::Unknown
               ? runtime::display::DisplayPlatformFamily::WindowsDesktop
               : parsed;
}

runtime::display::DisplayCaptureSourceType displayCaptureSourceTypeOptionValue(
    int argc,
    char** argv)
{
    const runtime::display::DisplayCaptureSourceType parsed =
        runtime::display::parseDisplayCaptureSourceType(
            optionValue(argc, argv, "--display-source-type"));
    return parsed == runtime::display::DisplayCaptureSourceType::Unknown
               ? runtime::display::DisplayCaptureSourceType::Monitor
               : parsed;
}

runtime::display::DisplayTargetArchitecture displayTargetArchitectureOptionValue(
    int argc,
    char** argv)
{
    return runtime::display::parseDisplayTargetArchitecture(
        optionValue(argc, argv, "--display-target-arch"));
}

runtime::display::DisplayTargetSocProfile displayTargetSocProfileOptionValue(
    int argc,
    char** argv)
{
    return runtime::display::parseDisplayTargetSocProfile(
        optionValue(argc, argv, "--display-target-soc"));
}

runtime::ProductDisplayCodecPolicy effectiveDisplayCodecPolicy(
    runtime::ProductDisplayCodecPolicy policy)
{
    if (envFlagEnabled("FUSIONDESK_ENABLE_MF_H264_PRODUCTION")) {
        policy.enableWindowsMediaFoundationH264 = true;
        policy.selectWindowsMediaFoundationH264 = true;
        policy.enableH264DeltaFrames = true;
        policy.selectionMode = "production";
    } else if (envFlagEnabled("FUSIONDESK_SELECT_MF_H264")) {
        policy.enableWindowsMediaFoundationH264 = true;
        policy.selectWindowsMediaFoundationH264 = true;
        policy.selectionMode = "validation";
    }
    if (envFlagEnabled("FUSIONDESK_MF_H264_PFRAME"))
        policy.enableH264DeltaFrames = true;
    if (policy.selectionMode.empty())
        policy.selectionMode = "default";
    return policy;
}

runtime::display::DisplayCodecSelectionRequest makeDisplayCodecSelectionRequest(
    int argc,
    char** argv,
    runtime::display::DisplayCodecDirection direction,
    const runtime::ProductDisplayCodecPolicy& policy)
{
    const runtime::ProductDisplayCodecPolicy effectivePolicy =
        effectiveDisplayCodecPolicy(policy);
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = displayTargetPlatformOptionValue(argc, argv);
    request.direction = direction;
    request.codecPreference =
        displayCodecPreferenceOptionValue(argc, argv, effectivePolicy);
    request.acceptedInputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.acceptedOutputMemoryTypes =
        runtime::display::defaultDisplayCodecAcceptedMemoryTypes(
            request.platform);
    request.acceptedPixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    request.architecture = displayTargetArchitectureOptionValue(argc, argv);
    request.socProfile = displayTargetSocProfileOptionValue(argc, argv);
    request.requestedAdapterId =
        displayCodecBackendOptionValue(argc, argv, request.platform);
    request.allowHardware = effectivePolicy.allowHardware &&
                            !hasArg(argc, argv, "--display-codec-no-hardware");
    request.allowSoftware = effectivePolicy.allowSoftware &&
                            !hasArg(argc, argv, "--display-codec-no-software");
    request.preferHardware = effectivePolicy.preferHardware &&
                             !hasArg(argc, argv,
                                     "--display-codec-prefer-software");
    request.preferZeroCopy = effectivePolicy.preferZeroCopy &&
                             !hasArg(argc, argv,
                                     "--display-codec-no-zerocopy");
    return request;
}

runtime::display::DisplayCodecNegotiationRequest
makeDisplayCodecNegotiationRequest(
    int argc,
    char** argv,
    const runtime::ProductDisplayCodecPolicy& policy,
    std::vector<runtime::display::DisplayCodecCapability> candidates)
{
    runtime::display::DisplayCodecNegotiationRequest request;
    request.encoderRequest = makeDisplayCodecSelectionRequest(
        argc,
        argv,
        runtime::display::DisplayCodecDirection::Encode,
        policy);
    request.decoderRequest = makeDisplayCodecSelectionRequest(
        argc,
        argv,
        runtime::display::DisplayCodecDirection::Decode,
        policy);
    request.encoderRequest.candidates = candidates;
    request.decoderRequest.candidates = std::move(candidates);
    return request;
}

std::shared_ptr<runtime::display::IDisplayCodecBackendFactory>
makeDisplayCodecBackendFactory(
    runtime::display::DisplayPlatformFamily targetPlatform,
    const runtime::ProductDisplayCodecPolicy& policy)
{
    auto registry =
        std::make_shared<runtime::display::DisplayCodecBackendFactoryRegistry>();
#if defined(FUSIONDESK_PC_HAS_WINDOWS_DISPLAY_CAPTURE)
    if (targetPlatform ==
        runtime::display::DisplayPlatformFamily::WindowsDesktop) {
        const runtime::ProductDisplayCodecPolicy effectivePolicy =
            effectiveDisplayCodecPolicy(policy);
        platform::windows::display::WindowsMediaFoundationDisplayCodecPolicy
            windowsPolicy;
        windowsPolicy.rolloutEnabled =
            effectivePolicy.enableWindowsMediaFoundationH264;
        windowsPolicy.selectable =
            effectivePolicy.selectWindowsMediaFoundationH264;
        windowsPolicy.pFrameEnabled = effectivePolicy.enableH264DeltaFrames;
        windowsPolicy.selectionMode = effectivePolicy.selectionMode;
        registry->addFactory(
            std::make_shared<
                platform::windows::display::
                    WindowsMediaFoundationDisplayCodecBackendFactory>(
                windowsPolicy));
    }
#endif
    registry->addFactory(
        runtime::display::createRawFrameDisplayCodecBackendFactory(targetPlatform));
    return registry;
}

protocol::SessionId sessionIdOptionValue(int argc,
                                         char** argv,
                                         const std::string& name,
                                         protocol::SessionId fallback)
{
    const std::string value = optionValue(argc, argv, name);
    if (value.empty())
        return fallback;

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed == 0)
        return fallback;

    return static_cast<protocol::SessionId>(parsed);
}

RuntimeOptionsBuildResult makeRuntimeOptionsResult(int argc, char** argv)
{
    RuntimeOptionsBuildResult result;
    runtime::RuntimeOptions& options = result.options;
    if (!profileModulesRequested(argc, argv))
        return result;

    options.profile.profileId = "pc-shell-selected-profile";
    options.profile.minimumChannels = network::defaultMvpChannelSpecs();
    applyDisplayCodecPolicyOption(options.profile, argc, argv);

    if (hasArg(argc, argv, "--mount-display") ||
        hasArg(argc, argv, "--start-display") ||
        hasArg(argc, argv, "--show-display-window")) {
        appendProfileModule(options.profile, "display.screen");
    }

    if (hasArg(argc, argv, "--mount-input")) {
        appendProfileModule(options.profile, "input.mouse");
        appendProfileModule(options.profile, "input.keyboard");
    }

    if (clipboardProfileRequested(argc, argv)) {
        const std::string policyPath =
            optionValue(argc, argv, "--clipboard-policy-file");
        if (!policyPath.empty()) {
            const PcClipboardPolicyFileLoadResult loaded =
                loadClipboardProductPolicyFromJsonFile(
                    policyPath,
                    options.profile.clipboardPolicy);
            if (!loaded.ok) {
                result.ok = false;
                result.messages = loaded.messages;
                return result;
            }
            options.profile.clipboardPolicy = loaded.policy;
        }
        options.profile.clipboardPolicy =
            productClipboardPolicyOptionValue(argc,
                                              argv,
                                              options.profile.clipboardPolicy);
        appendProfileModule(options.profile, "clipboard.redirect");
    }

    if (hasArg(argc, argv, "--pump-profile-modules")) {
        appendProfileModule(options.profile, "input.mouse");
        appendProfileModule(options.profile, "input.keyboard");
    }

    if (hasArg(argc, argv, "--send-test-echo"))
        appendProfileModule(options.profile, "test.echo");

    for (const std::string& moduleId :
         optionValues(argc, argv, "--profile-module")) {
        appendProfileModule(options.profile, moduleId);
    }

    if (options.profile.requiredModules.empty())
        appendProfileModule(options.profile, "display.screen");

    return result;
}

runtime::RuntimeOptions makeRuntimeOptions(int argc, char** argv)
{
    RuntimeOptionsBuildResult result = makeRuntimeOptionsResult(argc, argv);
    if (!result.ok)
        writeShellMessages(result.messages);
    return result.options;
}

session::SessionCreateOptions makeSessionOptions(const runtime::RuntimeHost& host,
                                                 PcShellRole role,
                                                 int argc,
                                                 char** argv)
{
    session::SessionCreateOptions options;
    options.context.sessionId =
        sessionIdOptionValue(argc, argv, "--session-id", 0);
    options.context.userId =
        role == PcShellRole::Client ? "pc-client-user" : "pc-agent-user";
    options.context.tenantId = "pc-shell";
    options.context.deviceId =
        role == PcShellRole::Client ? "pc-client-device" : "pc-agent-device";
    options.context.clientDeviceId = "pc-client-device";
    options.context.agentDeviceId = "pc-agent-device";
    options.context.localPlatform = "windows";
    options.context.remotePlatform = "windows";
    options.context.requestedFeatures = host.profile().defaultFeatures;
    options.context.licensedFeatures = host.profile().defaultFeatures;
    options.context.policyFeatures = host.profile().defaultFeatures;
    options.context.negotiatedCapabilities = makeNegotiated();
    options.minimumChannels = host.profile().minimumChannels;
    return options;
}

session::SessionRole sessionRoleFor(PcShellRole role)
{
    return role == PcShellRole::Client ?
           session::SessionRole::Client :
           session::SessionRole::Agent;
}

const char* applicationName(PcShellRole role)
{
    return role == PcShellRole::Client ? "fusiondesk-pc-client"
                                       : "fusiondesk-pc-agent";
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
