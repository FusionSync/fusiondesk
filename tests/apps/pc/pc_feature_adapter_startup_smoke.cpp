#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "pc_app_shell.h"
#include "pc_clipboard_drag_mapper.h"
#include "pc_clipboard_policy_file.h"
#include "pc_clipboard_shell.h"
#include "pc_profile_options.h"

#if defined(_WIN32)
#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"
#endif
#include "fusiondesk/runtime/feature/clipboard_product_policy.h"

namespace {

std::filesystem::path uniqueTempPath(const std::string& name)
{
    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fusiondesk_" + name + "_" + std::to_string(now) + ".json");
}

void writeTextFile(const std::filesystem::path& path,
                   const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    file << text;
}

#if defined(_WIN32)
fusiondesk::protocol::ByteBuffer bytes(const std::string& value)
{
    return fusiondesk::protocol::ByteBuffer(value.begin(), value.end());
}

fusiondesk::modules::clipboard::TransferSourceBundle
textBundleForDragMapper()
{
    using namespace fusiondesk::modules::clipboard;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 5;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes("hello");

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            77,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

fusiondesk::modules::clipboard::DragSurfaceCoordinate
dragPointForMapper(std::int32_t x, std::int32_t y)
{
    fusiondesk::modules::clipboard::DragSurfaceCoordinate point;
    point.coordinateSpace =
        fusiondesk::modules::clipboard::DragCoordinateSpace::RemoteLogical;
    point.x = x;
    point.y = y;
    point.surfaceWidth = 1280;
    point.surfaceHeight = 720;
    point.scale = 1.0;
    return point;
}

void clipboardEndpointUsesConfiguredDragMapper()
{
    using namespace fusiondesk::modules::clipboard;
    namespace winclip = fusiondesk::platform::windows::clipboard;

    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-drag-viewport-x",
        "100",
        "--clipboard-drag-viewport-y",
        "200",
        "--clipboard-drag-viewport-width",
        "640",
        "--clipboard-drag-viewport-height",
        "360",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    int argc = static_cast<int>(argv.size());
    ClipboardPolicy policy =
        fusiondesk::apps::pc::clipboardPolicyOptionValue(argc, argv.data());
    std::shared_ptr<IClipboardEndpoint> endpointBase =
        fusiondesk::apps::pc::makeClipboardEndpoint(argc,
                                                     argv.data(),
                                                     policy,
                                                     {});
    auto endpoint =
        std::dynamic_pointer_cast<winclip::WindowsClipboardEndpoint>(
            endpointBase);
    assert(endpoint != nullptr);

    assert(endpoint->publishBundle(
               ClipboardPublishRequest{textBundleForDragMapper()}) ==
           fusiondesk::protocol::ResponseStatus::Ok);

    DragSessionStart start;
    start.dragSessionId = 9001;
    start.bundleId = 11;
    start.offerId = 22;
    start.ownerEpoch = 33;
    start.allowedActions = transfer_action::Copy;
    start.preferredAction = TransferAction::Copy;
    start.start = dragPointForMapper(640, 360);
    assert(endpoint->dragStart(start) ==
           fusiondesk::protocol::ResponseStatus::Ok);

    const winclip::WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint->diagnostics();
    assert(diagnostics.lastDragX == 420);
    assert(diagnostics.lastDragY == 380);

    assert(endpoint->dragCancel(9001, DragCancelReason::UserCancelled) ==
           fusiondesk::protocol::ResponseStatus::Ok);
}

void clipboardEndpointUsesDynamicDragMapper()
{
    using namespace fusiondesk::modules::clipboard;
    namespace winclip = fusiondesk::platform::windows::clipboard;

    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    int argc = static_cast<int>(argv.size());
    ClipboardPolicy policy =
        fusiondesk::apps::pc::clipboardPolicyOptionValue(argc, argv.data());
    auto mapper = fusiondesk::apps::pc::makeClipboardDragCoordinateMapper(
        argc,
        argv.data(),
        []() -> std::optional<DragCoordinateMapViewport> {
            DragCoordinateMapViewport viewport;
            viewport.originX = 50;
            viewport.originY = 60;
            viewport.width = 100;
            viewport.height = 200;
            viewport.outputSpace = DragCoordinateSpace::LocalLogical;
            return viewport;
        });
    std::shared_ptr<IClipboardEndpoint> endpointBase =
        fusiondesk::apps::pc::makeClipboardEndpoint(argc,
                                                     argv.data(),
                                                     policy,
                                                     {},
                                                     mapper);
    auto endpoint =
        std::dynamic_pointer_cast<winclip::WindowsClipboardEndpoint>(
            endpointBase);
    assert(endpoint != nullptr);

    assert(endpoint->publishBundle(
               ClipboardPublishRequest{textBundleForDragMapper()}) ==
           fusiondesk::protocol::ResponseStatus::Ok);

    DragSessionStart start;
    start.dragSessionId = 9002;
    start.bundleId = 11;
    start.offerId = 22;
    start.ownerEpoch = 33;
    start.allowedActions = transfer_action::Copy;
    start.preferredAction = TransferAction::Copy;
    start.start = dragPointForMapper(640, 360);
    assert(endpoint->dragStart(start) ==
           fusiondesk::protocol::ResponseStatus::Ok);

    const winclip::WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint->diagnostics();
    assert(diagnostics.lastDragX == 100);
    assert(diagnostics.lastDragY == 160);

    assert(endpoint->dragCancel(9002, DragCancelReason::UserCancelled) ==
           fusiondesk::protocol::ResponseStatus::Ok);
}
#else
void clipboardEndpointUsesConfiguredDragMapper()
{
}

void clipboardEndpointUsesDynamicDragMapper()
{
}
#endif

void clipboardRuntimePolicyOptionsMapToRuntimeRules()
{
    namespace feature = fusiondesk::runtime::feature;

    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-runtime-audit",
        "--clipboard-runtime-max-audit-events",
        "3",
        "--clipboard-runtime-deny-file-range",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    const int argc = static_cast<int>(argv.size());
    const fusiondesk::runtime::ProductClipboardPolicy productPolicy =
        fusiondesk::apps::pc::productClipboardPolicyOptionValue(argc,
                                                                 argv.data());
    assert(productPolicy.runtimeRules.auditAllowed);
    assert(productPolicy.runtimeRules.maxRecentAuditEvents == 3);
    assert(!productPolicy.runtimeRules.allowRemoteFileRangeRead);
    std::shared_ptr<feature::IClipboardRuntimePolicy> runtimePolicy =
        fusiondesk::apps::pc::makeClipboardRuntimePolicy(productPolicy);

    auto configurable =
        std::dynamic_pointer_cast<feature::ConfigurableClipboardRuntimePolicy>(
            runtimePolicy);
    assert(configurable != nullptr);

    feature::ClipboardRuntimePolicyContext context;
    context.operation = feature::ClipboardRuntimeOperation::RemoteFormatRead;
    feature::ClipboardRuntimePolicyDecision decision =
        configurable->authorize(context);
    assert(decision.allowed);
    assert(decision.auditRequired);

    context.operation = feature::ClipboardRuntimeOperation::RemoteFileRangeRead;
    context.objectId = 99;
    context.requestedBytes = 128;
    decision = configurable->authorize(context);
    assert(!decision.allowed);
    assert(decision.responseStatus ==
           fusiondesk::protocol::ResponseStatus::DeniedByPolicy);

    const feature::ClipboardRuntimePolicySnapshot snapshot =
        configurable->snapshot();
    assert(snapshot.authorizeCalls == 2);
    assert(snapshot.allowed == 1);
    assert(snapshot.denied == 1);
    assert(snapshot.lastObjectId == 99);
    assert(snapshot.lastRequestedBytes == 128);
}

void clipboardEndpointSelectionOptionsAreStable()
{
    std::vector<std::string> defaultArgs = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
    };
    std::vector<char*> defaultArgv;
    defaultArgv.reserve(defaultArgs.size());
    for (std::string& arg : defaultArgs)
        defaultArgv.push_back(arg.data());
    assert(fusiondesk::apps::pc::clipboardEndpointKindOptionValue(
               static_cast<int>(defaultArgv.size()),
               defaultArgv.data()) == "auto");
    assert(!fusiondesk::apps::pc::qtClipboardEndpointRequested(
        static_cast<int>(defaultArgv.size()),
        defaultArgv.data()));

    std::vector<std::string> qtArgs = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-endpoint",
        "qt",
    };
    std::vector<char*> qtArgv;
    qtArgv.reserve(qtArgs.size());
    for (std::string& arg : qtArgs)
        qtArgv.push_back(arg.data());
    assert(fusiondesk::apps::pc::clipboardEndpointKindOptionValue(
               static_cast<int>(qtArgv.size()),
               qtArgv.data()) == "qt");
    assert(fusiondesk::apps::pc::qtClipboardEndpointRequested(
        static_cast<int>(qtArgv.size()),
        qtArgv.data()));

    std::vector<std::string> windowsArgs = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-endpoint",
        "windows",
    };
    std::vector<char*> windowsArgv;
    windowsArgv.reserve(windowsArgs.size());
    for (std::string& arg : windowsArgs)
        windowsArgv.push_back(arg.data());
    assert(fusiondesk::apps::pc::clipboardEndpointKindOptionValue(
               static_cast<int>(windowsArgv.size()),
               windowsArgv.data()) == "windows");
    assert(!fusiondesk::apps::pc::qtClipboardEndpointRequested(
        static_cast<int>(windowsArgv.size()),
        windowsArgv.data()));

    std::vector<std::string> macArgs = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-endpoint",
        "macos",
    };
    std::vector<char*> macArgv;
    macArgv.reserve(macArgs.size());
    for (std::string& arg : macArgs)
        macArgv.push_back(arg.data());
    assert(fusiondesk::apps::pc::clipboardEndpointKindOptionValue(
               static_cast<int>(macArgv.size()),
               macArgv.data()) == "macos");
    assert(!fusiondesk::apps::pc::qtClipboardEndpointRequested(
        static_cast<int>(macArgv.size()),
        macArgv.data()));

    std::vector<std::string> linuxArgs = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-endpoint",
        "linux",
    };
    std::vector<char*> linuxArgv;
    linuxArgv.reserve(linuxArgs.size());
    for (std::string& arg : linuxArgs)
        linuxArgv.push_back(arg.data());
    assert(fusiondesk::apps::pc::clipboardEndpointKindOptionValue(
               static_cast<int>(linuxArgv.size()),
               linuxArgv.data()) == "linux");
    assert(!fusiondesk::apps::pc::qtClipboardEndpointRequested(
        static_cast<int>(linuxArgv.size()),
        linuxArgv.data()));
}

void clipboardOptionsPopulateRuntimeProductPolicy()
{
    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--mount-clipboard",
        "--clipboard-no-file-contents",
        "--clipboard-runtime-audit",
        "--clipboard-runtime-max-audit-events",
        "5",
        "--clipboard-runtime-deny-read",
        "--clipboard-max-file-range-bytes",
        "999",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    const int argc = static_cast<int>(argv.size());
    const fusiondesk::runtime::RuntimeOptions options =
        fusiondesk::apps::pc::makeRuntimeOptions(argc, argv.data());
    assert(options.profile.profileId == "pc-shell-selected-profile");
    assert(!options.profile.clipboardPolicy.modulePolicy.allowFileContents);
    assert(options.profile.clipboardPolicy.modulePolicy.maxFileRangeBytes == 999);
    assert(options.profile.clipboardPolicy.runtimeRules.auditAllowed);
    assert(options.profile.clipboardPolicy.runtimeRules.maxRecentAuditEvents == 5);
    assert(!options.profile.clipboardPolicy.runtimeRules.allowRemoteFormatRead);

    const fusiondesk::runtime::feature::ClipboardRuntimePolicyRules rules =
        fusiondesk::runtime::feature::clipboardRuntimePolicyRulesFromProductPolicy(
            options.profile.clipboardPolicy);
    assert(!rules.allowRemoteFileRangeRead);
    assert(!rules.allowRemoteObjectLock);
    assert(!rules.allowRemoteObjectUnlock);
}

void clipboardPolicyFilePopulatesRuntimeProductPolicy()
{
    const std::filesystem::path path =
        uniqueTempPath("pc_clipboard_policy");
    writeTextFile(
        path,
        R"({
  "clipboardPolicy": {
    "module": {
      "allowImage": false,
      "allowFileContents": false,
      "maxFileRangeBytes": 2048,
      "maxFileCount": 7
    },
    "runtime": {
      "auditAllowed": true,
      "maxRecentAuditEvents": 4,
      "allowRemoteFormatRead": false,
      "denialReason": "enterprise_clipboard_policy_denied"
    }
  }
})");

    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-policy-file",
        path.string(),
        "--clipboard-allow-custom-formats",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    const int argc = static_cast<int>(argv.size());
    const fusiondesk::apps::pc::RuntimeOptionsBuildResult result =
        fusiondesk::apps::pc::makeRuntimeOptionsResult(argc, argv.data());
    assert(result.ok);
    assert(result.options.profile.defaultFeatures.has(
        fusiondesk::protocol::feature::Clipboard));
    assert(!result.options.profile.clipboardPolicy.modulePolicy.allowImage);
    assert(!result.options.profile.clipboardPolicy.modulePolicy.allowFileContents);
    assert(result.options.profile.clipboardPolicy.modulePolicy.allowCustomFormats);
    assert(result.options.profile.clipboardPolicy.modulePolicy.maxFileRangeBytes == 2048);
    assert(result.options.profile.clipboardPolicy.modulePolicy.maxFileCount == 7);
    assert(result.options.profile.clipboardPolicy.runtimeRules.auditAllowed);
    assert(result.options.profile.clipboardPolicy.runtimeRules.maxRecentAuditEvents == 4);
    assert(!result.options.profile.clipboardPolicy.runtimeRules.allowRemoteFormatRead);
    assert(!result.options.profile.clipboardPolicy.runtimeRules.allowRemoteFileRangeRead);
    assert(!result.options.profile.clipboardPolicy.runtimeRules.allowRemoteObjectLock);
    assert(!result.options.profile.clipboardPolicy.runtimeRules.allowRemoteObjectUnlock);
    assert(result.options.profile.clipboardPolicy.runtimeRules.denialReason ==
           "enterprise_clipboard_policy_denied");

    std::filesystem::remove(path);
}

void clipboardPolicyFileRoundTripsSerializedProductPolicy()
{
    const std::filesystem::path path =
        uniqueTempPath("pc_clipboard_policy_roundtrip");

    fusiondesk::runtime::ProductClipboardPolicy policy;
    policy.modulePolicy.allowAnnounce = false;
    policy.modulePolicy.allowImage = false;
    policy.modulePolicy.allowFileContents = false;
    policy.modulePolicy.allowCustomFormats = true;
    policy.modulePolicy.maxInlineBytes = 333;
    policy.modulePolicy.maxFileRangeBytes = 444;
    policy.modulePolicy.maxFileCount = 5;
    policy.runtimeRules.auditAllowed = true;
    policy.runtimeRules.maxRecentAuditEvents = 6;
    policy.runtimeRules.allowRemoteFormatRead = false;
    policy.runtimeRules.allowRemoteFileRangeRead = true;
    policy.runtimeRules.allowRemoteObjectLock = true;
    policy.runtimeRules.allowRemoteObjectUnlock = true;
    policy.runtimeRules.denialReason = "roundtrip_denied";

    const std::string compact =
        fusiondesk::apps::pc::clipboardProductPolicyToJson(policy, true);
    assert(compact.find("\"clipboardPolicy\"") != std::string::npos);
    assert(compact.find("\"module\"") != std::string::npos);
    assert(compact.find("\"runtime\"") != std::string::npos);

    const fusiondesk::apps::pc::PcClipboardPolicyFileSaveResult saved =
        fusiondesk::apps::pc::saveClipboardProductPolicyToJsonFile(
            path.string(),
            policy);
    assert(saved.ok);

    const fusiondesk::apps::pc::PcClipboardPolicyFileLoadResult loaded =
        fusiondesk::apps::pc::loadClipboardProductPolicyFromJsonFile(
            path.string());
    assert(loaded.ok);
    assert(loaded.loaded);
    assert(!loaded.policy.modulePolicy.allowAnnounce);
    assert(!loaded.policy.modulePolicy.allowImage);
    assert(!loaded.policy.modulePolicy.allowFileContents);
    assert(loaded.policy.modulePolicy.allowCustomFormats);
    assert(loaded.policy.modulePolicy.maxInlineBytes == 333);
    assert(loaded.policy.modulePolicy.maxFileRangeBytes == 444);
    assert(loaded.policy.modulePolicy.maxFileCount == 5);
    assert(loaded.policy.runtimeRules.auditAllowed);
    assert(loaded.policy.runtimeRules.maxRecentAuditEvents == 6);
    assert(!loaded.policy.runtimeRules.allowRemoteFormatRead);
    assert(!loaded.policy.runtimeRules.allowRemoteFileRangeRead);
    assert(!loaded.policy.runtimeRules.allowRemoteObjectLock);
    assert(!loaded.policy.runtimeRules.allowRemoteObjectUnlock);
    assert(loaded.policy.runtimeRules.denialReason == "roundtrip_denied");

    std::filesystem::remove(path);
}

void invalidClipboardPolicyFileBlocksRuntimeOptions()
{
    const std::filesystem::path path =
        uniqueTempPath("pc_clipboard_policy_invalid");
    writeTextFile(path, "{ invalid json");

    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-policy-file",
        path.string(),
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    const int argc = static_cast<int>(argv.size());
    const fusiondesk::apps::pc::RuntimeOptionsBuildResult result =
        fusiondesk::apps::pc::makeRuntimeOptionsResult(argc, argv.data());
    assert(!result.ok);
    assert(!result.messages.empty());

    std::filesystem::remove(path);

    const std::filesystem::path typePath =
        uniqueTempPath("pc_clipboard_policy_invalid_type");
    writeTextFile(typePath, R"({"clipboardPolicy":{"module":false}})");
    std::vector<std::string> typeArgs = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--clipboard-policy-file",
        typePath.string(),
    };
    std::vector<char*> typeArgv;
    typeArgv.reserve(typeArgs.size());
    for (std::string& arg : typeArgs)
        typeArgv.push_back(arg.data());

    const fusiondesk::apps::pc::RuntimeOptionsBuildResult typeResult =
        fusiondesk::apps::pc::makeRuntimeOptionsResult(
            static_cast<int>(typeArgv.size()),
            typeArgv.data());
    assert(!typeResult.ok);
    assert(!typeResult.messages.empty());

    std::filesystem::remove(typePath);
}

void clipboardPolicyExportShellWritesEffectiveJson()
{
    const std::filesystem::path path =
        uniqueTempPath("pc_clipboard_policy_export");
    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--smoke",
        "--clipboard-policy-export-file",
        path.string(),
        "--clipboard-no-file-contents",
        "--clipboard-runtime-audit",
        "--clipboard-runtime-deny-read",
        "--clipboard-max-file-count",
        "9",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    const int argc = static_cast<int>(argv.size());
    assert(fusiondesk::apps::pc::runPcShell(
               argc,
               argv.data(),
               fusiondesk::apps::pc::PcShellRole::Client) == 0);
    assert(std::filesystem::exists(path));

    const fusiondesk::apps::pc::PcClipboardPolicyFileLoadResult loaded =
        fusiondesk::apps::pc::loadClipboardProductPolicyFromJsonFile(
            path.string());
    assert(loaded.ok);
    assert(!loaded.policy.modulePolicy.allowFileContents);
    assert(loaded.policy.modulePolicy.maxFileCount == 9);
    assert(loaded.policy.runtimeRules.auditAllowed);
    assert(!loaded.policy.runtimeRules.allowRemoteFormatRead);
    assert(!loaded.policy.runtimeRules.allowRemoteFileRangeRead);
    assert(!loaded.policy.runtimeRules.allowRemoteObjectLock);
    assert(!loaded.policy.runtimeRules.allowRemoteObjectUnlock);

    std::filesystem::remove(path);
}

int runShell(fusiondesk::apps::pc::PcShellRole role)
{
    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--smoke",
        "--mount-input",
        "--require-profile-module",
        "input.mouse",
        "--require-profile-module",
        "input.keyboard",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    int argc = static_cast<int>(argv.size());
    return fusiondesk::apps::pc::runPcShell(argc, argv.data(), role);
}

int runClipboardShell(fusiondesk::apps::pc::PcShellRole role)
{
    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--smoke",
        "--mount-clipboard",
        "--require-profile-module",
        "clipboard.redirect",
        "--clipboard-no-drag",
        "--clipboard-max-file-range-bytes",
        "4096",
        "--clipboard-max-file-count",
        "8",
        "--clipboard-max-directory-depth",
        "4",
        "--clipboard-no-expand-directories",
        "--windows-clipboard-native-drag-loop",
        "--clipboard-drag-viewport-width",
        "640",
        "--clipboard-drag-viewport-height",
        "360",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    int argc = static_cast<int>(argv.size());
    return fusiondesk::apps::pc::runPcShell(argc, argv.data(), role);
}

int runStartWithoutReadyChannels()
{
    std::vector<std::string> args = {
        "fusiondesk_pc_feature_adapter_startup_smoke",
        "--smoke",
        "--mount-input",
        "--start-profile-modules",
        "--wait-channels-ms",
        "0",
    };
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args)
        argv.push_back(arg.data());

    int argc = static_cast<int>(argv.size());
    return fusiondesk::apps::pc::runPcShell(argc,
                                             argv.data(),
                                             fusiondesk::apps::pc::PcShellRole::Client);
}

void mountsFeatureAdapterProfilesForBothPcRoles()
{
    clipboardEndpointUsesConfiguredDragMapper();
    clipboardEndpointUsesDynamicDragMapper();
    clipboardRuntimePolicyOptionsMapToRuntimeRules();
    clipboardEndpointSelectionOptionsAreStable();
    clipboardOptionsPopulateRuntimeProductPolicy();
    clipboardPolicyFilePopulatesRuntimeProductPolicy();
    clipboardPolicyFileRoundTripsSerializedProductPolicy();
    invalidClipboardPolicyFileBlocksRuntimeOptions();
    clipboardPolicyExportShellWritesEffectiveJson();
    assert(runShell(fusiondesk::apps::pc::PcShellRole::Client) == 0);
    assert(runShell(fusiondesk::apps::pc::PcShellRole::Agent) == 0);
    assert(runClipboardShell(fusiondesk::apps::pc::PcShellRole::Client) == 0);
    assert(runClipboardShell(fusiondesk::apps::pc::PcShellRole::Agent) == 0);
    assert(runStartWithoutReadyChannels() == 8);
}

} // namespace

int main()
{
    mountsFeatureAdapterProfilesForBothPcRoles();
    return 0;
}
