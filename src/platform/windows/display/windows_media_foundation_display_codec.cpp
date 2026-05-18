#include "fusiondesk/platform/windows/display/windows_media_foundation_display_codec.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/modules/display/display_encoded_video_payload.h"
#include "fusiondesk/runtime/display/display_color_conversion.h"

#define WIN32_LEAN_AND_MEAN
#include <codecapi.h>
#include <icodecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <objbase.h>
#include <oleauto.h>
#include <wrl/client.h>

namespace fusiondesk {
namespace platform {
namespace windows {
namespace display {

namespace {

using runtime::display::DisplayCodecBackendKind;
using runtime::display::DisplayCodecCapability;
using runtime::display::DisplayCodecId;
using runtime::display::DisplayCodecMemoryType;
using runtime::display::DisplayPlatformFamily;
using runtime::display::BgraToNv12Result;
using runtime::display::Nv12ToBgraResult;

bool envEnabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

WindowsMediaFoundationDisplayCodecPolicy defaultDisplayCodecPolicy()
{
    WindowsMediaFoundationDisplayCodecPolicy policy;
    policy.rolloutEnabled = envEnabled("FUSIONDESK_ENABLE_MF_CODEC") ||
                            envEnabled("FUSIONDESK_SELECT_MF_H264") ||
                            envEnabled("FUSIONDESK_ENABLE_MF_H264_PRODUCTION");
    policy.selectable = envEnabled("FUSIONDESK_SELECT_MF_H264") ||
                        envEnabled("FUSIONDESK_ENABLE_MF_H264_PRODUCTION");
    policy.pFrameEnabled = envEnabled("FUSIONDESK_MF_H264_PFRAME") ||
                           envEnabled("FUSIONDESK_ENABLE_MF_H264_PRODUCTION");
    policy.selectionMode =
        envEnabled("FUSIONDESK_ENABLE_MF_H264_PRODUCTION")
            ? "production"
            : (envEnabled("FUSIONDESK_SELECT_MF_H264")
                   ? "validation"
                   : "direct");
    return policy;
}

struct ComApartment
{
    HRESULT hr = S_OK;
    bool initialized = false;

    ComApartment()
    {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE)
            hr = S_OK;
    }

    ~ComApartment()
    {
        if (initialized)
            CoUninitialize();
    }
};

struct MediaFoundationLifetime
{
    HRESULT hr = S_OK;
    bool started = false;

    MediaFoundationLifetime()
    {
        hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        started = SUCCEEDED(hr);
    }

    ~MediaFoundationLifetime()
    {
        if (started)
            MFShutdown();
    }
};

std::string hresultText(HRESULT hr)
{
    char buffer[32] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "HRESULT=0x%08lX",
                  static_cast<unsigned long>(hr));
    return buffer;
}

std::string boolText(bool value)
{
    return value ? "1" : "0";
}

LONGLONG sampleTimeFromUsec(std::uint64_t monotonicTimestampUsec,
                            std::uint64_t frameId)
{
    constexpr LONGLONG frameDuration100ns = 10000000LL / 30LL;
    const std::uint64_t maxUsec =
        static_cast<std::uint64_t>(
            (std::numeric_limits<LONGLONG>::max)() / 10);
    if (monotonicTimestampUsec != 0 && monotonicTimestampUsec <= maxUsec)
        return static_cast<LONGLONG>(monotonicTimestampUsec * 10ULL);
    return static_cast<LONGLONG>(frameId) * frameDuration100ns;
}

void traceMfCodec(const std::string& message)
{
    if (envEnabled("FUSIONDESK_TRACE_MF_CODEC")) {
        std::printf("MF codec trace: %s\n", message.c_str());
        std::fflush(stdout);
    }
}

std::string annexBNalTypes(const protocol::ByteBuffer& bytes)
{
    std::string result;
    for (std::size_t index = 0; index + 4 < bytes.size(); ++index) {
        std::size_t startCodeBytes = 0;
        if (bytes[index] == 0 && bytes[index + 1] == 0 &&
            bytes[index + 2] == 1) {
            startCodeBytes = 3;
        } else if (index + 5 < bytes.size() && bytes[index] == 0 &&
                   bytes[index + 1] == 0 && bytes[index + 2] == 0 &&
                   bytes[index + 3] == 1) {
            startCodeBytes = 4;
        }

        if (startCodeBytes == 0)
            continue;

        const std::size_t nalOffset = index + startCodeBytes;
        if (nalOffset >= bytes.size())
            break;
        const int nalType = static_cast<int>(bytes[nalOffset] & 0x1fU);
        if (!result.empty())
            result += ",";
        result += std::to_string(nalType);
        index = nalOffset;
    }
    return result.empty() ? "none" : result;
}

bool setCodecApiValueIfSupported(IMFTransform* transform,
                                 const GUID& key,
                                 VARIANT& value)
{
    if (transform == nullptr)
        return false;

    Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
    HRESULT hr = transform->QueryInterface(
        IID_PPV_ARGS(codecApi.GetAddressOf()));
    if (FAILED(hr) || !codecApi)
        return false;
    if (FAILED(codecApi->IsSupported(&key)))
        return false;
    if (FAILED(codecApi->IsModifiable(&key)))
        return false;
    return SUCCEEDED(codecApi->SetValue(&key, &value));
}

bool setCodecApiBoolIfSupported(IMFTransform* transform,
                                const GUID& key,
                                bool enabled)
{
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BOOL;
    value.boolVal = enabled ? VARIANT_TRUE : VARIANT_FALSE;
    const bool ok = setCodecApiValueIfSupported(transform, key, value);
    VariantClear(&value);
    return ok;
}

bool setCodecApiUInt32IfSupported(IMFTransform* transform,
                                  const GUID& key,
                                  std::uint32_t number)
{
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_UI4;
    value.ulVal = number;
    const bool ok = setCodecApiValueIfSupported(transform, key, value);
    VariantClear(&value);
    return ok;
}

void configureH264EncoderRealtimeMode(IMFTransform* encoder,
                                      bool pFrameEnabled)
{
    const bool lowLatency =
        setCodecApiUInt32IfSupported(encoder, CODECAPI_AVLowLatencyMode, 1);
    const bool realtime =
        setCodecApiBoolIfSupported(encoder, CODECAPI_AVEncCommonRealTime, true);
    const bool noBFrames =
        setCodecApiUInt32IfSupported(encoder,
                                     CODECAPI_AVEncMPVDefaultBPictureCount,
                                     0);
    bool allIntraGop = false;
    if (!pFrameEnabled) {
        allIntraGop = setCodecApiUInt32IfSupported(encoder,
                                                   CODECAPI_AVEncMPVGOPSize,
                                                   1);
    }
    const bool lowDelayRateControl =
        setCodecApiUInt32IfSupported(
            encoder,
            CODECAPI_AVEncCommonRateControlMode,
            eAVEncCommonRateControlMode_LowDelayVBR);
    const bool speed =
        setCodecApiUInt32IfSupported(encoder,
                                     CODECAPI_AVEncCommonQualityVsSpeed,
                                     100);
    traceMfCodec(std::string("encoder low-latency mode ") +
                 (lowLatency ? "enabled" : "not supported") +
                 " realtime=" + (realtime ? "1" : "0") +
                 " noBFrames=" + (noBFrames ? "1" : "0") +
                 " allIntraGop=" + (allIntraGop ? "1" : "0") +
                 " lowDelayRateControl=" +
                 (lowDelayRateControl ? "1" : "0") +
                 " speed=" + (speed ? "1" : "0"));
}

void configureH264DecoderLowLatencyMode(IMFTransform* decoder)
{
    const bool lowLatency =
        setCodecApiUInt32IfSupported(decoder, CODECAPI_AVLowLatencyMode, 1);
    traceMfCodec(std::string("decoder low-latency mode ") +
                 (lowLatency ? "enabled" : "not supported"));
}

void restartTransformAfterDrain(IMFTransform* transform,
                                const char* label)
{
    if (transform == nullptr)
        return;

    HRESULT hr = transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (FAILED(hr)) {
        traceMfCodec(std::string(label) +
                     " restart after drain failed: " + hresultText(hr));
    }
}

std::string videoSubtypeName(const GUID& subtype)
{
    if (IsEqualGUID(subtype, MFVideoFormat_RGB32))
        return "RGB32";
    if (IsEqualGUID(subtype, MFVideoFormat_ARGB32))
        return "ARGB32";
    if (IsEqualGUID(subtype, MFVideoFormat_NV12))
        return "NV12";
    if (IsEqualGUID(subtype, MFVideoFormat_YV12))
        return "YV12";
    if (IsEqualGUID(subtype, MFVideoFormat_IYUV))
        return "IYUV";
    if (IsEqualGUID(subtype, MFVideoFormat_YUY2))
        return "YUY2";
    return "unknown";
}

bool supportedDecoderOutputSubtype(const GUID& subtype)
{
    return IsEqualGUID(subtype, MFVideoFormat_NV12) ||
           IsEqualGUID(subtype, MFVideoFormat_RGB32) ||
           IsEqualGUID(subtype, MFVideoFormat_ARGB32);
}

std::uint32_t defaultDecoderOutputStride(const GUID& subtype,
                                         std::uint32_t width)
{
    if (IsEqualGUID(subtype, MFVideoFormat_RGB32) ||
        IsEqualGUID(subtype, MFVideoFormat_ARGB32))
        return width * 4U;
    return width;
}

modules::display::CapturedFrame makePreflightBgraFrame(std::uint32_t width,
                                                       std::uint32_t height)
{
    modules::display::CapturedFrame frame;
    frame.frameId = 1;
    frame.keyFrame = true;
    frame.width = width;
    frame.height = height;
    frame.strideBytes = width * 4U;
    frame.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
    frame.monotonicTimestampUsec = 1;
    frame.pixels.resize(static_cast<std::size_t>(frame.strideBytes) * height);

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y) * frame.strideBytes +
                static_cast<std::size_t>(x) * 4U;
            frame.pixels[offset] =
                static_cast<std::uint8_t>((x * 3U + y) & 0xffU);
            frame.pixels[offset + 1] =
                static_cast<std::uint8_t>((x + y * 5U) & 0xffU);
            frame.pixels[offset + 2] =
                static_cast<std::uint8_t>((x * 7U + y * 11U) & 0xffU);
            frame.pixels[offset + 3] = 255;
        }
    }
    return frame;
}

void releaseActivations(IMFActivate** activations, UINT32 count)
{
    if (activations == nullptr)
        return;

    for (UINT32 index = 0; index < count; ++index) {
        if (activations[index] != nullptr)
            activations[index]->Release();
    }
    CoTaskMemFree(activations);
}

bool mftAvailable(GUID category,
                  const MFT_REGISTER_TYPE_INFO* input,
                  const MFT_REGISTER_TYPE_INFO* output,
                  HRESULT* nativeCode)
{
    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    const DWORD flags = MFT_ENUM_FLAG_SYNCMFT |
                        MFT_ENUM_FLAG_ASYNCMFT |
                        MFT_ENUM_FLAG_HARDWARE |
                        MFT_ENUM_FLAG_SORTANDFILTER;
    const HRESULT hr = MFTEnumEx(category,
                                 flags,
                                 input,
                                 output,
                                 &activations,
                                 &count);
    if (nativeCode != nullptr)
        *nativeCode = hr;
    releaseActivations(activations, count);
    return SUCCEEDED(hr) && count > 0;
}

HRESULT createFirstTransform(GUID category,
                             const MFT_REGISTER_TYPE_INFO* input,
                             const MFT_REGISTER_TYPE_INFO* output,
                             Microsoft::WRL::ComPtr<IMFTransform>& transform)
{
    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    const DWORD flags = MFT_ENUM_FLAG_SYNCMFT |
                        MFT_ENUM_FLAG_ASYNCMFT |
                        MFT_ENUM_FLAG_HARDWARE |
                        MFT_ENUM_FLAG_SORTANDFILTER;
    HRESULT hr = MFTEnumEx(category,
                           flags,
                           input,
                           output,
                           &activations,
                           &count);
    if (SUCCEEDED(hr) && count > 0 && activations[0] != nullptr) {
        hr = activations[0]->ActivateObject(
            IID_PPV_ARGS(transform.GetAddressOf()));
    }
    releaseActivations(activations, count);
    if (SUCCEEDED(hr) && transform)
        return S_OK;
    return SUCCEEDED(hr) ? MF_E_TOPO_CODEC_NOT_FOUND : hr;
}

HRESULT setBasicVideoType(IMFMediaType* type,
                          GUID subtype,
                          std::uint32_t width,
                          std::uint32_t height)
{
    if (type == nullptr)
        return E_POINTER;

    HRESULT hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr))
        return hr;
    hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
    if (FAILED(hr))
        return hr;
    hr = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
    if (FAILED(hr))
        return hr;
    hr = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, 30, 1);
    if (FAILED(hr))
        return hr;
    hr = MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr))
        return hr;
    return type->SetUINT32(MF_MT_INTERLACE_MODE,
                           MFVideoInterlace_Progressive);
}

HRESULT createVideoType(GUID subtype,
                        std::uint32_t width,
                        std::uint32_t height,
                        Microsoft::WRL::ComPtr<IMFMediaType>& type)
{
    IMFMediaType* raw = nullptr;
    HRESULT hr = MFCreateMediaType(&raw);
    if (FAILED(hr))
        return hr;

    type.Attach(raw);
    return setBasicVideoType(type.Get(), subtype, width, height);
}

HRESULT setEncoderOutputType(IMFTransform* encoder,
                             std::uint32_t width,
                             std::uint32_t height)
{
    Microsoft::WRL::ComPtr<IMFMediaType> outputType;
    HRESULT hr = createVideoType(MFVideoFormat_H264,
                                 width,
                                 height,
                                 outputType);
    if (FAILED(hr))
        return hr;
    hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, 2000000);
    if (FAILED(hr))
        return hr;
    return encoder->SetOutputType(0, outputType.Get(), 0);
}

HRESULT setEncoderInputType(IMFTransform* encoder,
                            GUID subtype,
                            std::uint32_t width,
                            std::uint32_t height)
{
    Microsoft::WRL::ComPtr<IMFMediaType> inputType;
    HRESULT hr = createVideoType(subtype, width, height, inputType);
    if (FAILED(hr))
        return hr;
    return encoder->SetInputType(0, inputType.Get(), 0);
}

HRESULT setDecoderInputType(IMFTransform* decoder,
                            std::uint32_t width,
                            std::uint32_t height,
                            const protocol::ByteBuffer& sequenceHeader = {})
{
    (void)width;
    (void)height;
    Microsoft::WRL::ComPtr<IMFMediaType> inputType;
    IMFMediaType* raw = nullptr;
    HRESULT hr = MFCreateMediaType(&raw);
    if (FAILED(hr))
        return hr;
    inputType.Attach(raw);
    hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr))
        return hr;
    hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (FAILED(hr))
        return hr;
    if (!sequenceHeader.empty()) {
        hr = inputType->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER,
                                sequenceHeader.data(),
                                static_cast<UINT32>(sequenceHeader.size()));
        if (FAILED(hr))
            return hr;
    }
    return decoder->SetInputType(0, inputType.Get(), 0);
}

HRESULT setDecoderOutputType(IMFTransform* decoder,
                             std::uint32_t width,
                             std::uint32_t height,
                             GUID& selectedOutputSubtype,
                             std::uint32_t& selectedOutputStrideBytes)
{
    selectedOutputSubtype = GUID_NULL;
    selectedOutputStrideBytes = 0;
    HRESULT lastHr = MF_E_INVALIDMEDIATYPE;
    for (DWORD index = 0; index < 16; ++index) {
        Microsoft::WRL::ComPtr<IMFMediaType> available;
        HRESULT hr = decoder->GetOutputAvailableType(
            0,
            index,
            available.GetAddressOf());
        if (hr == MF_E_NO_MORE_TYPES)
            break;
        if (FAILED(hr)) {
            traceMfCodec("decoder output type enumeration failed: " +
                         hresultText(hr));
            break;
        }
        GUID subtype = GUID_NULL;
        hr = available->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (SUCCEEDED(hr)) {
            traceMfCodec("decoder available output subtype[" +
                         std::to_string(index) + "]=" +
                         videoSubtypeName(subtype));
        } else {
            lastHr = hr;
            continue;
        }

        if (!supportedDecoderOutputSubtype(subtype))
            continue;

        hr = decoder->SetOutputType(0, available.Get(), 0);
        if (SUCCEEDED(hr)) {
            UINT32 stride = 0;
            selectedOutputSubtype = subtype;
            if (SUCCEEDED(
                    available->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) &&
                stride > 0) {
                selectedOutputStrideBytes = stride;
            } else {
                selectedOutputStrideBytes =
                    defaultDecoderOutputStride(subtype, width);
            }
            traceMfCodec("decoder selected output subtype=" +
                         videoSubtypeName(subtype) + " stride=" +
                         std::to_string(selectedOutputStrideBytes));
            return S_OK;
        }
        traceMfCodec("decoder output subtype rejected: " +
                     videoSubtypeName(subtype) + " " + hresultText(hr));
        lastHr = hr;
    }
    return lastHr;
}

HRESULT setDecoderOutputType(IMFTransform* decoder,
                             std::uint32_t width,
                             std::uint32_t height)
{
    GUID selectedSubtype = GUID_NULL;
    std::uint32_t selectedStride = 0;
    return setDecoderOutputType(decoder,
                                width,
                                height,
                                selectedSubtype,
                                selectedStride);
}

bool encoderAcceptsInputSubtype(GUID subtype,
                                std::uint32_t width,
                                std::uint32_t height)
{
    MFT_REGISTER_TYPE_INFO h264Video;
    h264Video.guidMajorType = MFMediaType_Video;
    h264Video.guidSubtype = MFVideoFormat_H264;

    Microsoft::WRL::ComPtr<IMFTransform> encoder;
    HRESULT hr = createFirstTransform(MFT_CATEGORY_VIDEO_ENCODER,
                                      nullptr,
                                      &h264Video,
                                      encoder);
    if (FAILED(hr) || !encoder)
        return false;
    hr = setEncoderOutputType(encoder.Get(), width, height);
    if (FAILED(hr))
        return false;
    return SUCCEEDED(setEncoderInputType(encoder.Get(),
                                         subtype,
                                         width,
                                         height));
}

HRESULT createSampleFromBytes(const protocol::ByteBuffer& bytes,
                              LONGLONG sampleTime,
                              LONGLONG sampleDuration,
                              Microsoft::WRL::ComPtr<IMFSample>& sample)
{
    sample.Reset();

    IMFSample* rawSample = nullptr;
    HRESULT hr = MFCreateSample(&rawSample);
    if (FAILED(hr))
        return hr;
    sample.Attach(rawSample);

    IMFMediaBuffer* rawBuffer = nullptr;
    hr = MFCreateMemoryBuffer(static_cast<DWORD>(bytes.size()), &rawBuffer);
    if (FAILED(hr))
        return hr;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    buffer.Attach(rawBuffer);

    BYTE* target = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    hr = buffer->Lock(&target, &maxLength, &currentLength);
    if (FAILED(hr))
        return hr;
    if (bytes.size() > maxLength) {
        buffer->Unlock();
        return MF_E_BUFFERTOOSMALL;
    }
    if (!bytes.empty())
        std::memcpy(target, bytes.data(), bytes.size());
    hr = buffer->Unlock();
    if (FAILED(hr))
        return hr;
    hr = buffer->SetCurrentLength(static_cast<DWORD>(bytes.size()));
    if (FAILED(hr))
        return hr;
    hr = sample->AddBuffer(buffer.Get());
    if (FAILED(hr))
        return hr;
    hr = sample->SetSampleTime(sampleTime);
    if (FAILED(hr))
        return hr;
    return sample->SetSampleDuration(sampleDuration);
}

HRESULT copySampleBytes(IMFSample* sample, protocol::ByteBuffer& output)
{
    if (sample == nullptr)
        return E_POINTER;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
    if (FAILED(hr))
        return hr;

    BYTE* source = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    hr = buffer->Lock(&source, &maxLength, &currentLength);
    if (FAILED(hr))
        return hr;
    output.insert(output.end(), source, source + currentLength);
    return buffer->Unlock();
}

protocol::ByteBuffer readBlobAttribute(IMFAttributes* attributes, REFGUID key)
{
    protocol::ByteBuffer result;
    if (attributes == nullptr)
        return result;

    UINT32 blobSize = 0;
    HRESULT hr = attributes->GetBlobSize(key, &blobSize);
    if (FAILED(hr) || blobSize == 0)
        return result;

    result.resize(blobSize);
    hr = attributes->GetBlob(key, result.data(), blobSize, &blobSize);
    if (FAILED(hr)) {
        result.clear();
        return result;
    }
    result.resize(blobSize);
    return result;
}

protocol::ByteBuffer encoderSequenceHeader(IMFTransform* encoder)
{
    if (encoder == nullptr)
        return {};

    Microsoft::WRL::ComPtr<IMFMediaType> outputType;
    HRESULT hr = encoder->GetOutputCurrentType(0, outputType.GetAddressOf());
    if (FAILED(hr) || !outputType)
        return {};
    return readBlobAttribute(outputType.Get(), MF_MT_MPEG_SEQUENCE_HEADER);
}

HRESULT createOutputSample(IMFTransform* encoder,
                           Microsoft::WRL::ComPtr<IMFSample>& sample)
{
    sample.Reset();

    MFT_OUTPUT_STREAM_INFO streamInfo = {};
    HRESULT hr = encoder->GetOutputStreamInfo(0, &streamInfo);
    if (FAILED(hr))
        return hr;
    if ((streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0)
        return S_OK;

    const DWORD outputBytes =
        streamInfo.cbSize > 0 ? streamInfo.cbSize : 2U * 1024U * 1024U;
    IMFSample* rawSample = nullptr;
    hr = MFCreateSample(&rawSample);
    if (FAILED(hr))
        return hr;
    sample.Attach(rawSample);

    IMFMediaBuffer* rawBuffer = nullptr;
    hr = MFCreateMemoryBuffer(outputBytes, &rawBuffer);
    if (FAILED(hr))
        return hr;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    buffer.Attach(rawBuffer);
    return sample->AddBuffer(buffer.Get());
}

HRESULT createConfiguredH264Encoder(
    std::uint32_t width,
    std::uint32_t height,
    bool pFrameEnabled,
    Microsoft::WRL::ComPtr<IMFTransform>& encoder,
    GUID& selectedInputSubtype,
    bool& encoderMftCreated,
    bool& outputTypeAccepted,
    bool& nv12InputAccepted,
    bool& bgraInputAccepted)
{
    encoder.Reset();
    encoderMftCreated = false;
    outputTypeAccepted = false;
    nv12InputAccepted = false;
    bgraInputAccepted = false;

    MFT_REGISTER_TYPE_INFO h264Video;
    h264Video.guidMajorType = MFMediaType_Video;
    h264Video.guidSubtype = MFVideoFormat_H264;

    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    const DWORD flags = MFT_ENUM_FLAG_SYNCMFT |
                        MFT_ENUM_FLAG_ASYNCMFT |
                        MFT_ENUM_FLAG_HARDWARE |
                        MFT_ENUM_FLAG_SORTANDFILTER;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                           flags,
                           nullptr,
                           &h264Video,
                           &activations,
                           &count);
    if (FAILED(hr)) {
        releaseActivations(activations, count);
        return hr;
    }
    if (count == 0) {
        releaseActivations(activations, count);
        return MF_E_TOPO_CODEC_NOT_FOUND;
    }

    const GUID inputSubtypes[] = {MFVideoFormat_NV12, MFVideoFormat_RGB32};
    HRESULT lastHr = MF_E_INVALIDMEDIATYPE;
    for (UINT32 activationIndex = 0; activationIndex < count;
         ++activationIndex) {
        if (activations[activationIndex] == nullptr)
            continue;

        for (const GUID& subtype : inputSubtypes) {
            Microsoft::WRL::ComPtr<IMFTransform> candidate;
            hr = activations[activationIndex]->ActivateObject(
                IID_PPV_ARGS(candidate.GetAddressOf()));
            if (FAILED(hr) || !candidate) {
                lastHr = hr;
                continue;
            }
            encoderMftCreated = true;
            configureH264EncoderRealtimeMode(candidate.Get(), pFrameEnabled);

            hr = setEncoderOutputType(candidate.Get(), width, height);
            if (FAILED(hr)) {
                lastHr = hr;
                continue;
            }
            outputTypeAccepted = true;

            hr = setEncoderInputType(candidate.Get(), subtype, width, height);
            if (SUCCEEDED(hr)) {
                if (IsEqualGUID(subtype, MFVideoFormat_NV12))
                    nv12InputAccepted = true;
                if (IsEqualGUID(subtype, MFVideoFormat_RGB32))
                    bgraInputAccepted = true;
                selectedInputSubtype = subtype;
                encoder = candidate;
                releaseActivations(activations, count);
                return S_OK;
            }
            lastHr = hr;
        }
    }

    releaseActivations(activations, count);
    return lastHr;
}

HRESULT pullEncoderOutput(IMFTransform* encoder,
                          std::uint32_t width,
                          std::uint32_t height,
                          protocol::ByteBuffer& bitstream,
                          bool& outputProduced)
{
    outputProduced = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        Microsoft::WRL::ComPtr<IMFSample> outputSample;
        HRESULT hr = createOutputSample(encoder, outputSample);
        if (FAILED(hr))
            return hr;

        MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
        outputBuffer.dwStreamID = 0;
        outputBuffer.pSample = outputSample.Get();
        DWORD status = 0;
        hr = encoder->ProcessOutput(0, 1, &outputBuffer, &status);

        Microsoft::WRL::ComPtr<IMFSample> producedByTransform;
        if (outputBuffer.pSample != nullptr &&
            outputBuffer.pSample != outputSample.Get()) {
            producedByTransform.Attach(outputBuffer.pSample);
        }
        if (outputBuffer.pEvents != nullptr)
            outputBuffer.pEvents->Release();

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
            return outputProduced ? S_OK : hr;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            hr = setEncoderOutputType(encoder, width, height);
            if (FAILED(hr))
                return hr;
            continue;
        }
        if (FAILED(hr))
            return hr;

        IMFSample* sampleToCopy =
            outputBuffer.pSample != nullptr ? outputBuffer.pSample
                                            : outputSample.Get();
        if (sampleToCopy != nullptr) {
            hr = copySampleBytes(sampleToCopy, bitstream);
            if (FAILED(hr))
                return hr;
            outputProduced = !bitstream.empty();
        }
    }
    return outputProduced ? S_OK : MF_E_TRANSFORM_STREAM_CHANGE;
}

bool requestEncoderKeyFrame(IMFTransform* encoder)
{
    if (encoder == nullptr)
        return false;

    Microsoft::WRL::ComPtr<ICodecAPI> codecApi;
    HRESULT hr = encoder->QueryInterface(
        IID_PPV_ARGS(codecApi.GetAddressOf()));
    if (FAILED(hr) || !codecApi)
        return false;
    if (FAILED(codecApi->IsSupported(&CODECAPI_AVEncVideoForceKeyFrame)))
        return false;
    if (FAILED(codecApi->IsModifiable(&CODECAPI_AVEncVideoForceKeyFrame)))
        return false;

    VARIANT value;
    VariantInit(&value);
    value.vt = VT_UI4;
    value.ulVal = 1;
    hr = codecApi->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &value);
    VariantClear(&value);
    return SUCCEEDED(hr);
}

HRESULT createConfiguredH264Decoder(
    std::uint32_t width,
    std::uint32_t height,
    const protocol::ByteBuffer& sequenceHeader,
    Microsoft::WRL::ComPtr<IMFTransform>& decoder)
{
    decoder.Reset();

    MFT_REGISTER_TYPE_INFO h264Video;
    h264Video.guidMajorType = MFMediaType_Video;
    h264Video.guidSubtype = MFVideoFormat_H264;

    IMFActivate** activations = nullptr;
    UINT32 count = 0;
    const DWORD flags = MFT_ENUM_FLAG_SYNCMFT |
                        MFT_ENUM_FLAG_ASYNCMFT |
                        MFT_ENUM_FLAG_HARDWARE |
                        MFT_ENUM_FLAG_SORTANDFILTER;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                           flags,
                           &h264Video,
                           nullptr,
                           &activations,
                           &count);
    if (FAILED(hr)) {
        releaseActivations(activations, count);
        return hr;
    }
    if (count == 0) {
        releaseActivations(activations, count);
        return MF_E_TOPO_CODEC_NOT_FOUND;
    }

    HRESULT lastHr = MF_E_INVALIDMEDIATYPE;
    for (UINT32 activationIndex = 0; activationIndex < count;
         ++activationIndex) {
        if (activations[activationIndex] == nullptr)
            continue;

        Microsoft::WRL::ComPtr<IMFTransform> candidate;
        hr = activations[activationIndex]->ActivateObject(
            IID_PPV_ARGS(candidate.GetAddressOf()));
        if (FAILED(hr) || !candidate) {
            lastHr = hr;
            continue;
        }
        configureH264DecoderLowLatencyMode(candidate.Get());

        hr = setDecoderInputType(candidate.Get(),
                                 width,
                                 height,
                                 sequenceHeader);
        if (FAILED(hr)) {
            traceMfCodec("decoder candidate input type rejected: " +
                         hresultText(hr));
            lastHr = hr;
            continue;
        }
        decoder = candidate;
        releaseActivations(activations, count);
        return S_OK;
    }

    releaseActivations(activations, count);
    return lastHr;
}

HRESULT pullDecoderOutput(IMFTransform* decoder,
                          std::uint32_t width,
                          std::uint32_t height,
                          protocol::ByteBuffer& pixels,
                          GUID& selectedOutputSubtype,
                          std::uint32_t& selectedOutputStrideBytes,
                          bool& outputProduced)
{
    outputProduced = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        Microsoft::WRL::ComPtr<IMFSample> outputSample;
        HRESULT hr = createOutputSample(decoder, outputSample);
        if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            hr = setDecoderOutputType(decoder,
                                      width,
                                      height,
                                      selectedOutputSubtype,
                                      selectedOutputStrideBytes);
            if (FAILED(hr))
                return hr;
            continue;
        }
        if (FAILED(hr))
            return hr;

        MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
        outputBuffer.dwStreamID = 0;
        outputBuffer.pSample = outputSample.Get();
        DWORD status = 0;
        hr = decoder->ProcessOutput(0, 1, &outputBuffer, &status);

        Microsoft::WRL::ComPtr<IMFSample> producedByTransform;
        if (outputBuffer.pSample != nullptr &&
            outputBuffer.pSample != outputSample.Get()) {
            producedByTransform.Attach(outputBuffer.pSample);
        }
        if (outputBuffer.pEvents != nullptr)
            outputBuffer.pEvents->Release();

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
            return hr;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE ||
            hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
            hr = setDecoderOutputType(decoder,
                                      width,
                                      height,
                                      selectedOutputSubtype,
                                      selectedOutputStrideBytes);
            if (FAILED(hr))
                return hr;
            continue;
        }
        if (FAILED(hr))
            return hr;

        IMFSample* sampleToCopy =
            outputBuffer.pSample != nullptr ? outputBuffer.pSample
                                            : outputSample.Get();
        if (sampleToCopy != nullptr) {
            hr = copySampleBytes(sampleToCopy, pixels);
            if (FAILED(hr))
                return hr;
            outputProduced = !pixels.empty();
        }
        return S_OK;
    }
    return MF_E_TRANSFORM_STREAM_CHANGE;
}

protocol::ByteBuffer encodeH264AsFdsf(std::uint32_t width,
                                      std::uint32_t height,
                                      std::uint64_t frameId,
                                      bool keyFrame,
                                      std::uint64_t timestampUsec,
                                      const protocol::ByteBuffer& sequenceHeader,
                                      const protocol::ByteBuffer& bitstream)
{
    modules::display::DisplayEncodedVideoPayload payload;
    payload.codec = modules::display::DisplayEncodedVideoCodec::H264;
    payload.bitstreamFormat =
        modules::display::DisplayEncodedVideoBitstreamFormat::AnnexB;
    payload.codedWidth = width;
    payload.codedHeight = height;
    payload.visibleWidth = width;
    payload.visibleHeight = height;
    payload.frame.frameId = frameId;
    payload.frame.keyFrame = keyFrame;
    payload.frame.width = width;
    payload.frame.height = height;
    payload.frame.strideBytes = width * 4U;
    payload.frame.pixelFormat =
        modules::display::DisplayPixelFormat::Bgra32;
    payload.frame.monotonicTimestampUsec = timestampUsec;
    payload.sequenceHeader = sequenceHeader;
    payload.bitstream = bitstream;
    return modules::display::encodeDisplayEncodedVideoPayload(payload);
}

protocol::ByteBuffer encodeH264AsFdsf(
    const modules::display::CapturedFrame& frame,
    const protocol::ByteBuffer& sequenceHeader,
    const protocol::ByteBuffer& bitstream)
{
    return encodeH264AsFdsf(frame.width,
                            frame.height,
                            frame.frameId,
                            frame.keyFrame,
                            frame.monotonicTimestampUsec,
                            sequenceHeader,
                            bitstream);
}

protocol::ByteBuffer packedBgraBytes(
    const modules::display::CapturedFrame& frame)
{
    const std::uint32_t packedStride = frame.width * 4U;
    if (frame.strideBytes == packedStride)
        return frame.pixels;

    protocol::ByteBuffer packed(
        static_cast<std::size_t>(packedStride) * frame.height);
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const std::size_t sourceOffset =
            static_cast<std::size_t>(y) * frame.strideBytes;
        const std::size_t targetOffset =
            static_cast<std::size_t>(y) * packedStride;
        std::memcpy(packed.data() + targetOffset,
                    frame.pixels.data() + sourceOffset,
                    packedStride);
    }
    return packed;
}

protocol::ByteBuffer packedDecoderBgraBytes(const protocol::ByteBuffer& pixels,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            std::uint32_t strideBytes)
{
    const std::uint32_t packedStride = width * 4U;
    const std::uint32_t sourceStride =
        strideBytes == 0 ? packedStride : strideBytes;
    if (sourceStride < packedStride)
        return {};
    const std::uint64_t requiredBytes =
        static_cast<std::uint64_t>(sourceStride) * height;
    if (requiredBytes > pixels.size())
        return {};
    if (sourceStride == packedStride)
        return protocol::ByteBuffer(
            pixels.begin(),
            pixels.begin() +
                static_cast<std::ptrdiff_t>(requiredBytes));

    protocol::ByteBuffer packed(
        static_cast<std::size_t>(packedStride) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t sourceOffset =
            static_cast<std::size_t>(y) * sourceStride;
        const std::size_t targetOffset =
            static_cast<std::size_t>(y) * packedStride;
        std::memcpy(packed.data() + targetOffset,
                    pixels.data() + sourceOffset,
                    packedStride);
    }
    return packed;
}

class WindowsMediaFoundationH264Encoder final
    : public modules::display::IVideoEncoder
{
public:
    explicit WindowsMediaFoundationH264Encoder(
        WindowsMediaFoundationDisplayCodecPolicy policy)
        : policy_(std::move(policy))
    {
    }

    modules::display::DisplayCodecRuntimeInfo codecRuntimeInfo() const override
    {
        modules::display::DisplayCodecRuntimeInfo info;
        info.selected = true;
        info.adapterId = "windows.media_foundation.h264";
        info.codec = "h264";
        info.backend = "windows.media_foundation";
        info.fallback = false;
        info.hardwareAccelerated = false;
        info.zeroCopy = false;
        info.lowLatency = true;
        info.deltaFrames = policy_.pFrameEnabled;
        info.selectionMode = policy_.selectionMode;
        return info;
    }

    modules::display::EncodedFrame encode(
        const modules::display::CapturedFrame& frame) override
    {
        modules::display::EncodedFrame encoded;
        encoded.frameId = frame.frameId;
        encoded.keyFrame = frame.keyFrame;
        encoded.width = frame.width;
        encoded.height = frame.height;
        encoded.strideBytes = frame.width * 4U;
        encoded.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
        encoded.monotonicTimestampUsec = frame.monotonicTimestampUsec;

        if (FAILED(com_.hr) || FAILED(mf_.hr))
            return encoded;
        if (frame.pixelFormat != modules::display::DisplayPixelFormat::Bgra32 ||
            frame.width == 0 || frame.height == 0 ||
            (frame.width % 2U) != 0 || (frame.height % 2U) != 0)
            return encoded;

        const BgraToNv12Result nv12 =
            runtime::display::convertBgraToNv12(frame);
        if (!nv12.ok)
            return encoded;

        if (!ensureEncoder(frame.width, frame.height))
            return encoded;

        bool outputKeyFrame = frame.keyFrame;
        if (!frame.keyFrame && encodedFrames_ > 0 &&
            !policy_.pFrameEnabled) {
            traceMfCodec("encoder recreating for synchronous all-intra frame");
            resetEncoder();
            if (!ensureEncoder(frame.width, frame.height))
                return encoded;
            outputKeyFrame = true;
        }

        if (frame.keyFrame) {
            const bool requested = requestEncoderKeyFrame(encoder_.Get());
            traceMfCodec(std::string("encoder keyframe request ") +
                         (requested ? "accepted" : "not supported"));
            if (!requested && encodedFrames_ > 0) {
                traceMfCodec("encoder recreating for requested keyframe");
                resetEncoder();
                if (!ensureEncoder(frame.width, frame.height))
                    return encoded;
                requestEncoderKeyFrame(encoder_.Get());
            }
            outputKeyFrame = true;
        }

        const protocol::ByteBuffer bgra =
            IsEqualGUID(selectedInputSubtype_, MFVideoFormat_RGB32)
                ? packedBgraBytes(frame)
                : protocol::ByteBuffer();
        const protocol::ByteBuffer& inputBytes =
            IsEqualGUID(selectedInputSubtype_, MFVideoFormat_NV12)
                ? nv12.frame.bytes
                : bgra;

        Microsoft::WRL::ComPtr<IMFSample> inputSample;
        HRESULT hr = createSampleFromBytes(inputBytes,
                                           sampleTimeFromUsec(
                                               frame.monotonicTimestampUsec,
                                               frame.frameId),
                                           10000000LL / 30LL,
                                           inputSample);
        if (SUCCEEDED(hr))
            hr = encoder_->ProcessInput(0, inputSample.Get(), 0);
        if (FAILED(hr)) {
            traceMfCodec("encoder ProcessInput failed: " + hresultText(hr));
            return encoded;
        }

        const HRESULT drainHr =
            encoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        const bool drained = SUCCEEDED(drainHr);
        if (!drained)
            traceMfCodec("encoder drain failed: " + hresultText(drainHr));
        protocol::ByteBuffer bitstream;
        bool outputProduced = false;
        hr = pullEncoderOutput(encoder_.Get(),
                               frame.width,
                               frame.height,
                               bitstream,
                               outputProduced);
        if (drained)
            restartTransformAfterDrain(encoder_.Get(), "encoder");
        if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
            traceMfCodec("encoder ProcessOutput failed: " + hresultText(hr));
            return encoded;
        }
        if (!outputProduced || bitstream.empty()) {
            traceMfCodec("encoder produced no bitstream: " + hresultText(hr));
            return encoded;
        }

        protocol::ByteBuffer sequenceHeader = sequenceHeader_;
        if (sequenceHeader.empty()) {
            sequenceHeader = encoderSequenceHeader(encoder_.Get());
            sequenceHeader_ = sequenceHeader;
        }
        traceMfCodec("encoder output frameId=" +
                     std::to_string(frame.frameId) +
                     " keyFrame=" + boolText(outputKeyFrame) +
                     " bitstreamBytes=" +
                     std::to_string(bitstream.size()) +
                     " sequenceHeaderBytes=" +
                     std::to_string(sequenceHeader.size()) +
                     " nalTypes=" + annexBNalTypes(bitstream));
        encoded.keyFrame = outputKeyFrame;
        encoded.payload = encodeH264AsFdsf(frame.width,
                                           frame.height,
                                           frame.frameId,
                                           outputKeyFrame,
                                           frame.monotonicTimestampUsec,
                                           sequenceHeader,
                                           bitstream);
        ++encodedFrames_;
        return encoded;
    }

private:
    void resetEncoder()
    {
        encoder_.Reset();
        selectedInputSubtype_ = GUID_NULL;
        sequenceHeader_.clear();
        width_ = 0;
        height_ = 0;
        encodedFrames_ = 0;
    }

    bool ensureEncoder(std::uint32_t width, std::uint32_t height)
    {
        if (encoder_ && width_ == width && height_ == height)
            return true;

        resetEncoder();

        bool encoderMftCreated = false;
        bool outputTypeAccepted = false;
        bool nv12InputAccepted = false;
        bool bgraInputAccepted = false;
        HRESULT hr = createConfiguredH264Encoder(width,
                                                 height,
                                                 policy_.pFrameEnabled,
                                                 encoder_,
                                                 selectedInputSubtype_,
                                                 encoderMftCreated,
                                                 outputTypeAccepted,
                                                 nv12InputAccepted,
                                                 bgraInputAccepted);
        if (FAILED(hr) || !encoder_)
            return false;

        hr = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        if (SUCCEEDED(hr))
            hr = encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        if (FAILED(hr)) {
            encoder_.Reset();
            return false;
        }

        width_ = width;
        height_ = height;
        sequenceHeader_ = encoderSequenceHeader(encoder_.Get());
        return true;
    }

    ComApartment com_;
    MediaFoundationLifetime mf_;
    WindowsMediaFoundationDisplayCodecPolicy policy_;
    Microsoft::WRL::ComPtr<IMFTransform> encoder_;
    GUID selectedInputSubtype_ = GUID_NULL;
    protocol::ByteBuffer sequenceHeader_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint64_t encodedFrames_ = 0;
};

class WindowsMediaFoundationH264Decoder final
    : public modules::display::IVideoDecoder
{
public:
    explicit WindowsMediaFoundationH264Decoder(
        WindowsMediaFoundationDisplayCodecPolicy policy)
        : policy_(std::move(policy))
    {
    }

    modules::display::DisplayCodecRuntimeInfo codecRuntimeInfo() const override
    {
        modules::display::DisplayCodecRuntimeInfo info;
        info.selected = true;
        info.adapterId = "windows.media_foundation.h264";
        info.codec = "h264";
        info.backend = "windows.media_foundation";
        info.fallback = false;
        info.hardwareAccelerated = false;
        info.zeroCopy = false;
        info.lowLatency = true;
        info.deltaFrames = policy_.pFrameEnabled;
        info.selectionMode = policy_.selectionMode;
        return info;
    }

    modules::display::DecodedFrame decode(
        const modules::display::EncodedFrame& frame) override
    {
        modules::display::DecodedFrame decoded;
        decoded.decodeStatus = modules::display::DisplayDecodeStatus::Failed;
        if (FAILED(com_.hr) || FAILED(mf_.hr))
            return decoded;

        const modules::display::DisplayEncodedVideoPayloadDecodeResult fdsf =
            modules::display::decodeDisplayEncodedVideoPayload(frame.payload);
        if (!fdsf.ok ||
            fdsf.payload.codec != modules::display::DisplayEncodedVideoCodec::H264 ||
            fdsf.payload.bitstream.empty()) {
            traceMfCodec("decoder rejected FDSF: " + fdsf.error);
            return decoded;
        }

        const std::uint32_t width = fdsf.payload.visibleWidth;
        const std::uint32_t height = fdsf.payload.visibleHeight;
        if (width == 0 || height == 0) {
            traceMfCodec("decoder rejected zero visible size");
            return decoded;
        }
        if (fdsf.payload.frame.keyFrame &&
            !fdsf.payload.sequenceHeader.empty() &&
            decoder_ && decodedFrames_ > 0) {
            traceMfCodec("decoder recreating for keyframe sequence header");
            resetDecoder();
        }
        if (!ensureDecoder(width, height, fdsf.payload.sequenceHeader)) {
            traceMfCodec("decoder ensureDecoder failed");
            return decoded;
        }

        protocol::ByteBuffer inputBitstream = fdsf.payload.bitstream;
        if (!policy_.pFrameEnabled &&
            !fdsf.payload.sequenceHeader.empty() &&
            (decodedFrames_ == 0 || fdsf.payload.frame.keyFrame)) {
            inputBitstream.insert(inputBitstream.begin(),
                                  fdsf.payload.sequenceHeader.begin(),
                                  fdsf.payload.sequenceHeader.end());
        }
        traceMfCodec("decoder input frameId=" +
                     std::to_string(fdsf.payload.frame.frameId) +
                     " keyFrame=" +
                     boolText(fdsf.payload.frame.keyFrame) +
                     " bitstreamBytes=" +
                     std::to_string(fdsf.payload.bitstream.size()) +
                     " sequenceHeaderBytes=" +
                     std::to_string(fdsf.payload.sequenceHeader.size()) +
                     " inputNalTypes=" + annexBNalTypes(inputBitstream));

        Microsoft::WRL::ComPtr<IMFSample> inputSample;
        HRESULT hr = createSampleFromBytes(inputBitstream,
                                           sampleTimeFromUsec(
                                               fdsf.payload.frame.monotonicTimestampUsec,
                                               fdsf.payload.frame.frameId),
                                           10000000LL / 30LL,
                                           inputSample);
        if (SUCCEEDED(hr) && fdsf.payload.frame.keyFrame) {
            inputSample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
        }
        if (SUCCEEDED(hr))
            hr = decoder_->ProcessInput(0, inputSample.Get(), 0);
        if (FAILED(hr)) {
            traceMfCodec("decoder ProcessInput failed: " + hresultText(hr));
            return decoded;
        }
        pendingMetadata_.push_back(fdsf.payload.frame);

        protocol::ByteBuffer pixels;
        bool outputProduced = false;
        hr = pullDecoderOutput(decoder_.Get(),
                               width,
                               height,
                               pixels,
                               selectedOutputSubtype_,
                               selectedOutputStrideBytes_,
                               outputProduced);
        if (!policy_.pFrameEnabled &&
            (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || !outputProduced ||
             pixels.empty())) {
            const HRESULT drainHr =
                decoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
            if (SUCCEEDED(drainHr)) {
                protocol::ByteBuffer drainedPixels;
                bool drainedOutputProduced = false;
                const HRESULT drainedHr =
                    pullDecoderOutput(decoder_.Get(),
                                      width,
                                      height,
                                      drainedPixels,
                                      selectedOutputSubtype_,
                                      selectedOutputStrideBytes_,
                                      drainedOutputProduced);
                if (!policy_.pFrameEnabled)
                    restartTransformAfterDrain(decoder_.Get(), "decoder");
                if (FAILED(drainedHr) &&
                    drainedHr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
                    hr = drainedHr;
                } else if (drainedOutputProduced &&
                           !drainedPixels.empty()) {
                    pixels = std::move(drainedPixels);
                    outputProduced = true;
                    hr = drainedHr;
                }
            } else {
                traceMfCodec("decoder drain failed: " +
                             hresultText(drainHr));
            }
        }
        if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
            traceMfCodec("decoder ProcessOutput failed: " + hresultText(hr));
            return decoded;
        }
        if (!outputProduced || pixels.empty()) {
            traceMfCodec("decoder produced no output: " + hresultText(hr));
            decoded.decodeStatus =
                modules::display::DisplayDecodeStatus::NeedsMoreInput;
            return decoded;
        }

        modules::display::EncodedFrame outputMetadata = fdsf.payload.frame;
        if (!pendingMetadata_.empty()) {
            outputMetadata = pendingMetadata_.front();
            pendingMetadata_.pop_front();
        }

        protocol::ByteBuffer bgraPixels;
        std::uint32_t bgraStrideBytes = width * 4U;
        if (IsEqualGUID(selectedOutputSubtype_, MFVideoFormat_NV12)) {
            runtime::display::Nv12Frame nv12;
            nv12.width = width;
            nv12.height = height;
            nv12.yStrideBytes = selectedOutputStrideBytes_ == 0
                                    ? width
                                    : selectedOutputStrideBytes_;
            nv12.uvStrideBytes = nv12.yStrideBytes;
            nv12.bytes = pixels;
            const Nv12ToBgraResult converted =
                runtime::display::convertNv12ToBgra(nv12);
            if (!converted.ok) {
                traceMfCodec("decoder NV12-to-BGRA conversion failed: " +
                             converted.error);
                return decoded;
            }
            bgraPixels = converted.bytes;
            bgraStrideBytes = converted.strideBytes;
        } else if (IsEqualGUID(selectedOutputSubtype_, MFVideoFormat_RGB32) ||
                   IsEqualGUID(selectedOutputSubtype_, MFVideoFormat_ARGB32)) {
            bgraPixels = packedDecoderBgraBytes(pixels,
                                                width,
                                                height,
                                                selectedOutputStrideBytes_);
            if (bgraPixels.empty()) {
                traceMfCodec("decoder BGRA output packing failed");
                return decoded;
            }
        } else {
            traceMfCodec("decoder selected unsupported output subtype: " +
                         videoSubtypeName(selectedOutputSubtype_));
            return decoded;
        }

        decoded.frameId = outputMetadata.frameId;
        decoded.decodeStatus = modules::display::DisplayDecodeStatus::Ok;
        decoded.keyFrame = outputMetadata.keyFrame;
        decoded.width = width;
        decoded.height = height;
        decoded.strideBytes = bgraStrideBytes;
        decoded.pixelFormat = modules::display::DisplayPixelFormat::Bgra32;
        decoded.monotonicTimestampUsec =
            outputMetadata.monotonicTimestampUsec;
        decoded.pixels = std::move(bgraPixels);
        ++decodedFrames_;
        return decoded;
    }

private:
    void resetDecoder()
    {
        decoder_.Reset();
        selectedOutputSubtype_ = GUID_NULL;
        selectedOutputStrideBytes_ = 0;
        sequenceHeader_.clear();
        pendingMetadata_.clear();
        width_ = 0;
        height_ = 0;
        decodedFrames_ = 0;
    }

    bool ensureDecoder(std::uint32_t width,
                       std::uint32_t height,
                       const protocol::ByteBuffer& sequenceHeader)
    {
        if (decoder_ && width_ == width && height_ == height &&
            sequenceHeader_ == sequenceHeader)
            return true;

        resetDecoder();

        HRESULT hr = createConfiguredH264Decoder(width,
                                                 height,
                                                 sequenceHeader,
                                                 decoder_);
        if (FAILED(hr) || !decoder_) {
            traceMfCodec("createConfiguredH264Decoder failed: " +
                         hresultText(hr) + " sequenceHeaderBytes=" +
                         std::to_string(sequenceHeader.size()));
            return false;
        }

        hr = setDecoderOutputType(decoder_.Get(),
                                  width,
                                  height,
                                  selectedOutputSubtype_,
                                  selectedOutputStrideBytes_);
        if (FAILED(hr)) {
            traceMfCodec("decoder output type setup failed: " +
                         hresultText(hr));
            decoder_.Reset();
            return false;
        }

        hr = decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        if (SUCCEEDED(hr))
            hr = decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        if (FAILED(hr)) {
            traceMfCodec("decoder stream start failed: " + hresultText(hr));
            decoder_.Reset();
            return false;
        }

        width_ = width;
        height_ = height;
        sequenceHeader_ = sequenceHeader;
        return true;
    }

    ComApartment com_;
    MediaFoundationLifetime mf_;
    WindowsMediaFoundationDisplayCodecPolicy policy_;
    Microsoft::WRL::ComPtr<IMFTransform> decoder_;
    GUID selectedOutputSubtype_ = GUID_NULL;
    std::uint32_t selectedOutputStrideBytes_ = 0;
    protocol::ByteBuffer sequenceHeader_;
    std::deque<modules::display::EncodedFrame> pendingMetadata_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint64_t decodedFrames_ = 0;
};

DisplayCodecCapability h264Capability(
    const WindowsMediaFoundationCodecProbeResult& probe,
    const WindowsMediaFoundationDisplayCodecPolicy& policy)
{
    DisplayCodecCapability capability;
    capability.adapterId = "windows.media_foundation.h264";
    capability.platform = DisplayPlatformFamily::WindowsDesktop;
    capability.backend = DisplayCodecBackendKind::WindowsMediaFoundation;
    capability.codec = DisplayCodecId::H264;
    capability.pixelFormats = {modules::display::DisplayPixelFormat::Bgra32};
    capability.inputMemoryTypes = {DisplayCodecMemoryType::CpuBuffer};
    capability.outputMemoryTypes = {DisplayCodecMemoryType::CpuBuffer};
    capability.supportsEncode = true;
    capability.supportsDecode = true;
    capability.available = false;
    capability.fallback = false;
    capability.hardwareAccelerated = false;
    capability.zeroCopy = false;
    capability.lowLatency = true;
    capability.requiresHardwareDevice = false;
    capability.maxWidth = 8192;
    capability.maxHeight = 8192;
    capability.priority = 90;

    if (!probe.rolloutEnabled) {
        capability.unavailableReason =
            "MediaFoundation H.264 codec adapter rollout is not enabled";
    } else if (!probe.mediaFoundationStarted) {
        capability.unavailableReason =
            "MediaFoundation startup failed: " + probe.message;
    } else if (!probe.h264EncoderFound || !probe.h264DecoderFound) {
        capability.unavailableReason =
            "MediaFoundation H.264 MFT probe failed: " + probe.message;
    } else if (policy.selectable) {
        capability.available = true;
    } else {
        capability.unavailableReason =
            "MediaFoundation H.264 codec adapter probe succeeded; "
            "enable the ProductProfile display codec policy for production "
            "or set FUSIONDESK_SELECT_MF_H264=1 for validation";
    }
    return capability;
}

} // namespace

WindowsMediaFoundationCodecProbeResult probeWindowsMediaFoundationH264Codec(
    bool rolloutEnabled)
{
    WindowsMediaFoundationCodecProbeResult result;
    result.rolloutEnabled = rolloutEnabled;
    if (!result.rolloutEnabled) {
        result.message =
            "MediaFoundation H.264 codec adapter rollout is not enabled";
        return result;
    }

    const HRESULT startup = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    result.nativeCode = static_cast<long>(startup);
    if (FAILED(startup)) {
        result.message = hresultText(startup);
        return result;
    }
    result.mediaFoundationStarted = true;

    MFT_REGISTER_TYPE_INFO h264Video;
    h264Video.guidMajorType = MFMediaType_Video;
    h264Video.guidSubtype = MFVideoFormat_H264;

    HRESULT encoderHr = S_OK;
    result.h264EncoderFound = mftAvailable(MFT_CATEGORY_VIDEO_ENCODER,
                                           nullptr,
                                           &h264Video,
                                           &encoderHr);

    HRESULT decoderHr = S_OK;
    result.h264DecoderFound = mftAvailable(MFT_CATEGORY_VIDEO_DECODER,
                                           &h264Video,
                                           nullptr,
                                           &decoderHr);

    MFShutdown();

    if (result.h264EncoderFound && result.h264DecoderFound) {
        result.message =
            "MediaFoundation H.264 encoder and decoder MFTs are present";
    } else {
        result.message = "encoder=" +
                         std::string(result.h264EncoderFound ? "1" : "0") +
                         " decoder=" +
                         std::string(result.h264DecoderFound ? "1" : "0") +
                         " encoderHr=" + hresultText(encoderHr) +
                         " decoderHr=" + hresultText(decoderHr);
    }
    return result;
}

WindowsMediaFoundationCodecProbeResult probeWindowsMediaFoundationH264Codec()
{
    return probeWindowsMediaFoundationH264Codec(
        defaultDisplayCodecPolicy().rolloutEnabled);
}

WindowsMediaFoundationDisplayCodecBackendFactory::
    WindowsMediaFoundationDisplayCodecBackendFactory()
    : policy_(defaultDisplayCodecPolicy())
{
}

WindowsMediaFoundationDisplayCodecBackendFactory::
    WindowsMediaFoundationDisplayCodecBackendFactory(
        WindowsMediaFoundationDisplayCodecPolicy policy)
    : policy_(std::move(policy))
{
    if (policy_.selectionMode.empty())
        policy_.selectionMode = "default";
}

WindowsMediaFoundationH264AdapterPreflightResult
preflightWindowsMediaFoundationH264Adapter(std::uint32_t width,
                                          std::uint32_t height)
{
    WindowsMediaFoundationH264AdapterPreflightResult result;
    result.rolloutEnabled = envEnabled("FUSIONDESK_ENABLE_MF_CODEC");
    if (!result.rolloutEnabled) {
        result.message =
            "MediaFoundation H.264 adapter preflight rollout is not enabled";
        return result;
    }

    if (width == 0 || height == 0) {
        result.message =
            "MediaFoundation H.264 adapter preflight requires a non-zero size";
        return result;
    }
    if ((width % 2U) != 0 || (height % 2U) != 0) {
        result.message =
            "MediaFoundation H.264 adapter preflight requires even dimensions";
        return result;
    }

    const modules::display::CapturedFrame bgra =
        makePreflightBgraFrame(width, height);
    const BgraToNv12Result nv12 = runtime::display::convertBgraToNv12(bgra);
    result.bgraToNv12ConversionOk = nv12.ok;
    if (!nv12.ok) {
        result.message = "BGRA-to-NV12 preflight conversion failed: " +
                         nv12.error;
        return result;
    }
    result.bgraToNv12Bytes =
        static_cast<std::uint64_t>(nv12.frame.bytes.size());
    result.bgraToNv12YPlaneBytes =
        static_cast<std::uint64_t>(nv12.frame.yPlaneSize());
    result.bgraToNv12UvPlaneBytes =
        static_cast<std::uint64_t>(nv12.frame.uvPlaneSize());

    ComApartment apartment;
    if (FAILED(apartment.hr)) {
        result.nativeCode = static_cast<long>(apartment.hr);
        result.message =
            "COM initialization failed: " + hresultText(apartment.hr);
        return result;
    }

    MediaFoundationLifetime mf;
    result.nativeCode = static_cast<long>(mf.hr);
    if (FAILED(mf.hr)) {
        result.message =
            "MediaFoundation startup failed: " + hresultText(mf.hr);
        return result;
    }
    result.mediaFoundationStarted = true;

    MFT_REGISTER_TYPE_INFO h264Video;
    h264Video.guidMajorType = MFMediaType_Video;
    h264Video.guidSubtype = MFVideoFormat_H264;

    Microsoft::WRL::ComPtr<IMFTransform> encoder;
    HRESULT hr = createFirstTransform(MFT_CATEGORY_VIDEO_ENCODER,
                                      nullptr,
                                      &h264Video,
                                      encoder);
    result.h264EncoderMftCreated = SUCCEEDED(hr) && encoder;
    if (!result.h264EncoderMftCreated)
        result.nativeCode = static_cast<long>(hr);

    if (result.h264EncoderMftCreated) {
        hr = setEncoderOutputType(encoder.Get(), width, height);
        result.encoderOutputTypeAccepted = SUCCEEDED(hr);
        if (!result.encoderOutputTypeAccepted) {
            result.nativeCode = static_cast<long>(hr);
        } else {
            result.encoderBgraInputAccepted =
                encoderAcceptsInputSubtype(MFVideoFormat_RGB32,
                                           width,
                                           height);
            result.encoderNv12InputAccepted =
                encoderAcceptsInputSubtype(MFVideoFormat_NV12,
                                           width,
                                           height);
            if (!result.encoderBgraInputAccepted &&
                !result.encoderNv12InputAccepted)
                result.nativeCode = static_cast<long>(MF_E_INVALIDMEDIATYPE);
        }
    }

    Microsoft::WRL::ComPtr<IMFTransform> decoder;
    hr = createFirstTransform(MFT_CATEGORY_VIDEO_DECODER,
                              &h264Video,
                              nullptr,
                              decoder);
    result.h264DecoderMftCreated = SUCCEEDED(hr) && decoder;
    if (!result.h264DecoderMftCreated) {
        result.nativeCode = static_cast<long>(hr);
    } else {
        hr = setDecoderInputType(decoder.Get(), width, height);
        result.decoderInputTypeAccepted = SUCCEEDED(hr);
        if (!result.decoderInputTypeAccepted) {
            result.nativeCode = static_cast<long>(hr);
        } else {
            GUID decoderOutputSubtype = GUID_NULL;
            std::uint32_t decoderOutputStride = 0;
            hr = setDecoderOutputType(decoder.Get(),
                                      width,
                                      height,
                                      decoderOutputSubtype,
                                      decoderOutputStride);
            result.decoderOutputTypeAccepted = SUCCEEDED(hr);
            if (result.decoderOutputTypeAccepted) {
                result.decoderNv12OutputAccepted =
                    IsEqualGUID(decoderOutputSubtype, MFVideoFormat_NV12);
                result.decoderBgraOutputAccepted =
                    IsEqualGUID(decoderOutputSubtype, MFVideoFormat_RGB32) ||
                    IsEqualGUID(decoderOutputSubtype, MFVideoFormat_ARGB32);
            } else {
                result.nativeCode = static_cast<long>(hr);
            }
        }
    }

    result.message =
        "encoderMft=" + boolText(result.h264EncoderMftCreated) +
        " encoderOutput=" + boolText(result.encoderOutputTypeAccepted) +
        " encoderBgraInput=" + boolText(result.encoderBgraInputAccepted) +
        " encoderNv12Input=" + boolText(result.encoderNv12InputAccepted) +
        " bgraToNv12=" + boolText(result.bgraToNv12ConversionOk) +
        " nv12Bytes=" + std::to_string(result.bgraToNv12Bytes) +
        " decoderMft=" + boolText(result.h264DecoderMftCreated) +
        " decoderInput=" + boolText(result.decoderInputTypeAccepted) +
        " decoderOutput=" + boolText(result.decoderOutputTypeAccepted) +
        " decoderNv12Output=" +
        boolText(result.decoderNv12OutputAccepted) +
        " decoderBgraOutput=" + boolText(result.decoderBgraOutputAccepted);
    return result;
}

WindowsMediaFoundationH264EncodePreflightResult
preflightWindowsMediaFoundationH264Encode(std::uint32_t width,
                                         std::uint32_t height)
{
    WindowsMediaFoundationH264EncodePreflightResult result;
    result.rolloutEnabled = envEnabled("FUSIONDESK_ENABLE_MF_CODEC");
    if (!result.rolloutEnabled) {
        result.message =
            "MediaFoundation H.264 encode preflight rollout is not enabled";
        return result;
    }
    if (width == 0 || height == 0) {
        result.message =
            "MediaFoundation H.264 encode preflight requires a non-zero size";
        return result;
    }
    if ((width % 2U) != 0 || (height % 2U) != 0) {
        result.message =
            "MediaFoundation H.264 encode preflight requires even dimensions";
        return result;
    }

    const modules::display::CapturedFrame bgra =
        makePreflightBgraFrame(width, height);
    const BgraToNv12Result nv12 = runtime::display::convertBgraToNv12(bgra);
    result.bgraToNv12ConversionOk = nv12.ok;
    if (!nv12.ok) {
        result.message = "BGRA-to-NV12 encode preflight conversion failed: " +
                         nv12.error;
        return result;
    }
    result.bgraToNv12Bytes =
        static_cast<std::uint64_t>(nv12.frame.bytes.size());

    ComApartment apartment;
    if (FAILED(apartment.hr)) {
        result.nativeCode = static_cast<long>(apartment.hr);
        result.message =
            "COM initialization failed: " + hresultText(apartment.hr);
        return result;
    }

    MediaFoundationLifetime mf;
    result.nativeCode = static_cast<long>(mf.hr);
    if (FAILED(mf.hr)) {
        result.message =
            "MediaFoundation startup failed: " + hresultText(mf.hr);
        return result;
    }
    result.mediaFoundationStarted = true;

    Microsoft::WRL::ComPtr<IMFTransform> encoder;
    GUID selectedInputSubtype = GUID_NULL;
    bool encoderMftCreated = false;
    bool outputTypeAccepted = false;
    bool nv12InputAccepted = false;
    bool bgraInputAccepted = false;
    HRESULT hr = createConfiguredH264Encoder(width,
                                             height,
                                             defaultDisplayCodecPolicy().pFrameEnabled,
                                             encoder,
                                             selectedInputSubtype,
                                             encoderMftCreated,
                                             outputTypeAccepted,
                                             nv12InputAccepted,
                                             bgraInputAccepted);
    result.h264EncoderMftCreated = encoderMftCreated;
    result.encoderOutputTypeAccepted = outputTypeAccepted;
    result.encoderNv12InputAccepted = nv12InputAccepted;
    result.encoderBgraInputAccepted = bgraInputAccepted;
    if (FAILED(hr) || !encoder) {
        result.nativeCode = static_cast<long>(hr);
        result.message = "H.264 encoder CPU-input configuration failed: " +
                         hresultText(hr);
        return result;
    }

    const protocol::ByteBuffer* selectedInputBytes =
        IsEqualGUID(selectedInputSubtype, MFVideoFormat_NV12)
            ? &nv12.frame.bytes
            : &bgra.pixels;

    hr = encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (SUCCEEDED(hr))
        hr = encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    result.streamingStarted = SUCCEEDED(hr);
    if (!result.streamingStarted) {
        result.nativeCode = static_cast<long>(hr);
        result.message = "H.264 encoder stream start failed: " +
                         hresultText(hr);
        return result;
    }

    Microsoft::WRL::ComPtr<IMFSample> inputSample;
    hr = createSampleFromBytes(*selectedInputBytes,
                               0,
                               10000000LL / 30LL,
                               inputSample);
    if (SUCCEEDED(hr))
        hr = encoder->ProcessInput(0, inputSample.Get(), 0);
    result.inputSampleAccepted = SUCCEEDED(hr);
    if (!result.inputSampleAccepted) {
        result.nativeCode = static_cast<long>(hr);
        result.message = "H.264 encoder input sample rejected: " +
                         hresultText(hr);
        return result;
    }

    encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    protocol::ByteBuffer bitstream;
    hr = pullEncoderOutput(encoder.Get(),
                           width,
                           height,
                           bitstream,
                           result.outputSampleProduced);
    if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT) {
        result.nativeCode = static_cast<long>(hr);
        result.message = "H.264 encoder output failed: " + hresultText(hr);
        return result;
    }

    result.bitstreamBytes = static_cast<std::uint64_t>(bitstream.size());
    if (!bitstream.empty()) {
        const protocol::ByteBuffer sequenceHeader =
            encoderSequenceHeader(encoder.Get());
        const protocol::ByteBuffer fdsf =
            encodeH264AsFdsf(width,
                             height,
                             1,
                             true,
                             1,
                             sequenceHeader,
                             bitstream);
        result.fdsfPayloadBytes = static_cast<std::uint64_t>(fdsf.size());
        result.fdsfPayloadEncoded = !fdsf.empty();
    }

    result.message =
        "bgraToNv12=" + boolText(result.bgraToNv12ConversionOk) +
        " nv12Bytes=" + std::to_string(result.bgraToNv12Bytes) +
        " encoderMft=" + boolText(result.h264EncoderMftCreated) +
        " encoderOutput=" + boolText(result.encoderOutputTypeAccepted) +
        " encoderNv12Input=" + boolText(result.encoderNv12InputAccepted) +
        " encoderBgraInput=" + boolText(result.encoderBgraInputAccepted) +
        " streamStarted=" + boolText(result.streamingStarted) +
        " inputSample=" + boolText(result.inputSampleAccepted) +
        " outputSample=" + boolText(result.outputSampleProduced) +
        " bitstreamBytes=" + std::to_string(result.bitstreamBytes) +
        " fdsf=" + boolText(result.fdsfPayloadEncoded) +
        " fdsfBytes=" + std::to_string(result.fdsfPayloadBytes);
    return result;
}

std::vector<runtime::display::DisplayCodecCapability>
WindowsMediaFoundationDisplayCodecBackendFactory::capabilities() const
{
    return {h264Capability(
        probeWindowsMediaFoundationH264Codec(policy_.rolloutEnabled),
        policy_)};
}

std::shared_ptr<modules::display::IVideoEncoder>
WindowsMediaFoundationDisplayCodecBackendFactory::createEncoder(
    const runtime::display::DisplayCodecCapability& selected) const
{
    if (!policy_.rolloutEnabled)
        return nullptr;
    if (selected.adapterId != "windows.media_foundation.h264" ||
        selected.backend != DisplayCodecBackendKind::WindowsMediaFoundation ||
        selected.codec != DisplayCodecId::H264 ||
        !selected.supportsEncode)
        return nullptr;

    return std::make_shared<WindowsMediaFoundationH264Encoder>(policy_);
}

std::shared_ptr<modules::display::IVideoDecoder>
WindowsMediaFoundationDisplayCodecBackendFactory::createDecoder(
    const runtime::display::DisplayCodecCapability& selected) const
{
    if (!policy_.rolloutEnabled)
        return nullptr;
    if (selected.adapterId != "windows.media_foundation.h264" ||
        selected.backend != DisplayCodecBackendKind::WindowsMediaFoundation ||
        selected.codec != DisplayCodecId::H264 ||
        !selected.supportsDecode)
        return nullptr;

    return std::make_shared<WindowsMediaFoundationH264Decoder>(policy_);
}

} // namespace display
} // namespace windows
} // namespace platform
} // namespace fusiondesk
