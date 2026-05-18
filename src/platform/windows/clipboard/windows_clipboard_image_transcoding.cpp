#include "windows_clipboard_image_transcoding.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <wincodec.h>
#endif

namespace fusiondesk {
namespace platform {
namespace windows {
namespace clipboard {

#if defined(_WIN32)
namespace {

constexpr std::size_t Npos = std::numeric_limits<std::size_t>::max();

template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ~ComPtr()
    {
        reset();
    }

    T* get() const
    {
        return ptr_;
    }

    T** put()
    {
        reset();
        return &ptr_;
    }

    T* operator->() const
    {
        return ptr_;
    }

    void reset(T* value = nullptr)
    {
        if (ptr_ != nullptr)
            ptr_->Release();
        ptr_ = value;
    }

private:
    T* ptr_ = nullptr;
};

class ScopedComInit
{
public:
    ScopedComInit()
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
        uninitialize_ = SUCCEEDED(result_);
    }

    ~ScopedComInit()
    {
        if (uninitialize_)
            CoUninitialize();
    }

    bool ok() const
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
    bool uninitialize_ = false;
};

bool createWicFactory(ComPtr<IWICImagingFactory>& factory)
{
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.put())));
}

std::uint32_t readLeU32(const protocol::ByteBuffer& bytes,
                        std::size_t offset)
{
    std::uint32_t value = 0;
    if (offset + sizeof(value) <= bytes.size())
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::size_t dibPixelOffset(const protocol::ByteBuffer& dibBytes)
{
    if (dibBytes.size() < sizeof(std::uint32_t))
        return Npos;

    const std::uint32_t headerSize = readLeU32(dibBytes, 0);
    if (headerSize < sizeof(BITMAPINFOHEADER) ||
        headerSize > dibBytes.size())
        return Npos;

    BITMAPINFOHEADER info = {};
    std::memcpy(&info, dibBytes.data(), sizeof(info));
    if (info.biPlanes != 1 || info.biBitCount == 0)
        return Npos;

    std::size_t offset = headerSize;
    if (headerSize == sizeof(BITMAPINFOHEADER)) {
        if (info.biCompression == BI_BITFIELDS)
            offset += 3 * sizeof(DWORD);
#if defined(BI_ALPHABITFIELDS)
        if (info.biCompression == BI_ALPHABITFIELDS)
            offset += 4 * sizeof(DWORD);
#endif
    }

    if (info.biBitCount <= 8) {
        const std::uint32_t colorCount =
            info.biClrUsed != 0 ? info.biClrUsed :
                                  (1U << info.biBitCount);
        const std::size_t colorBytes =
            static_cast<std::size_t>(colorCount) * sizeof(RGBQUAD);
        if (colorBytes > dibBytes.size() - offset)
            return Npos;
        offset += colorBytes;
    }

    return offset <= dibBytes.size() ? offset : Npos;
}

protocol::ByteBuffer bmpFileFromDib(const protocol::ByteBuffer& dibBytes)
{
    const std::size_t pixelOffset = dibPixelOffset(dibBytes);
    if (pixelOffset == Npos)
        return {};

    if (dibBytes.size() >
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) -
            sizeof(BITMAPFILEHEADER))
        return {};

    static_assert(sizeof(BITMAPFILEHEADER) == 14,
                  "Windows BMP file header must be packed");

    BITMAPFILEHEADER fileHeader = {};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits =
        static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + pixelOffset);
    fileHeader.bfSize =
        static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + dibBytes.size());

    protocol::ByteBuffer result(sizeof(BITMAPFILEHEADER) + dibBytes.size());
    std::memcpy(result.data(), &fileHeader, sizeof(fileHeader));
    std::memcpy(result.data() + sizeof(fileHeader),
                dibBytes.data(),
                dibBytes.size());
    return result;
}

bool decodeEncodedImageToBgra(const protocol::ByteBuffer& encoded,
                              UINT& width,
                              UINT& height,
                              protocol::ByteBuffer& bgra)
{
    if (encoded.empty() ||
        encoded.size() > std::numeric_limits<DWORD>::max())
        return false;

    ScopedComInit com;
    if (!com.ok())
        return false;

    ComPtr<IWICImagingFactory> factory;
    if (!createWicFactory(factory))
        return false;

    protocol::ByteBuffer mutableEncoded = encoded;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromMemory(
            mutableEncoded.data(),
            static_cast<DWORD>(mutableEncoded.size()))))
        return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(
            stream.get(),
            nullptr,
            WICDecodeMetadataCacheOnLoad,
            decoder.put())))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.put())) ||
        FAILED(frame->GetSize(&width, &height)) ||
        width == 0 || height == 0)
        return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(frame.get(),
                                     GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone,
                                     nullptr,
                                     0.0,
                                     WICBitmapPaletteTypeCustom)))
        return false;

    if (width > std::numeric_limits<UINT>::max() / 4)
        return false;
    const UINT stride = width * 4;
    if (height > std::numeric_limits<UINT>::max() / stride)
        return false;
    const UINT size = stride * height;

    bgra.assign(size, 0);
    return SUCCEEDED(converter->CopyPixels(nullptr,
                                           stride,
                                           size,
                                           bgra.data()));
}

protocol::ByteBuffer streamBytes(IStream* stream)
{
    if (stream == nullptr)
        return {};

    STATSTG stat = {};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) ||
        stat.cbSize.HighPart != 0 ||
        stat.cbSize.LowPart == 0)
        return {};

    HGLOBAL global = nullptr;
    if (FAILED(GetHGlobalFromStream(stream, &global)) || global == nullptr)
        return {};

    const void* locked = GlobalLock(global);
    if (locked == nullptr)
        return {};

    protocol::ByteBuffer result(stat.cbSize.LowPart);
    std::memcpy(result.data(), locked, result.size());
    GlobalUnlock(global);
    return result;
}

protocol::ByteBuffer encodeBgraToPng(UINT width,
                                      UINT height,
                                      const protocol::ByteBuffer& bgra)
{
    if (width == 0 || height == 0 ||
        width > std::numeric_limits<UINT>::max() / 4)
        return {};
    const UINT stride = width * 4;
    if (height > std::numeric_limits<UINT>::max() / stride ||
        bgra.size() != static_cast<std::size_t>(stride) * height)
        return {};

    ScopedComInit com;
    if (!com.ok())
        return {};

    ComPtr<IWICImagingFactory> factory;
    if (!createWicFactory(factory))
        return {};

    protocol::ByteBuffer mutablePixels = bgra;
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(
            width,
            height,
            GUID_WICPixelFormat32bppBGRA,
            stride,
            static_cast<UINT>(mutablePixels.size()),
            mutablePixels.data(),
            bitmap.put())))
        return {};

    ComPtr<IStream> output;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, output.put())))
        return {};

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,
                                      nullptr,
                                      encoder.put())) ||
        FAILED(encoder->Initialize(output.get(), WICBitmapEncoderNoCache)))
        return {};

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(encoder->CreateNewFrame(frame.put(), properties.put())) ||
        FAILED(frame->Initialize(properties.get())) ||
        FAILED(frame->SetSize(width, height)))
        return {};

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) ||
        FAILED(frame->WriteSource(bitmap.get(), nullptr)) ||
        FAILED(frame->Commit()) ||
        FAILED(encoder->Commit()))
        return {};

    return streamBytes(output.get());
}

protocol::ByteBuffer dibFromBgra(UINT width,
                                 UINT height,
                                 const protocol::ByteBuffer& bgra)
{
    if (width == 0 || height == 0 ||
        width > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        width > std::numeric_limits<UINT>::max() / 4)
        return {};

    const UINT stride = width * 4;
    if (height > std::numeric_limits<UINT>::max() / stride ||
        bgra.size() != static_cast<std::size_t>(stride) * height)
        return {};

    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = static_cast<LONG>(width);
    header.biHeight = -static_cast<LONG>(height);
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(bgra.size());

    protocol::ByteBuffer result(sizeof(header) + bgra.size());
    std::memcpy(result.data(), &header, sizeof(header));
    std::memcpy(result.data() + sizeof(header), bgra.data(), bgra.size());
    return result;
}

protocol::ByteBuffer dibV5FromBgra(UINT width,
                                   UINT height,
                                   const protocol::ByteBuffer& bgra)
{
    if (width == 0 || height == 0 ||
        width > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        width > std::numeric_limits<UINT>::max() / 4)
        return {};

    const UINT stride = width * 4;
    if (height > std::numeric_limits<UINT>::max() / stride ||
        bgra.size() != static_cast<std::size_t>(stride) * height)
        return {};

    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = static_cast<LONG>(width);
    header.bV5Height = -static_cast<LONG>(height);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    header.bV5CSType = LCS_sRGB;
    header.bV5SizeImage = static_cast<DWORD>(bgra.size());

    protocol::ByteBuffer result(sizeof(header) + bgra.size());
    std::memcpy(result.data(), &header, sizeof(header));
    std::memcpy(result.data() + sizeof(header), bgra.data(), bgra.size());
    return result;
}

} // namespace
#endif // defined(_WIN32)

protocol::ByteBuffer windowsPngFromDibBytes(
    const protocol::ByteBuffer& dibBytes)
{
#if defined(_WIN32)
    const protocol::ByteBuffer bmp = bmpFileFromDib(dibBytes);
    if (bmp.empty())
        return {};

    UINT width = 0;
    UINT height = 0;
    protocol::ByteBuffer bgra;
    if (!decodeEncodedImageToBgra(bmp, width, height, bgra))
        return {};
    return encodeBgraToPng(width, height, bgra);
#else
    (void)dibBytes;
    return {};
#endif
}

protocol::ByteBuffer windowsDibFromPngBytes(
    const protocol::ByteBuffer& pngBytes)
{
#if defined(_WIN32)
    UINT width = 0;
    UINT height = 0;
    protocol::ByteBuffer bgra;
    if (!decodeEncodedImageToBgra(pngBytes, width, height, bgra))
        return {};
    return dibFromBgra(width, height, bgra);
#else
    (void)pngBytes;
    return {};
#endif
}

protocol::ByteBuffer windowsDibV5FromPngBytes(
    const protocol::ByteBuffer& pngBytes)
{
#if defined(_WIN32)
    UINT width = 0;
    UINT height = 0;
    protocol::ByteBuffer bgra;
    if (!decodeEncodedImageToBgra(pngBytes, width, height, bgra))
        return {};
    return dibV5FromBgra(width, height, bgra);
#else
    (void)pngBytes;
    return {};
#endif
}

} // namespace clipboard
} // namespace windows
} // namespace platform
} // namespace fusiondesk
