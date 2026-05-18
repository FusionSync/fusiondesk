#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/runtime/display/display_codec_selection.h"

using namespace fusiondesk;

namespace {

runtime::display::DisplayCodecCapability h264Hardware(
    std::string adapterId,
    runtime::display::DisplayPlatformFamily platform)
{
    runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform = platform;
    capability.backend =
        runtime::display::DisplayCodecBackendKind::WindowsMediaFoundation;
    capability.codec = runtime::display::DisplayCodecId::H264;
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::D3DTexture,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::D3DTexture,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.hardwareAccelerated = true;
    capability.zeroCopy = true;
    capability.lowLatency = true;
    capability.requiresHardwareDevice = true;
    capability.maxWidth = 4096;
    capability.maxHeight = 4096;
    capability.priority = 90;
    return capability;
}

runtime::display::DisplayCodecCapability rawSoftware(
    std::string adapterId,
    runtime::display::DisplayPlatformFamily platform)
{
    runtime::display::DisplayCodecCapability capability;
    capability.adapterId = std::move(adapterId);
    capability.platform = platform;
    capability.backend = runtime::display::DisplayCodecBackendKind::RawFrame;
    capability.codec = runtime::display::DisplayCodecId::RawBgra;
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.fallback = true;
    capability.lowLatency = true;
    capability.priority = 5;
    return capability;
}

void windowsDefaultsSelectRawUntilCodecAdapterIsLinked()
{
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = runtime::display::DisplayCodecDirection::Encode;
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::H265,
                               runtime::display::DisplayCodecId::RawBgra};

    const runtime::display::DisplayCodecSelectionResult selected =
        runtime::display::selectDisplayCodec(request);

    assert(selected.ok);
    assert(selected.hasSelection);
    assert(selected.selected.codec == runtime::display::DisplayCodecId::RawBgra);
    assert(selected.selected.backend ==
           runtime::display::DisplayCodecBackendKind::RawFrame);
    assert(selected.fallbackSelected);

    bool sawUnavailableProductionCodec = false;
    for (const runtime::display::DisplayCodecRejection& rejection :
         selected.rejected) {
        if (rejection.reason ==
            "codec adapter is unavailable: codec adapter not linked")
            sawUnavailableProductionCodec = true;
    }
    assert(sawUnavailableProductionCodec);
    assert(std::string(runtime::display::displayCodecIdName(
               selected.selected.codec)) == "raw_bgra");
}

void injectedHardwareH264BeatsRawFallback()
{
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = runtime::display::DisplayCodecDirection::Encode;
    request.codecPreference = {runtime::display::DisplayCodecId::H264,
                               runtime::display::DisplayCodecId::RawBgra};
    request.acceptedInputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::D3DTexture,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    request.acceptedOutputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    request.candidates = {
        rawSoftware("windows.raw_frame",
                    runtime::display::DisplayPlatformFamily::WindowsDesktop),
        h264Hardware("windows.media_foundation.h264",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop)};

    const runtime::display::DisplayCodecSelectionResult selected =
        runtime::display::selectDisplayCodec(request);

    assert(selected.ok);
    assert(selected.selected.codec == runtime::display::DisplayCodecId::H264);
    assert(selected.selected.hardwareAccelerated);
    assert(selected.selected.zeroCopy);
    assert(!selected.fallbackSelected);
    assert(std::string(runtime::display::displayCodecBackendKindName(
               selected.selected.backend)) == "windows.media_foundation");
}

void softwareDisallowBlocksRawFallback()
{
    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = runtime::display::DisplayCodecDirection::Encode;
    request.codecPreference = {runtime::display::DisplayCodecId::RawBgra};
    request.allowSoftware = false;

    const runtime::display::DisplayCodecSelectionResult selected =
        runtime::display::selectDisplayCodec(request);

    assert(!selected.ok);
    bool sawSoftwareReject = false;
    for (const runtime::display::DisplayCodecRejection& rejection :
         selected.rejected) {
        if (rejection.reason == "software codec adapters are disabled")
            sawSoftwareReject = true;
    }
    assert(sawSoftwareReject);
}

void decoderPrefersZeroCopyOutputWhenAvailable()
{
    runtime::display::DisplayCodecCapability software;
    software.adapterId = "software.h264";
    software.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    software.backend = runtime::display::DisplayCodecBackendKind::Software;
    software.codec = runtime::display::DisplayCodecId::H264;
    software.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    software.inputMemoryTypes = {runtime::display::DisplayCodecMemoryType::CpuBuffer};
    software.outputMemoryTypes = {runtime::display::DisplayCodecMemoryType::CpuBuffer};
    software.supportsDecode = true;
    software.lowLatency = true;
    software.priority = 50;

    runtime::display::DisplayCodecCapability hardware =
        h264Hardware("windows.media_foundation.h264",
                     runtime::display::DisplayPlatformFamily::WindowsDesktop);
    hardware.supportsEncode = false;
    hardware.priority = 55;

    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = runtime::display::DisplayCodecDirection::Decode;
    request.codecPreference = {runtime::display::DisplayCodecId::H264};
    request.acceptedInputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    request.acceptedOutputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::D3DTexture,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    request.candidates = {software, hardware};

    const runtime::display::DisplayCodecSelectionResult selected =
        runtime::display::selectDisplayCodec(request);

    assert(selected.ok);
    assert(selected.selected.adapterId == "windows.media_foundation.h264");
    assert(selected.selected.zeroCopy);
}

void rockchipMppRespectsArchitectureAndSoc()
{
    runtime::display::DisplayCodecCapability mpp;
    mpp.adapterId = "linux.rockchip.mpp.h265";
    mpp.platform = runtime::display::DisplayPlatformFamily::RockchipLinux;
    mpp.backend = runtime::display::DisplayCodecBackendKind::RockchipMpp;
    mpp.codec = runtime::display::DisplayCodecId::H265;
    mpp.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    mpp.inputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::DmaBuf,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    mpp.outputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::DmaBuf,
        runtime::display::DisplayCodecMemoryType::CpuBuffer};
    mpp.architectures = {runtime::display::DisplayTargetArchitecture::Arm64};
    mpp.socProfiles = {runtime::display::DisplayTargetSocProfile::Rockchip3568,
                       runtime::display::DisplayTargetSocProfile::Rockchip3588};
    mpp.supportsEncode = true;
    mpp.supportsDecode = true;
    mpp.hardwareAccelerated = true;
    mpp.zeroCopy = true;
    mpp.lowLatency = true;
    mpp.priority = 95;

    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::RockchipLinux;
    request.direction = runtime::display::DisplayCodecDirection::Encode;
    request.codecPreference = {runtime::display::DisplayCodecId::H265};
    request.acceptedInputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::DmaBuf};
    request.acceptedOutputMemoryTypes = {
        runtime::display::DisplayCodecMemoryType::CpuBuffer,
        runtime::display::DisplayCodecMemoryType::DmaBuf};
    request.architecture = runtime::display::DisplayTargetArchitecture::Arm64;
    request.socProfile = runtime::display::DisplayTargetSocProfile::Rockchip3588;
    request.candidates = {mpp};

    runtime::display::DisplayCodecSelectionResult selected =
        runtime::display::selectDisplayCodec(request);
    assert(selected.ok);
    assert(selected.selected.backend ==
           runtime::display::DisplayCodecBackendKind::RockchipMpp);
    assert(std::string(runtime::display::displayCodecBackendKindName(
               selected.selected.backend)) == "rockchip.mpp");

    request.architecture = runtime::display::DisplayTargetArchitecture::Mips64El;
    selected = runtime::display::selectDisplayCodec(request);
    assert(!selected.ok);

    bool sawArchitectureReject = false;
    for (const runtime::display::DisplayCodecRejection& rejection :
         selected.rejected) {
        if (rejection.reason ==
            "codec adapter does not support requested architecture")
            sawArchitectureReject = true;
    }
    assert(sawArchitectureReject);
}

void parsingAndInvalidRequests()
{
    assert(runtime::display::parseDisplayCodecId("raw") ==
           runtime::display::DisplayCodecId::RawBgra);
    assert(runtime::display::parseDisplayCodecId("avc") ==
           runtime::display::DisplayCodecId::H264);
    assert(runtime::display::parseDisplayCodecId("hevc") ==
           runtime::display::DisplayCodecId::H265);
    assert(runtime::display::parseDisplayCodecId("av1") ==
           runtime::display::DisplayCodecId::Av1);
    assert(runtime::display::parseDisplayCodecId("missing") ==
           runtime::display::DisplayCodecId::Unknown);
    assert(std::string(runtime::display::displayCodecDirectionName(
               runtime::display::DisplayCodecDirection::Decode)) == "decode");

    runtime::display::DisplayCodecSelectionRequest request;
    request.platform = runtime::display::DisplayPlatformFamily::WindowsDesktop;
    request.direction = runtime::display::DisplayCodecDirection::Unknown;

    const runtime::display::DisplayCodecSelectionResult selected =
        runtime::display::selectDisplayCodec(request);
    assert(!selected.ok);
    assert(!selected.messages.empty());
}

} // namespace

int main()
{
    windowsDefaultsSelectRawUntilCodecAdapterIsLinked();
    injectedHardwareH264BeatsRawFallback();
    softwareDisallowBlocksRawFallback();
    decoderPrefersZeroCopyOutputWhenAvailable();
    rockchipMppRespectsArchitectureAndSoc();
    parsingAndInvalidRequests();
    return 0;
}
