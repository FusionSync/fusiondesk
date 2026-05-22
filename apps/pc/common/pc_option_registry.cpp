#include "pc_option_registry.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <ostream>
#include <sstream>

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

PcOptionDefinition option(std::string name,
                          PcOptionValueType type,
                          std::string valueName,
                          bool repeatable,
                          std::vector<std::string> enumValues,
                          std::string owner,
                          std::string configPath,
                          std::string guiGroup,
                          std::string guiLabel,
                          PcOptionVisibility visibility,
                          std::string description)
{
    PcOptionDefinition definition;
    definition.name = std::move(name);
    definition.valueType = type;
    definition.valueName = std::move(valueName);
    definition.repeatable = repeatable;
    definition.enumValues = std::move(enumValues);
    definition.owner = std::move(owner);
    definition.configPath = std::move(configPath);
    definition.guiGroup = std::move(guiGroup);
    definition.guiLabel = std::move(guiLabel);
    definition.visibility = visibility;
    definition.description = std::move(description);
    return definition;
}

PcOptionDefinition flag(std::string name,
                        std::string owner,
                        PcOptionVisibility visibility,
                        std::string description,
                        std::string configPath = {},
                        std::string guiGroup = {},
                        std::string guiLabel = {})
{
    return option(std::move(name),
                  PcOptionValueType::Flag,
                  {},
                  false,
                  {},
                  std::move(owner),
                  std::move(configPath),
                  std::move(guiGroup),
                  std::move(guiLabel),
                  visibility,
                  std::move(description));
}

PcOptionDefinition value(std::string name,
                         PcOptionValueType type,
                         std::string valueName,
                         std::string owner,
                         PcOptionVisibility visibility,
                         std::string description,
                         std::string configPath = {},
                         std::string guiGroup = {},
                         std::string guiLabel = {})
{
    return option(std::move(name),
                  type,
                  std::move(valueName),
                  false,
                  {},
                  std::move(owner),
                  std::move(configPath),
                  std::move(guiGroup),
                  std::move(guiLabel),
                  visibility,
                  std::move(description));
}

PcOptionDefinition repeatedValue(std::string name,
                                 PcOptionValueType type,
                                 std::string valueName,
                                 std::string owner,
                                 PcOptionVisibility visibility,
                                 std::string description,
                                 std::string configPath = {},
                                 std::string guiGroup = {},
                                 std::string guiLabel = {})
{
    return option(std::move(name),
                  type,
                  std::move(valueName),
                  true,
                  {},
                  std::move(owner),
                  std::move(configPath),
                  std::move(guiGroup),
                  std::move(guiLabel),
                  visibility,
                  std::move(description));
}

PcOptionDefinition enumValue(std::string name,
                             std::string valueName,
                             std::vector<std::string> enumValues,
                             std::string owner,
                             PcOptionVisibility visibility,
                             std::string description,
                             std::string configPath = {},
                             std::string guiGroup = {},
                             std::string guiLabel = {})
{
    return option(std::move(name),
                  PcOptionValueType::Enum,
                  std::move(valueName),
                  false,
                  std::move(enumValues),
                  std::move(owner),
                  std::move(configPath),
                  std::move(guiGroup),
                  std::move(guiLabel),
                  visibility,
                  std::move(description));
}

const std::vector<PcOptionDefinition>& definitions()
{
    static const std::vector<PcOptionDefinition> options = {
        flag("--help", "app", PcOptionVisibility::User, "Print PC shell options and exit."),
        flag("--help-all", "app", PcOptionVisibility::Developer, "Print all PC shell options, including developer and test controls."),
        flag("--dump-gui-config-model", "app", PcOptionVisibility::Developer, "Print a JSON model for GUI-facing configuration fields and exit."),

        flag("--smoke", "app", PcOptionVisibility::Test, "Initialize RuntimeHost and session, then exit."),
        value("--run-ms", PcOptionValueType::Integer, "ms", "app", PcOptionVisibility::Test, "Run a bounded Qt event loop before exiting."),
        value("--session-id", PcOptionValueType::UnsignedInteger, "id", "session", PcOptionVisibility::Developer, "Inject a session id for local scripts and smoke tests."),

        value("--transport-profile", PcOptionValueType::Path, "path", "connection", PcOptionVisibility::Developer, "Load a client-side Qt TCP transport profile JSON."),
        value("--listen-profile", PcOptionValueType::Path, "path", "connection", PcOptionVisibility::Developer, "Load an agent-side Qt TCP listen profile JSON."),
        value("--wait-channels-ms", PcOptionValueType::Integer, "ms", "runtime", PcOptionVisibility::Developer, "Wait budget for required module channels before starting modules."),

        flag("--peer-profile-service", "connection.fdpp", PcOptionVisibility::Developer, "Start the FDPP peer-profile responder."),
        repeatedValue("--peer-profile-channel", PcOptionValueType::String, "name=endpoint", "connection.fdpp", PcOptionVisibility::Developer, "Request a peer connection channel from the FDPP responder."),
        value("--peer-profile-agent-session", PcOptionValueType::UnsignedInteger, "id", "connection.fdpp", PcOptionVisibility::Developer, "Agent session id carried by the FDPP request."),
        value("--peer-profile-client-ready-prefix", PcOptionValueType::String, "text", "connection.fdpp", PcOptionVisibility::Developer, "Client ready endpoint prefix for generated peer profiles."),
        value("--peer-profile-agent-ready-prefix", PcOptionValueType::String, "text", "connection.fdpp", PcOptionVisibility::Developer, "Agent ready endpoint prefix for generated peer profiles."),
        value("--peer-profile-wait-ms", PcOptionValueType::Integer, "ms", "connection.fdpp", PcOptionVisibility::Developer, "Wait budget for FDPP completion."),
        value("--peer-profile-timeout-ms", PcOptionValueType::Integer, "ms", "connection.fdpp", PcOptionVisibility::Developer, "Packet timeout carried by FDPP requests."),

        flag("--module-inventory-service", "connection.fdmi", PcOptionVisibility::Developer, "Start the FDMI module inventory responder."),
        flag("--module-inventory-request", "connection.fdmi", PcOptionVisibility::Developer, "Request the peer module inventory."),
        value("--module-inventory-wait-ms", PcOptionValueType::Integer, "ms", "connection.fdmi", PcOptionVisibility::Developer, "Wait budget for module inventory exchange."),
        value("--module-inventory-timeout-ms", PcOptionValueType::Integer, "ms", "connection.fdmi", PcOptionVisibility::Developer, "Packet timeout carried by FDMI requests."),

        value("--reconnect-profile", PcOptionValueType::Path, "path", "runtime.reconnect", PcOptionVisibility::Developer, "Load a reconnect orchestration profile and trigger reconnect."),
        value("--reconnect-after-ms", PcOptionValueType::Integer, "ms", "runtime.reconnect", PcOptionVisibility::Developer, "Delay before applying the reconnect profile."),
        value("--reconnect-reason", PcOptionValueType::String, "text", "runtime.reconnect", PcOptionVisibility::Developer, "Reason stored in reconnect diagnostics."),
        flag("--reconnect-no-display-keyframe", "runtime.reconnect", PcOptionVisibility::Developer, "Do not request a display keyframe after reconnect."),

        flag("--mount-profile-modules", "runtime.modules", PcOptionVisibility::Developer, "Mount selected product profile modules without starting them."),
        flag("--start-profile-modules", "runtime.modules", PcOptionVisibility::Developer, "Start all mounted profile modules after channel readiness."),
        repeatedValue("--profile-module", PcOptionValueType::String, "id", "runtime.modules", PcOptionVisibility::Developer, "Add a profile module id to the startup ProductProfile."),
        repeatedValue("--require-profile-module", PcOptionValueType::String, "id", "runtime.modules", PcOptionVisibility::Test, "Assert that a profile module was mounted."),

        flag("--mount-display", "display.screen", PcOptionVisibility::Developer, "Mount display.screen dependencies."),
        flag("--start-display", "display.screen", PcOptionVisibility::Developer, "Start display modules after channel readiness."),
        flag("--pump-display", "display.screen", PcOptionVisibility::Developer, "Run the agent-side display frame pump."),
        flag("--show-display-window", "display.screen", PcOptionVisibility::User, "Open the client display window.", "display.openWindow", "Remote Display", "Open remote desktop window"),
        value("--display-fps", PcOptionValueType::Integer, "n", "display.screen", PcOptionVisibility::Advanced, "Target display pump frame rate.", "display.quality.frameRate", "Remote Display", "Smoothness"),
        value("--display-first-frame-timeout-ms", PcOptionValueType::Integer, "ms", "display.screen", PcOptionVisibility::Developer, "Client first-frame timeout before requesting a keyframe."),
        flag("--require-display-frame", "display.screen", PcOptionVisibility::Test, "Verify display frame progress during a bounded run."),
        enumValue("--display-source-type", "<type>", {"auto", "unknown", "monitor", "screen", "display", "desktop", "window", "app", "application", "window_capture", "virtual_display", "virtual", "vdisplay", "mobile_projection", "mobile", "projection", "media_projection", "android_projection"}, "display.screen", PcOptionVisibility::User, "Select the display source kind.", "display.shareSource.type", "Remote Display", "Share content"),
        value("--display-source-id", PcOptionValueType::Integer, "n", "display.screen", PcOptionVisibility::User, "Select a display source id.", "display.shareSource.id", "Remote Display", "Selected screen or window"),
        value("--display-native-source-handle", PcOptionValueType::UnsignedInteger, "n|0xhex", "display.screen", PcOptionVisibility::Developer, "Native source handle, such as an HWND."),
        value("--display-target-width", PcOptionValueType::Integer, "px", "display.screen", PcOptionVisibility::User, "Target capture width.", "display.resolution.width", "Remote Display", "Custom width"),
        value("--display-target-height", PcOptionValueType::Integer, "px", "display.screen", PcOptionVisibility::User, "Target capture height.", "display.resolution.height", "Remote Display", "Custom height"),
        enumValue("--display-scale-mode", "<mode>", {"source", "fit", "stretch"}, "display.screen", PcOptionVisibility::User, "Scale remote display content.", "display.scaleMode", "Remote Display", "Display scaling"),
        flag("--display-no-cursor", "display.screen", PcOptionVisibility::User, "Hide the remote cursor in captured frames.", "display.showRemoteCursor", "Remote Display", "Show remote cursor"),
        value("--display-capture-backend", PcOptionValueType::String, "auto|gdi|dxgi|wgc|adapter-id", "display.screen", PcOptionVisibility::Advanced, "Force a display capture backend for diagnostics.", "display.captureBackend", "Advanced Display", "Screen capture method"),
        enumValue("--display-target-platform", "<platform>", {"auto", "unknown", "windows", "win", "win32", "win64", "windows_desktop", "windows_pc", "linux_x11", "x11", "linux_desktop_x11", "linux_wayland", "wayland", "linux_desktop_wayland", "linux_embedded", "embedded_linux", "drm_kms", "kms", "gbm", "macos", "mac", "darwin", "osx", "android_client", "android_controller", "android_agent", "android", "harmonyos", "harmony", "hmos", "openharmony", "open_harmony", "ohos", "rockchip_linux", "rk_linux", "rk3568_linux", "rk3588_linux", "rockchip_android", "rk_android", "rk3568_android", "rk3588_android"}, "display.screen", PcOptionVisibility::Developer, "Platform-family hint for display backend selection."),
        enumValue("--display-target-arch", "<arch>", {"auto", "unknown", "x86", "i386", "i686", "x86_64", "x64", "amd64", "arm", "arm32", "armv7", "armeabi_v7a", "arm64", "aarch64", "arm64_v8a", "loongarch64", "loong64", "mips64el", "mips64le"}, "display.screen", PcOptionVisibility::Developer, "Architecture hint for display backend selection."),
        enumValue("--display-target-soc", "<soc>", {"auto", "unknown", "generic", "rk3568", "rockchip3568", "rockchip_3568", "3568", "rk3588", "rockchip3588", "rockchip_3588", "3588"}, "display.screen", PcOptionVisibility::Developer, "SoC hint for display backend selection."),

        enumValue("--display-codec", "<codec>", {"auto", "raw", "raw_bgra", "bgra", "raw_frame", "h264", "avc", "h265", "hevc", "av1"}, "display.codec", PcOptionVisibility::Advanced, "Preferred display codec family.", "display.codec.preference", "Advanced Display", "Video codec"),
        value("--display-codec-backend", PcOptionValueType::String, "auto|raw|mf-h264|adapter-id", "display.codec", PcOptionVisibility::Developer, "Force a display codec backend id."),
        enumValue("--display-codec-policy", "<policy>", {"default", "raw", "raw-stable", "windows-h264-production", "h264-production", "mf-h264-production", "windows-h264-validation", "h264-validation", "mf-h264-validation"}, "display.codec", PcOptionVisibility::Advanced, "Select a display codec rollout policy.", "display.codec.policy", "Advanced Display", "Encoding strategy"),
        flag("--display-codec-negotiate-local", "display.codec", PcOptionVisibility::Developer, "Run local display codec negotiation diagnostics."),
        flag("--display-codec-negotiate-fdpp", "display.codec", PcOptionVisibility::Developer, "Negotiate display codec through FDPP."),
        value("--display-codec-fdpp-wait-ms", PcOptionValueType::Integer, "ms", "display.codec", PcOptionVisibility::Developer, "Agent wait budget for FDPP display codec negotiation."),
        flag("--display-codec-no-hardware", "display.codec", PcOptionVisibility::Advanced, "Disable hardware codec candidates.", "display.codec.allowHardware", "Advanced Display", "Use hardware acceleration"),
        flag("--display-codec-no-software", "display.codec", PcOptionVisibility::Developer, "Disable software codec candidates."),
        flag("--display-codec-prefer-software", "display.codec", PcOptionVisibility::Advanced, "Prefer software codec candidates.", "display.codec.preferSoftware", "Advanced Display", "Prefer software codec"),
        flag("--display-codec-no-zerocopy", "display.codec", PcOptionVisibility::Developer, "Do not prefer zero-copy codec paths."),

        flag("--mount-input", "input", PcOptionVisibility::User, "Enable keyboard and mouse input modules.", "input.enabled", "Control Permissions", "Allow keyboard and mouse control"),
        flag("--pump-profile-modules", "input", PcOptionVisibility::Developer, "Start the runtime feature pump for input modules."),
        value("--feature-pump-interval-ms", PcOptionValueType::Integer, "ms", "input", PcOptionVisibility::Developer, "Feature pump timer interval."),
        value("--max-input-events-per-pump", PcOptionValueType::Integer, "n", "input", PcOptionVisibility::Developer, "Maximum input events drained per feature pump tick."),

        flag("--mount-clipboard", "clipboard.redirect", PcOptionVisibility::Developer, "Mount clipboard.redirect dependencies."),
        flag("--start-clipboard", "clipboard.redirect", PcOptionVisibility::Developer, "Start clipboard modules after channel readiness."),
        flag("--pump-clipboard", "clipboard.redirect", PcOptionVisibility::Developer, "Start the ClipboardRuntimeService owner."),
        value("--clipboard-pump-interval-ms", PcOptionValueType::Integer, "ms", "clipboard.redirect", PcOptionVisibility::Developer, "Clipboard runtime pump interval."),
        enumValue("--clipboard-endpoint", "<endpoint>", {"auto", "windows", "macos", "linux", "qt"}, "clipboard.redirect", PcOptionVisibility::Advanced, "Select the native clipboard endpoint adapter.", "clipboard.endpoint", "Advanced Clipboard", "Clipboard implementation"),
        value("--clipboard-owner-window-name", PcOptionValueType::String, "text", "clipboard.redirect", PcOptionVisibility::Developer, "Native owner window name for endpoints that expose one."),
        value("--clipboard-read-timeout-ms", PcOptionValueType::Integer, "ms", "clipboard.redirect", PcOptionVisibility::Developer, "Default timeout for remote clipboard reads."),
        flag("--clipboard-no-owner-suppression", "clipboard.redirect", PcOptionVisibility::Developer, "Do not suppress updates owned by the current process."),
        flag("--clipboard-no-owner-marker", "clipboard.redirect", PcOptionVisibility::Developer, "Do not write the Windows owner marker."),
        flag("--clipboard-no-delayed-rendering", "clipboard.redirect", PcOptionVisibility::Developer, "Materialize native clipboard text immediately."),
        value("--clipboard-open-retry-count", PcOptionValueType::UnsignedInteger, "n", "clipboard.redirect", PcOptionVisibility::Developer, "OpenClipboard retry attempts."),
        value("--clipboard-open-retry-delay-ms", PcOptionValueType::UnsignedInteger, "ms", "clipboard.redirect", PcOptionVisibility::Developer, "Delay between OpenClipboard retry attempts."),
        flag("--windows-clipboard-native", "clipboard.redirect", PcOptionVisibility::Developer, "Use the real Windows clipboard endpoint instead of dry-run mode."),
        flag("--windows-clipboard-native-drag-preflight", "clipboard.redirect", PcOptionVisibility::Test, "Run native Windows drag publication preflight."),
        flag("--windows-clipboard-native-drag-loop", "clipboard.redirect", PcOptionVisibility::Test, "Run the interactive Windows DoDragDrop loop."),

        value("--clipboard-policy-file", PcOptionValueType::Path, "path", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Load clipboard product policy JSON.", "clipboard.policy.importPath", "Clipboard and File Sharing", "Import clipboard policy"),
        value("--clipboard-policy-export-file", PcOptionValueType::Path, "path", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Write the effective clipboard product policy JSON.", "clipboard.policy.exportPath", "Clipboard and File Sharing", "Export clipboard policy"),
        flag("--clipboard-no-announce", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Disable local clipboard announcement.", "clipboard.direction.allowSend", "Clipboard and File Sharing", "Allow copying to remote"),
        flag("--clipboard-no-receive", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Disable receiving remote clipboard offers.", "clipboard.direction.allowReceive", "Clipboard and File Sharing", "Allow copying from remote"),
        flag("--clipboard-no-send-content", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Disable sending clipboard content to the peer.", "clipboard.direction.allowContentSend", "Clipboard and File Sharing", "Allow sending clipboard content"),
        flag("--clipboard-no-write-local", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Disable writing remote clipboard content locally.", "clipboard.direction.allowLocalWrite", "Clipboard and File Sharing", "Allow writing to local clipboard"),
        flag("--clipboard-no-plain-text", "clipboard.policy", PcOptionVisibility::User, "Disable plain text clipboard transfer.", "clipboard.content.allowText", "Clipboard and File Sharing", "Allow text"),
        flag("--clipboard-no-html", "clipboard.policy", PcOptionVisibility::User, "Disable HTML clipboard transfer.", "clipboard.content.allowRichText", "Clipboard and File Sharing", "Keep rich text formatting"),
        flag("--clipboard-no-rtf", "clipboard.policy", PcOptionVisibility::User, "Disable RTF clipboard transfer.", "clipboard.content.allowRichText", "Clipboard and File Sharing", "Keep rich text formatting"),
        flag("--clipboard-no-image", "clipboard.policy", PcOptionVisibility::User, "Disable image clipboard transfer.", "clipboard.content.allowImage", "Clipboard and File Sharing", "Allow images"),
        flag("--clipboard-no-file-list", "clipboard.policy", PcOptionVisibility::User, "Disable file-list clipboard transfer.", "clipboard.files.allowList", "Clipboard and File Sharing", "Allow file copy"),
        flag("--clipboard-no-file-contents", "clipboard.policy", PcOptionVisibility::User, "Disable remote file content reads.", "clipboard.files.allowContents", "Clipboard and File Sharing", "Allow file contents"),
        flag("--clipboard-no-drag", "clipboard.policy", PcOptionVisibility::User, "Disable clipboard drag/drop.", "clipboard.files.allowDragDrop", "Clipboard and File Sharing", "Allow file drag and drop"),
        flag("--clipboard-allow-custom-formats", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Allow custom clipboard formats.", "clipboard.content.allowCustomFormats", "Clipboard and File Sharing", "Allow app-specific formats"),
        value("--clipboard-max-inline-bytes", PcOptionValueType::UnsignedInteger, "bytes", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Maximum inline clipboard payload bytes."),
        value("--clipboard-max-file-range-bytes", PcOptionValueType::UnsignedInteger, "bytes", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Maximum bytes per remote file range read."),
        value("--clipboard-max-file-count", PcOptionValueType::UnsignedInteger, "count", "clipboard.policy", PcOptionVisibility::User, "Maximum files in one clipboard offer.", "clipboard.files.maxFileCount", "Clipboard and File Sharing", "Maximum files per copy"),
        value("--clipboard-max-single-file-bytes", PcOptionValueType::UnsignedInteger, "bytes", "clipboard.policy", PcOptionVisibility::User, "Maximum bytes for a single copied file.", "clipboard.files.maxSingleFileBytes", "Clipboard and File Sharing", "Single file size limit"),
        value("--clipboard-max-directory-depth", PcOptionValueType::UnsignedInteger, "n", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Maximum directory expansion depth for local file offers."),
        flag("--clipboard-no-expand-directories", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Do not expand local directories into file offers."),

        flag("--clipboard-runtime-audit", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Audit allowed clipboard runtime operations.", "clipboard.audit.enabled", "Security and Audit", "Record clipboard access"),
        value("--clipboard-runtime-max-audit-events", PcOptionValueType::UnsignedInteger, "count", "clipboard.policy", PcOptionVisibility::AdminPolicy, "Maximum retained recent runtime audit events."),
        flag("--clipboard-runtime-deny-announce", "clipboard.policy", PcOptionVisibility::Developer, "Deny runtime local snapshot announce operations."),
        flag("--clipboard-runtime-deny-read", "clipboard.policy", PcOptionVisibility::Developer, "Deny runtime remote format read operations."),
        flag("--clipboard-runtime-deny-file-range", "clipboard.policy", PcOptionVisibility::Developer, "Deny runtime remote file range read operations."),
        flag("--clipboard-runtime-deny-object-lock", "clipboard.policy", PcOptionVisibility::Developer, "Deny runtime remote object lock operations."),
        flag("--clipboard-runtime-deny-object-unlock", "clipboard.policy", PcOptionVisibility::Developer, "Deny runtime remote object unlock operations."),
        flag("--clipboard-runtime-deny-expiry", "clipboard.policy", PcOptionVisibility::Developer, "Deny runtime pending read expiry operations."),

        value("--clipboard-dry-run-text", PcOptionValueType::String, "text", "clipboard.test", PcOptionVisibility::Test, "Seed the Windows dry-run clipboard store with text."),
        value("--clipboard-seed-text", PcOptionValueType::String, "text", "clipboard.test", PcOptionVisibility::Test, "Publish a local text bundle before clipboard startup."),
        value("--clipboard-seed-html-file", PcOptionValueType::Path, "path", "clipboard.test", PcOptionVisibility::Test, "Publish a text/html bundle from a local file."),
        value("--clipboard-seed-rtf-file", PcOptionValueType::Path, "path", "clipboard.test", PcOptionVisibility::Test, "Publish a text/rtf bundle from a local file."),
        value("--clipboard-seed-image-png", PcOptionValueType::Path, "path", "clipboard.test", PcOptionVisibility::Test, "Publish an image/png bundle from a local file."),
        repeatedValue("--clipboard-seed-file", PcOptionValueType::Path, "path", "clipboard.test", PcOptionVisibility::Test, "Seed a dry-run Windows local file source."),
        value("--require-clipboard-text", PcOptionValueType::String, "text", "clipboard.test", PcOptionVisibility::Test, "Verify endpoint text before exit."),
        value("--require-clipboard-html-file", PcOptionValueType::Path, "path", "clipboard.test", PcOptionVisibility::Test, "Verify endpoint text/html bytes before exit."),
        value("--require-clipboard-rtf-file", PcOptionValueType::Path, "path", "clipboard.test", PcOptionVisibility::Test, "Verify endpoint text/rtf bytes before exit."),
        value("--require-clipboard-image-png", PcOptionValueType::Path, "path", "clipboard.test", PcOptionVisibility::Test, "Verify endpoint image/png bytes before exit."),
        repeatedValue("--require-clipboard-file-text", PcOptionValueType::String, "relativePath=text|text", "clipboard.test", PcOptionVisibility::Test, "Read remote file bytes through FDCL and verify text."),
        repeatedValue("--require-clipboard-endpoint-file-text", PcOptionValueType::String, "relativePath=text|text", "clipboard.test", PcOptionVisibility::Test, "Read local endpoint file bytes and verify text."),
        value("--save-clipboard-files-dir", PcOptionValueType::Path, "dir", "clipboard.test", PcOptionVisibility::Test, "Materialize remote clipboard files into a local directory."),
        value("--clipboard-require-wait-ms", PcOptionValueType::Integer, "ms", "clipboard.test", PcOptionVisibility::Test, "Wait budget for clipboard verification requirements."),
        value("--clipboard-file-read-timeout-ms", PcOptionValueType::Integer, "ms", "clipboard.test", PcOptionVisibility::Test, "Timeout for remote file range reads."),
        value("--clipboard-file-read-chunk-bytes", PcOptionValueType::UnsignedInteger, "bytes", "clipboard.test", PcOptionVisibility::Test, "Maximum bytes per test file range read chunk."),

        flag("--clipboard-send-drag-drop", "clipboard.drag", PcOptionVisibility::Test, "Send a dry-run clipboard DragStart/Move/Drop lifecycle."),
        flag("--clipboard-send-drag-start-only", "clipboard.drag", PcOptionVisibility::Test, "Send only the dry-run DragStart event."),
        value("--clipboard-drag-offer-wait-ms", PcOptionValueType::Integer, "ms", "clipboard.drag", PcOptionVisibility::Test, "Wait budget for a local clipboard offer before drag."),
        value("--clipboard-drag-session-id", PcOptionValueType::UnsignedInteger, "n", "clipboard.drag", PcOptionVisibility::Test, "Drag session id."),
        value("--clipboard-drag-start-x", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "DragStart remote logical x coordinate."),
        value("--clipboard-drag-start-y", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "DragStart remote logical y coordinate."),
        value("--clipboard-drag-move-x", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "DragMove remote logical x coordinate."),
        value("--clipboard-drag-move-y", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "DragMove remote logical y coordinate."),
        value("--clipboard-drag-drop-x", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "DragDrop remote logical x coordinate."),
        value("--clipboard-drag-drop-y", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "DragDrop remote logical y coordinate."),
        value("--clipboard-drag-surface-width", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "Remote drag surface width."),
        value("--clipboard-drag-surface-height", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "Remote drag surface height."),
        value("--clipboard-drag-viewport-x", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "Local drag viewport x origin."),
        value("--clipboard-drag-viewport-y", PcOptionValueType::Integer, "n", "clipboard.drag", PcOptionVisibility::Test, "Local drag viewport y origin."),
        value("--clipboard-drag-viewport-width", PcOptionValueType::UnsignedInteger, "n", "clipboard.drag", PcOptionVisibility::Test, "Local drag viewport width."),
        value("--clipboard-drag-viewport-height", PcOptionValueType::UnsignedInteger, "n", "clipboard.drag", PcOptionVisibility::Test, "Local drag viewport height."),
        value("--clipboard-drag-viewport-scale", PcOptionValueType::String, "n", "clipboard.drag", PcOptionVisibility::Test, "Local drag viewport scale."),
        enumValue("--clipboard-drag-output-space", "<space>", {"local-logical", "local-physical"}, "clipboard.drag", PcOptionVisibility::Test, "Output coordinate space for drag mapping."),
        flag("--clipboard-drag-no-clamp", "clipboard.drag", PcOptionVisibility::Test, "Do not clamp drag coordinates to the viewport."),

        flag("--print-session-diagnostics", "diagnostics", PcOptionVisibility::Diagnostics, "Print session runtime diagnostics."),
        flag("--print-reconnect-diagnostics", "diagnostics", PcOptionVisibility::Diagnostics, "Print reconnect runtime diagnostics."),
        flag("--print-display-runtime-diagnostics", "diagnostics", PcOptionVisibility::Diagnostics, "Print display runtime diagnostics."),
        flag("--print-display-capture-plan", "diagnostics", PcOptionVisibility::Diagnostics, "Print display capture backend selection diagnostics."),
        flag("--print-display-sources", "diagnostics", PcOptionVisibility::Diagnostics, "Print display source catalog rows."),
        flag("--print-display-codec-plan", "diagnostics", PcOptionVisibility::Diagnostics, "Print display codec selection diagnostics."),
        flag("--print-module-inventory-diagnostics", "diagnostics", PcOptionVisibility::Diagnostics, "Print module inventory diagnostics."),
        flag("--print-clipboard-diagnostics", "diagnostics", PcOptionVisibility::Diagnostics, "Print clipboard policy, runtime, module, and endpoint diagnostics."),

        repeatedValue("--send-test-echo", PcOptionValueType::String, "text", "test.echo", PcOptionVisibility::Test, "Send a test.echo request payload."),
        flag("--require-test-echo-response", "test.echo", PcOptionVisibility::Test, "Wait for a test.echo response."),
        value("--test-echo-wait-ms", PcOptionValueType::Integer, "ms", "test.echo", PcOptionVisibility::Test, "Wait budget for test.echo response."),
        value("--test-echo-timeout-ms", PcOptionValueType::Integer, "ms", "test.echo", PcOptionVisibility::Test, "Packet timeout carried by test.echo requests."),
    };
    return options;
}

std::string stripInlineValue(const std::string& argument,
                             std::string* inlineValue,
                             bool* inlineValuePresent)
{
    const std::size_t separator = argument.find('=');
    if (separator == std::string::npos) {
        if (inlineValue != nullptr)
            inlineValue->clear();
        if (inlineValuePresent != nullptr)
            *inlineValuePresent = false;
        return argument;
    }
    if (inlineValue != nullptr)
        *inlineValue = argument.substr(separator + 1);
    if (inlineValuePresent != nullptr)
        *inlineValuePresent = true;
    return argument.substr(0, separator);
}

bool startsWithDashOption(const char* value)
{
    return value != nullptr && value[0] == '-' && value[1] == '-';
}

bool parseUnsigned(const std::string& value)
{
    if (value.empty())
        return false;
    if (value[0] == '-')
        return false;
    char* end = nullptr;
    std::strtoull(value.c_str(), &end, 0);
    return end != nullptr && *end == '\0';
}

bool parseInteger(const std::string& value)
{
    if (value.empty())
        return false;
    char* end = nullptr;
    std::strtol(value.c_str(), &end, 10);
    return end != nullptr && *end == '\0';
}

void appendJsonString(std::ostream& output, const std::string& value)
{
    output << '"';
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            output << ch;
            break;
        }
    }
    output << '"';
}

std::string joinedEnumValues(const std::vector<std::string>& values)
{
    std::string joined;
    for (const std::string& value : values) {
        if (!joined.empty())
            joined += "|";
        joined += value;
    }
    return joined;
}

bool visibleInDefaultHelp(PcOptionVisibility visibility)
{
    return visibility == PcOptionVisibility::User ||
           visibility == PcOptionVisibility::Advanced ||
           visibility == PcOptionVisibility::AdminPolicy ||
           visibility == PcOptionVisibility::Diagnostics;
}

} // namespace

const std::vector<PcOptionDefinition>& pcOptionDefinitions()
{
    return definitions();
}

const PcOptionDefinition* findPcOptionDefinition(const std::string& name)
{
    const std::vector<PcOptionDefinition>& options = definitions();
    const auto it = std::find_if(
        options.begin(),
        options.end(),
        [&name](const PcOptionDefinition& option) {
            return option.name == name;
        });
    return it == options.end() ? nullptr : &(*it);
}

bool pcShellHelpRequested(int argc, char** argv)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--help" || argument == "--help-all" ||
            argument == "-h") {
            return true;
        }
    }
    return false;
}

bool pcShellGuiConfigModelRequested(int argc, char** argv)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--dump-gui-config-model")
            return true;
    }
    return false;
}

PcOptionValidationResult validatePcShellOptions(int argc, char** argv)
{
    PcOptionValidationResult result;
    for (int index = 1; index < argc; ++index) {
        const char* raw = argv[index];
        if (raw == nullptr)
            continue;

        const std::string argument(raw);
        if (argument == "--")
            break;
        if (argument == "-h")
            continue;
        if (argument.rfind("--", 0) != 0)
            continue;

        std::string inlineValue;
        bool inlineValuePresent = false;
        const std::string optionName =
            stripInlineValue(argument, &inlineValue, &inlineValuePresent);
        const PcOptionDefinition* definition =
            findPcOptionDefinition(optionName);
        if (definition == nullptr) {
            result.ok = false;
            result.messages.push_back("unknown PC shell option: " + optionName);
            continue;
        }

        if (definition->valueType == PcOptionValueType::Flag) {
            if (inlineValuePresent) {
                result.ok = false;
                result.messages.push_back(
                    "flag option does not accept a value: " + optionName);
            }
            continue;
        }

        std::string actualValue = inlineValue;
        if (inlineValuePresent) {
            if (actualValue.empty()) {
                result.ok = false;
                result.messages.push_back("missing value for option: " +
                                          optionName);
                continue;
            }
        } else {
            if (index + 1 >= argc || startsWithDashOption(argv[index + 1])) {
                result.ok = false;
                result.messages.push_back("missing value for option: " +
                                          optionName);
                continue;
            }
            actualValue = argv[index + 1] == nullptr ? "" : argv[index + 1];
            ++index;
        }

        if (definition->valueType == PcOptionValueType::Integer &&
            !parseInteger(actualValue)) {
            result.ok = false;
            result.messages.push_back("option expects an integer: " +
                                      optionName);
        } else if (definition->valueType == PcOptionValueType::UnsignedInteger &&
                   !parseUnsigned(actualValue)) {
            result.ok = false;
            result.messages.push_back("option expects an unsigned integer: " +
                                      optionName);
        } else if (definition->valueType == PcOptionValueType::Enum &&
                   std::find(definition->enumValues.begin(),
                             definition->enumValues.end(),
                             actualValue) == definition->enumValues.end()) {
            result.ok = false;
            result.messages.push_back(
                "invalid value for " + optionName + ": " + actualValue +
                " (expected one of: " +
                joinedEnumValues(definition->enumValues) + ")");
        }
    }
    return result;
}

void writePcShellHelp(std::ostream& output,
                      const std::string& executableName,
                      bool includeDeveloperOptions)
{
    constexpr std::size_t kHelpUsageColumnWidth = 46;
    constexpr std::size_t kHelpDescriptionIndent = 48;

    output << "Usage: " << executableName << " [options]\n\n";
    output << "Common product options are listed first. Use --help-all to include developer and test controls.\n\n";

    std::map<PcOptionVisibility, std::vector<const PcOptionDefinition*>>
        grouped;
    for (const PcOptionDefinition& definition : definitions()) {
        if (!includeDeveloperOptions &&
            !visibleInDefaultHelp(definition.visibility)) {
            continue;
        }
        grouped[definition.visibility].push_back(&definition);
    }

    const PcOptionVisibility order[] = {
        PcOptionVisibility::User,
        PcOptionVisibility::Advanced,
        PcOptionVisibility::AdminPolicy,
        PcOptionVisibility::Diagnostics,
        PcOptionVisibility::Developer,
        PcOptionVisibility::Test};

    for (const PcOptionVisibility visibility : order) {
        const auto found = grouped.find(visibility);
        if (found == grouped.end())
            continue;
        output << pcOptionVisibilityName(visibility) << " options:\n";
        for (const PcOptionDefinition* definition : found->second) {
            std::ostringstream usage;
            usage << "  " << definition->name;
            if (definition->valueType != PcOptionValueType::Flag)
                usage << " " << definition->valueName;
            const std::string usageText = usage.str();
            if (usageText.size() >= kHelpUsageColumnWidth) {
                output << usageText << "\n"
                       << std::string(kHelpDescriptionIndent, ' ')
                       << definition->description << "\n";
            } else {
                output << std::left << std::setw(kHelpUsageColumnWidth)
                       << usageText << definition->description << "\n";
            }
        }
        output << "\n";
    }
}

void writePcShellGuiConfigModelJson(std::ostream& output)
{
    output << "{\n";
    output << "  \"version\": 1,\n";
    output << "  \"description\": \"FusionDesk PC shell GUI-facing configuration model derived from the app-level option registry.\",\n";
    output << "  \"fields\": [\n";
    bool first = true;
    for (const PcOptionDefinition& definition : definitions()) {
        if (definition.configPath.empty() || definition.guiLabel.empty())
            continue;
        if (!first)
            output << ",\n";
        first = false;
        output << "    {\n";
        output << "      \"path\": ";
        appendJsonString(output, definition.configPath);
        output << ",\n      \"label\": ";
        appendJsonString(output, definition.guiLabel);
        output << ",\n      \"group\": ";
        appendJsonString(output, definition.guiGroup);
        output << ",\n      \"visibility\": ";
        appendJsonString(output, pcOptionVisibilityName(definition.visibility));
        output << ",\n      \"owner\": ";
        appendJsonString(output, definition.owner);
        output << ",\n      \"cliOption\": ";
        appendJsonString(output, definition.name);
        output << ",\n      \"type\": ";
        appendJsonString(output, pcOptionValueTypeName(definition.valueType));
        output << ",\n      \"description\": ";
        appendJsonString(output, definition.description);
        if (!definition.enumValues.empty()) {
            output << ",\n      \"enumValues\": [";
            for (std::size_t index = 0; index < definition.enumValues.size();
                 ++index) {
                if (index != 0)
                    output << ", ";
                appendJsonString(output, definition.enumValues[index]);
            }
            output << "]";
        }
        output << "\n    }";
    }
    output << "\n  ]\n";
    output << "}\n";
}

const char* pcOptionValueTypeName(PcOptionValueType type)
{
    switch (type) {
    case PcOptionValueType::Flag:
        return "flag";
    case PcOptionValueType::String:
        return "string";
    case PcOptionValueType::Integer:
        return "integer";
    case PcOptionValueType::UnsignedInteger:
        return "unsigned-integer";
    case PcOptionValueType::Path:
        return "path";
    case PcOptionValueType::Enum:
        return "enum";
    }
    return "unknown";
}

const char* pcOptionVisibilityName(PcOptionVisibility visibility)
{
    switch (visibility) {
    case PcOptionVisibility::User:
        return "user";
    case PcOptionVisibility::Advanced:
        return "advanced";
    case PcOptionVisibility::AdminPolicy:
        return "admin-policy";
    case PcOptionVisibility::Diagnostics:
        return "diagnostics";
    case PcOptionVisibility::Developer:
        return "developer";
    case PcOptionVisibility::Test:
        return "test";
    }
    return "unknown";
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
