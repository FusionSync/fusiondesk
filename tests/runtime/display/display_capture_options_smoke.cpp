#include <cassert>
#include <string>

#include "fusiondesk/runtime/display/display_capture_geometry.h"
#include "fusiondesk/runtime/display/display_capture_options.h"

using fusiondesk::modules::display::DisplayCaptureOpenOptions;
using fusiondesk::modules::display::DisplayCaptureSourceType;
using fusiondesk::modules::display::DisplayCaptureStatusCode;
using fusiondesk::modules::display::DisplayPixelFormat;
using fusiondesk::modules::display::DisplayScaleMode;
using fusiondesk::modules::display::displayCaptureStatusCodeName;
using fusiondesk::runtime::display::DefaultRawFrameCaptureTargetHeight;
using fusiondesk::runtime::display::DefaultRawFrameCaptureTargetWidth;
using fusiondesk::runtime::display::DisplayCaptureOutputSize;
using fusiondesk::runtime::display::resolveDisplayCaptureOutputSize;
using fusiondesk::runtime::display::withDefaultRawFrameCaptureTarget;

namespace {

void defaultTargetAppliedForFitRawFrames()
{
    DisplayCaptureOpenOptions options;
    options.sourceId = 3;
    options.sourceType = DisplayCaptureSourceType::Window;
    options.nativeSourceHandle = 0x1234;
    options.scaleMode = DisplayScaleMode::Fit;
    options.preferredPixelFormat = DisplayPixelFormat::Bgra32;

    const DisplayCaptureOpenOptions normalized =
        withDefaultRawFrameCaptureTarget(options);

    assert(normalized.sourceId == 3);
    assert(normalized.sourceType == DisplayCaptureSourceType::Window);
    assert(normalized.nativeSourceHandle == 0x1234);
    assert(normalized.targetWidth == DefaultRawFrameCaptureTargetWidth);
    assert(normalized.targetHeight == DefaultRawFrameCaptureTargetHeight);
    assert(normalized.scaleMode == DisplayScaleMode::Fit);
    assert(normalized.preferredPixelFormat == DisplayPixelFormat::Bgra32);
}

void sourceScaleKeepsSourceSize()
{
    DisplayCaptureOpenOptions options;
    options.scaleMode = DisplayScaleMode::Source;

    const DisplayCaptureOpenOptions normalized =
        withDefaultRawFrameCaptureTarget(options);

    assert(normalized.targetWidth == 0);
    assert(normalized.targetHeight == 0);
    assert(normalized.scaleMode == DisplayScaleMode::Source);
}

void explicitTargetIsPreserved()
{
    DisplayCaptureOpenOptions options;
    options.targetWidth = 960;
    options.targetHeight = 540;
    options.scaleMode = DisplayScaleMode::Fit;

    const DisplayCaptureOpenOptions normalized =
        withDefaultRawFrameCaptureTarget(options);

    assert(normalized.targetWidth == 960);
    assert(normalized.targetHeight == 540);
}

void partialTargetIsPreserved()
{
    DisplayCaptureOpenOptions options;
    options.targetWidth = 1024;
    options.scaleMode = DisplayScaleMode::Fit;

    const DisplayCaptureOpenOptions normalized =
        withDefaultRawFrameCaptureTarget(options);

    assert(normalized.targetWidth == 1024);
    assert(normalized.targetHeight == 0);
}

void captureStatusNamesAreStable()
{
    assert(std::string(displayCaptureStatusCodeName(DisplayCaptureStatusCode::Ok)) == "Ok");
    assert(std::string(displayCaptureStatusCodeName(DisplayCaptureStatusCode::NotOpen)) == "NotOpen");
    assert(std::string(displayCaptureStatusCodeName(DisplayCaptureStatusCode::DeviceLost)) == "DeviceLost");
    assert(std::string(displayCaptureStatusCodeName(
               static_cast<DisplayCaptureStatusCode>(999))) == "Unknown");
}

void outputSizeSourceModeKeepsSourceGeometry()
{
    DisplayCaptureOpenOptions options;
    options.targetWidth = 1280;
    options.targetHeight = 720;
    options.scaleMode = DisplayScaleMode::Source;

    const DisplayCaptureOutputSize size =
        resolveDisplayCaptureOutputSize(2560, 1440, options);

    assert(size.width == 2560);
    assert(size.height == 1440);
}

void outputSizeStretchUsesRequestedDimensions()
{
    DisplayCaptureOpenOptions options;
    options.targetWidth = 640;
    options.targetHeight = 480;
    options.scaleMode = DisplayScaleMode::Stretch;

    const DisplayCaptureOutputSize size =
        resolveDisplayCaptureOutputSize(2560, 1440, options);

    assert(size.width == 640);
    assert(size.height == 480);
}

void outputSizeFitPreservesAspectRatio()
{
    DisplayCaptureOpenOptions options;
    options.targetWidth = 1280;
    options.targetHeight = 720;
    options.scaleMode = DisplayScaleMode::Fit;

    DisplayCaptureOutputSize size =
        resolveDisplayCaptureOutputSize(2560, 1440, options);
    assert(size.width == 1280);
    assert(size.height == 720);

    size = resolveDisplayCaptureOutputSize(1600, 1200, options);
    assert(size.width == 960);
    assert(size.height == 720);
}

void outputSizePartialTargetPreservesAspectRatio()
{
    DisplayCaptureOpenOptions options;
    options.targetWidth = 1024;
    options.scaleMode = DisplayScaleMode::Fit;

    DisplayCaptureOutputSize size =
        resolveDisplayCaptureOutputSize(2048, 1536, options);
    assert(size.width == 1024);
    assert(size.height == 768);

    options.targetWidth = 0;
    options.targetHeight = 360;
    size = resolveDisplayCaptureOutputSize(1920, 1080, options);
    assert(size.width == 640);
    assert(size.height == 360);
}

} // namespace

int main()
{
    defaultTargetAppliedForFitRawFrames();
    sourceScaleKeepsSourceSize();
    explicitTargetIsPreserved();
    partialTargetIsPreserved();
    captureStatusNamesAreStable();
    outputSizeSourceModeKeepsSourceGeometry();
    outputSizeStretchUsesRequestedDimensions();
    outputSizeFitPreservesAspectRatio();
    outputSizePartialTargetPreservesAspectRatio();
    return 0;
}
