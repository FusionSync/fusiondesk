#include <cassert>
#include <string>

#include "fusiondesk/runtime/display/display_color_conversion.h"

using namespace fusiondesk;

namespace {

modules::display::CapturedFrame makeBgraFrame()
{
    modules::display::CapturedFrame frame;
    frame.width = 2;
    frame.height = 2;
    frame.strideBytes = 8;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.pixels = {
        0, 0, 0, 255,
        255, 255, 255, 255,
        255, 0, 0, 255,
        0, 0, 255, 255,
    };
    return frame;
}

void convertsBgraToNv12()
{
    const runtime::display::BgraToNv12Result converted =
        runtime::display::convertBgraToNv12(makeBgraFrame());

    assert(converted.ok);
    assert(converted.frame.width == 2);
    assert(converted.frame.height == 2);
    assert(converted.frame.yStrideBytes == 2);
    assert(converted.frame.uvStrideBytes == 2);
    assert(converted.frame.yPlaneSize() == 4);
    assert(converted.frame.uvPlaneOffset() == 4);
    assert(converted.frame.uvPlaneSize() == 2);
    assert(converted.frame.bytes.size() == 6);

    assert(converted.frame.bytes[0] == 16);
    assert(converted.frame.bytes[1] == 235);
    assert(converted.frame.bytes[2] == 41);
    assert(converted.frame.bytes[3] == 82);
    assert(converted.frame.bytes[4] == 147);
    assert(converted.frame.bytes[5] == 152);
}

runtime::display::Nv12Frame makeNv12Frame()
{
    runtime::display::Nv12Frame frame;
    frame.width = 2;
    frame.height = 2;
    frame.yStrideBytes = 2;
    frame.uvStrideBytes = 2;
    frame.bytes = {
        16, 235,
        81, 145,
        128, 128,
    };
    return frame;
}

void convertsNv12ToBgra()
{
    const runtime::display::Nv12ToBgraResult converted =
        runtime::display::convertNv12ToBgra(makeNv12Frame());

    assert(converted.ok);
    assert(converted.width == 2);
    assert(converted.height == 2);
    assert(converted.strideBytes == 8);
    assert(converted.bytes.size() == 16);

    assert(converted.bytes[0] == 0);
    assert(converted.bytes[1] == 0);
    assert(converted.bytes[2] == 0);
    assert(converted.bytes[3] == 255);
    assert(converted.bytes[4] == 255);
    assert(converted.bytes[5] == 255);
    assert(converted.bytes[6] == 255);
    assert(converted.bytes[7] == 255);
    assert(converted.bytes[8] == 76);
    assert(converted.bytes[9] == 76);
    assert(converted.bytes[10] == 76);
    assert(converted.bytes[11] == 255);
    assert(converted.bytes[12] == 150);
    assert(converted.bytes[13] == 150);
    assert(converted.bytes[14] == 150);
    assert(converted.bytes[15] == 255);
}

void rejectsUnsupportedInput()
{
    modules::display::CapturedFrame frame = makeBgraFrame();
    frame.pixelFormat = modules::display::DisplayPixelFormat::Rgba32;
    runtime::display::BgraToNv12Result converted =
        runtime::display::convertBgraToNv12(frame);
    assert(!converted.ok);
    assert(converted.error.find("Bgra32") != std::string::npos);

    frame = makeBgraFrame();
    frame.width = 3;
    converted = runtime::display::convertBgraToNv12(frame);
    assert(!converted.ok);
    assert(converted.error.find("even dimensions") != std::string::npos);

    frame = makeBgraFrame();
    frame.strideBytes = 4;
    converted = runtime::display::convertBgraToNv12(frame);
    assert(!converted.ok);
    assert(converted.error.find("stride") != std::string::npos);

    frame = makeBgraFrame();
    frame.pixels.pop_back();
    converted = runtime::display::convertBgraToNv12(frame);
    assert(!converted.ok);
    assert(converted.error.find("truncated") != std::string::npos);

    runtime::display::Nv12Frame nv12 = makeNv12Frame();
    nv12.width = 3;
    const runtime::display::Nv12ToBgraResult odd =
        runtime::display::convertNv12ToBgra(nv12);
    assert(!odd.ok);
    assert(odd.error.find("even dimensions") != std::string::npos);

    nv12 = makeNv12Frame();
    nv12.yStrideBytes = 1;
    const runtime::display::Nv12ToBgraResult shortStride =
        runtime::display::convertNv12ToBgra(nv12);
    assert(!shortStride.ok);
    assert(shortStride.error.find("stride") != std::string::npos);

    nv12 = makeNv12Frame();
    nv12.bytes.pop_back();
    const runtime::display::Nv12ToBgraResult truncated =
        runtime::display::convertNv12ToBgra(nv12);
    assert(!truncated.ok);
    assert(truncated.error.find("truncated") != std::string::npos);
}

} // namespace

int main()
{
    convertsBgraToNv12();
    convertsNv12ToBgra();
    rejectsUnsupportedInput();
    return 0;
}
