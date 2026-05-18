#include "fusiondesk/core/module/module_catalog.h"

#include <utility>

namespace fusiondesk {
namespace module {
namespace catalog {

namespace {

using protocol::ChannelIdValue;
using protocol::ChannelType;
using protocol::PacketType;

ModuleManifest makeBase(std::string moduleId,
                        std::string displayName,
                        std::string sku,
                        protocol::FeatureMask feature,
                        std::uint32_t roleFlags,
                        std::uint32_t runModeFlags)
{
    ModuleManifest manifest;
    manifest.moduleId = std::move(moduleId);
    manifest.displayName = std::move(displayName);
    manifest.sku = std::move(sku);
    manifest.version = ModuleVersion{1, 0, 0};
    manifest.feature = feature;
    manifest.roleFlags = roleFlags;
    manifest.runModeFlags = runModeFlags;
    manifest.standaloneSaleUnit = true;
    manifest.disableAtRuntime = true;
    manifest.supportedPlatforms = {"windows", "linux", "macos"};
    return manifest;
}

ModulePeerCompatibility compatiblePeer(std::string peerModuleId)
{
    ModulePeerCompatibility peer;
    peer.peerModuleId = std::move(peerModuleId);
    peer.minPeerVersion = ModuleVersion{1, 0, 0};
    peer.maxPeerVersion = ModuleVersion{1, 99, 99};
    peer.compatibilityMode = "v1-family";
    return peer;
}

ChannelBinding bind(std::string name,
                    ChannelIdValue channelId,
                    ChannelType channelType,
                    bool required,
                    bool shared,
                    std::vector<PacketType> consumes,
                    std::vector<PacketType> produces)
{
    ChannelBinding binding;
    binding.name = std::move(name);
    binding.channelId = static_cast<protocol::ChannelId>(channelId);
    binding.channelType = channelType;
    binding.required = required;
    binding.shared = shared;
    binding.consumes = std::move(consumes);
    binding.produces = std::move(produces);
    return binding;
}

constexpr std::uint32_t clientAgent = ModuleRoleClient | ModuleRoleAgent;
constexpr std::uint32_t inProcessHosted = ModuleRunModeInProcess | ModuleRunModeHosted;
constexpr std::uint32_t inProcessHostedStandalone =
    ModuleRunModeInProcess | ModuleRunModeHosted | ModuleRunModeStandalone;

} // namespace

ModuleManifest displayScreen()
{
    return makeBase("display.screen",
                    "Screen projection capability",
                    "fusiondesk.module.display",
                    protocol::feature::Screen,
                    clientAgent,
                    inProcessHosted);
}

ModuleManifest displayScreenAgent()
{
    ModuleManifest manifest = makeBase("display.screen.agent",
                                       "Screen projection agent",
                                       "fusiondesk.module.display",
                                       protocol::feature::Screen,
                                       ModuleRoleAgent,
                                       inProcessHosted);
    manifest.compatiblePeers = {compatiblePeer("display.screen.client")};
    manifest.channels.push_back(bind("main_screen",
                                     ChannelIdValue::DesktopScreen,
                                     ChannelType::Video,
                                     true,
                                     false,
                                     {PacketType::Watermark},
                                     {PacketType::Video, PacketType::CursorChange, PacketType::Watermark}));
    manifest.channels.push_back(bind("small_data",
                                     ChannelIdValue::SmallData,
                                     ChannelType::Standard,
                                     true,
                                     true,
                                     {PacketType::PayloadAck},
                                     {PacketType::PayloadAck}));
    manifest.channels.push_back(bind("second_screen",
                                     ChannelIdValue::DesktopSecondScreen,
                                     ChannelType::Video,
                                     false,
                                     false,
                                     {PacketType::Watermark},
                                     {PacketType::Video, PacketType::CursorChange, PacketType::Watermark}));
    return manifest;
}

ModuleManifest displayScreenClient()
{
    ModuleManifest manifest = makeBase("display.screen.client",
                                       "Screen projection client",
                                       "fusiondesk.module.display",
                                       protocol::feature::Screen,
                                       ModuleRoleClient,
                                       inProcessHosted);
    manifest.compatiblePeers = {compatiblePeer("display.screen.agent")};
    manifest.channels.push_back(bind("main_screen",
                                     ChannelIdValue::DesktopScreen,
                                     ChannelType::Video,
                                     true,
                                     false,
                                     {PacketType::Video, PacketType::CursorChange, PacketType::Watermark},
                                     {}));
    manifest.channels.push_back(bind("small_data",
                                     ChannelIdValue::SmallData,
                                     ChannelType::Standard,
                                     true,
                                     true,
                                     {PacketType::PayloadAck},
                                     {PacketType::PayloadAck}));
    manifest.channels.push_back(bind("second_screen",
                                     ChannelIdValue::DesktopSecondScreen,
                                     ChannelType::Video,
                                     false,
                                     false,
                                     {PacketType::Video, PacketType::CursorChange, PacketType::Watermark},
                                     {}));
    return manifest;
}

std::vector<ModuleManifest> displayScreenRoleManifests()
{
    return {displayScreenAgent(), displayScreenClient()};
}

ModuleManifest clipboardRedirect()
{
    return makeBase("clipboard.redirect",
                    "Clipboard redirection capability",
                    "fusiondesk.module.clipboard",
                    protocol::feature::Clipboard,
                    clientAgent,
                    inProcessHosted);
}

ModuleManifest clipboardRedirectAgent()
{
    ModuleManifest manifest = makeBase("clipboard.redirect.agent",
                                       "Clipboard redirection agent",
                                       "fusiondesk.module.clipboard",
                                       protocol::feature::Clipboard,
                                       ModuleRoleAgent,
                                       inProcessHosted);
    manifest.compatiblePeers = {compatiblePeer("clipboard.redirect.client")};
    manifest.channels.push_back(bind("small_data",
                                     ChannelIdValue::SmallData,
                                     ChannelType::Standard,
                                     true,
                                     true,
                                     {PacketType::Clipboard},
                                     {PacketType::Clipboard}));
    manifest.channels.push_back(bind("large_data",
                                     ChannelIdValue::LargeData,
                                     ChannelType::Standard,
                                     false,
                                     true,
                                     {PacketType::Clipboard},
                                     {PacketType::Clipboard}));
    return manifest;
}

ModuleManifest clipboardRedirectClient()
{
    ModuleManifest manifest = makeBase("clipboard.redirect.client",
                                       "Clipboard redirection client",
                                       "fusiondesk.module.clipboard",
                                       protocol::feature::Clipboard,
                                       ModuleRoleClient,
                                       inProcessHosted);
    manifest.compatiblePeers = {compatiblePeer("clipboard.redirect.agent")};
    manifest.channels.push_back(bind("small_data",
                                     ChannelIdValue::SmallData,
                                     ChannelType::Standard,
                                     true,
                                     true,
                                     {PacketType::Clipboard},
                                     {PacketType::Clipboard}));
    manifest.channels.push_back(bind("large_data",
                                     ChannelIdValue::LargeData,
                                     ChannelType::Standard,
                                     false,
                                     true,
                                     {PacketType::Clipboard},
                                     {PacketType::Clipboard}));
    return manifest;
}

std::vector<ModuleManifest> clipboardRedirectRoleManifests()
{
    return {clipboardRedirectAgent(), clipboardRedirectClient()};
}

ModuleManifest desktopAudio()
{
    ModuleManifest manifest = makeBase("audio.desktop",
                                       "Desktop audio redirection",
                                       "fusiondesk.module.audio.desktop",
                                       protocol::feature::Audio,
                                       clientAgent,
                                       inProcessHosted);
    std::vector<PacketType> types = {PacketType::Audio};
    manifest.channels.push_back(bind("audio", ChannelIdValue::DesktopAudio, ChannelType::Audio, true, false, types, types));
    return manifest;
}

ModuleManifest microphone()
{
    ModuleManifest manifest = makeBase("audio.microphone",
                                       "Microphone redirection",
                                       "fusiondesk.module.audio.microphone",
                                       protocol::feature::Microphone,
                                       clientAgent,
                                       inProcessHosted);
    std::vector<PacketType> types = {PacketType::Microphone};
    manifest.channels.push_back(bind("microphone", ChannelIdValue::Microphone, ChannelType::Standard, true, false, types, types));
    return manifest;
}

ModuleManifest camera()
{
    ModuleManifest manifest = makeBase("camera.redirect",
                                       "Camera redirection",
                                       "fusiondesk.module.camera",
                                       protocol::feature::Camera,
                                       clientAgent,
                                       inProcessHosted);
    std::vector<PacketType> types = {PacketType::Exchange};
    manifest.channels.push_back(bind("camera", ChannelIdValue::Camera, ChannelType::Standard, true, false, types, types));
    return manifest;
}

ModuleManifest filesystem()
{
    ModuleManifest manifest = makeBase("filesystem.redirect",
                                       "Filesystem redirection",
                                       "fusiondesk.module.filesystem",
                                       protocol::feature::Filesystem,
                                       clientAgent,
                                       inProcessHostedStandalone);
    std::vector<PacketType> types = {PacketType::Filesystem, PacketType::FilesystemControl, PacketType::FilesystemIrp};
    manifest.channels.push_back(bind("large_data", ChannelIdValue::LargeData, ChannelType::Standard, true, true, types, types));
    manifest.channels.push_back(bind("filesystem", ChannelIdValue::Filesystem, ChannelType::Standard, false, false, types, types));
    return manifest;
}

ModuleManifest printer()
{
    ModuleManifest manifest = makeBase("printer.redirect",
                                       "Printer redirection",
                                       "fusiondesk.module.printer",
                                       protocol::feature::Printer,
                                       clientAgent,
                                       inProcessHostedStandalone);
    std::vector<PacketType> types = {PacketType::Printer};
    manifest.channels.push_back(bind("large_data", ChannelIdValue::LargeData, ChannelType::Standard, true, true, types, types));
    manifest.channels.push_back(bind("printer", ChannelIdValue::Printer, ChannelType::Standard, false, false, types, types));
    return manifest;
}

ModuleManifest keyboard()
{
    ModuleManifest manifest = makeBase("input.keyboard",
                                       "Keyboard redirection",
                                       "fusiondesk.module.input.keyboard",
                                       protocol::feature::Keyboard,
                                       clientAgent,
                                       inProcessHosted);
    std::vector<PacketType> types = {PacketType::Keyboard};
    manifest.channels.push_back(bind("small_data", ChannelIdValue::SmallData, ChannelType::Standard, true, true, types, types));
    return manifest;
}

ModuleManifest mouse()
{
    ModuleManifest manifest = makeBase("input.mouse",
                                       "Mouse redirection",
                                       "fusiondesk.module.input.mouse",
                                       protocol::feature::Mouse,
                                       clientAgent,
                                       inProcessHosted);
    std::vector<PacketType> types = {PacketType::Mouse, PacketType::CursorChange};
    manifest.channels.push_back(bind("small_data", ChannelIdValue::SmallData, ChannelType::Standard, true, true, types, types));
    return manifest;
}

ModuleManifest touch()
{
    ModuleManifest manifest = makeBase("input.touch",
                                       "Touch redirection",
                                       "fusiondesk.module.input.touch",
                                       protocol::feature::Touch,
                                       clientAgent,
                                       inProcessHosted);
    std::vector<PacketType> types = {PacketType::Touchscreen};
    manifest.channels.push_back(bind("small_data", ChannelIdValue::SmallData, ChannelType::Standard, true, true, types, types));
    return manifest;
}

ModuleManifest gamepad()
{
    ModuleManifest manifest = makeBase("input.gamepad",
                                       "Gamepad redirection",
                                       "fusiondesk.module.input.gamepad",
                                       protocol::feature::Gamepad,
                                       clientAgent,
                                       inProcessHostedStandalone);
    std::vector<PacketType> types = {PacketType::Gamepad};
    manifest.channels.push_back(bind("gamepad", ChannelIdValue::Gamepad, ChannelType::Standard, true, false, types, types));
    return manifest;
}

ModuleManifest peripheralUsb()
{
    ModuleManifest manifest = makeBase("peripheral.usb",
                                       "USB and peripheral redirection",
                                       "fusiondesk.module.peripheral.usb",
                                       protocol::feature::PeripheralUsb,
                                       ModuleRoleClient | ModuleRoleBridge,
                                       ModuleRunModeProcessOut | ModuleRunModeHosted | ModuleRunModeStandalone);
    manifest.requiredModules.push_back("peripheral.bridge");
    return manifest;
}

std::vector<ModuleManifest> remoteDesktopSuite()
{
    return {displayScreenAgent(), displayScreenClient(), clipboardRedirectAgent(), clipboardRedirectClient(), desktopAudio(), microphone(), camera(), filesystem(), printer(),
            keyboard(), mouse(), touch(), gamepad(), peripheralUsb()};
}

std::vector<ModuleManifest> remoteDesktopSuiteForRole(std::uint32_t roleFlag)
{
    std::vector<ModuleManifest> selected;
    for (ModuleManifest manifest : remoteDesktopSuite()) {
        if (manifest.roleFlags == 0 || (manifest.roleFlags & roleFlag) != 0)
            selected.push_back(std::move(manifest));
    }
    return selected;
}

std::vector<ModuleManifest> redirectionSuite()
{
    return {clipboardRedirect(), filesystem(), printer(), camera(), desktopAudio(), microphone(), peripheralUsb()};
}

std::vector<ModuleManifest> filesystemOnly()
{
    return {filesystem()};
}

std::vector<ModuleManifest> peripheralSuite()
{
    return {peripheralUsb()};
}

} // namespace catalog
} // namespace module
} // namespace fusiondesk
