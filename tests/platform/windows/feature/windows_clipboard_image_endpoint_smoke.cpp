#include <cassert>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"
#include "windows_clipboard_image_transcoding.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;
using namespace fusiondesk::platform::windows::clipboard;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

bool isPng(const protocol::ByteBuffer& bytes)
{
    const unsigned char signature[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return bytes.size() >= sizeof(signature) &&
           std::memcmp(bytes.data(), signature, sizeof(signature)) == 0;
}

protocol::ByteBuffer sampleDib32()
{
    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = 2;
    header.biHeight = -2;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = 16;

    const unsigned char pixels[] = {
        0x00, 0x00, 0xFF, 0xFF,
        0x00, 0xFF, 0x00, 0xFF,
        0xFF, 0x00, 0x00, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF};

    protocol::ByteBuffer dib(sizeof(header) + sizeof(pixels));
    std::memcpy(dib.data(), &header, sizeof(header));
    std::memcpy(dib.data() + sizeof(header), pixels, sizeof(pixels));
    return dib;
}

protocol::ByteBuffer samplePng()
{
    protocol::ByteBuffer png = windowsPngFromDibBytes(sampleDib32());
    assert(isPng(png));
    return png;
}

TransferSourceBundle imagePngBundle(protocol::ByteBuffer png)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImagePngFormat;
    descriptor.nativeFormatName = "PNG";
    descriptor.localFormatToken = windowsPngFormatToken();
    descriptor.formatId = 58;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = png.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = std::move(png);

    TransferSourceBundle bundle;
    bundle.bundleId = 16;
    bundle.offerId = 27;
    bundle.ownerEpoch = 38;
    bundle.sequence = 49;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            82,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

TransferSourceBundle imageDibBundle(protocol::ByteBuffer dib)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImageDibFormat;
    descriptor.nativeFormatName = "CF_DIB";
    descriptor.localFormatToken = CF_DIB;
    descriptor.formatId = 59;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = dib.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::NativePassthrough;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = std::move(dib);

    TransferSourceBundle bundle;
    bundle.bundleId = 17;
    bundle.offerId = 28;
    bundle.ownerEpoch = 39;
    bundle.sequence = 50;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            83,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

class FakeRemoteReader final : public IClipboardRemoteReader
{
public:
    TransferReadResult readRemoteFormat(const TransferReadRequest& request,
                                        std::uint32_t timeoutMs) override
    {
        ++calls;
        lastRequest = request;
        lastTimeoutMs = timeoutMs;

        TransferReadResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.canonicalFormat = responseCanonicalFormat;
        result.encoding = responseEncoding;
        result.bytes = responseBytes;
        return result;
    }

    int calls = 0;
    std::uint32_t lastTimeoutMs = 0;
    TransferReadRequest lastRequest;
    std::string responseCanonicalFormat = ImagePngFormat;
    TransferEncodingMode responseEncoding = TransferEncodingMode::CanonicalBytes;
    protocol::ByteBuffer responseBytes = bytes("remote png");
};

void dryRunSnapshotBuildsImagePngBundle()
{
    WindowsClipboardEndpoint endpoint;
    endpoint.setDryRunClipboardImagePng(bytes("png bytes"));

    const ClipboardSnapshot snapshot = endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);
    assert(snapshot.bundle.presentation.has_value());
    assert(snapshot.bundle.presentation->sourceKind == TransferSourceKind::Image);
    assert(snapshot.bundle.presentation->icons.size() == 1);
    assert(snapshot.bundle.presentation->icons.front().format == ImagePngFormat);
    assert(snapshot.bundle.presentation->icons.front().bytes == 9);

    const std::vector<TransferFormatDescriptor> formats =
        snapshot.bundle.sources.front()->formats();
    assert(formats.size() == 1);
    assert(formats.front().canonicalFormat == ImagePngFormat);
    assert(formats.front().nativeFormatName == "PNG");

    TransferReadRequest request;
    request.bundleId = snapshot.bundle.bundleId;
    request.offerId = snapshot.bundle.offerId;
    request.ownerEpoch = snapshot.bundle.ownerEpoch;
    request.sourceId = snapshot.bundle.sources.front()->id();
    request.itemIndex = formats.front().itemIndex;
    request.formatId = formats.front().formatId;
    request.localFormatToken = formats.front().localFormatToken;
    request.canonicalFormat = ImagePngFormat;
    request.acceptedMaxBytes = 1024;
    const TransferReadResult result =
        snapshot.bundle.sources.front()->read(request);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.bytes == bytes("png bytes"));
}

void dryRunSnapshotBuildsImageDibBundle()
{
    const protocol::ByteBuffer dib = sampleDib32();
    WindowsClipboardEndpoint endpoint;
    endpoint.setDryRunClipboardImageDib(dib, "CF_DIB", CF_DIB);

    const ClipboardSnapshot snapshot = endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);
    assert(snapshot.bundle.presentation.has_value());
    assert(snapshot.bundle.presentation->sourceKind == TransferSourceKind::Image);

    const std::vector<TransferFormatDescriptor> formats =
        snapshot.bundle.sources.front()->formats();
    assert(formats.size() == 2);
    assert(std::any_of(formats.begin(),
                       formats.end(),
                       [](const TransferFormatDescriptor& descriptor) {
                           return descriptor.canonicalFormat == ImagePngFormat &&
                                  descriptor.preferredEncoding ==
                                      TransferEncodingMode::Transcoded;
                       }));
    const auto dibFormat = std::find_if(
        formats.begin(),
        formats.end(),
        [](const TransferFormatDescriptor& descriptor) {
            return descriptor.canonicalFormat == ImageDibFormat &&
                   descriptor.nativeFormatName == "CF_DIB" &&
                   descriptor.localFormatToken == CF_DIB &&
                   descriptor.preferredEncoding ==
                       TransferEncodingMode::NativePassthrough;
        });
    assert(dibFormat != formats.end());

    TransferReadRequest request;
    request.bundleId = snapshot.bundle.bundleId;
    request.offerId = snapshot.bundle.offerId;
    request.ownerEpoch = snapshot.bundle.ownerEpoch;
    request.sourceId = snapshot.bundle.sources.front()->id();
    request.itemIndex = dibFormat->itemIndex;
    request.formatId = dibFormat->formatId;
    request.localFormatToken = dibFormat->localFormatToken;
    request.canonicalFormat = ImageDibFormat;
    request.acceptedMaxBytes = 4096;
    const TransferReadResult result =
        snapshot.bundle.sources.front()->read(request);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.canonicalFormat == ImageDibFormat);
    assert(result.encoding == TransferEncodingMode::NativePassthrough);
    assert(result.bytes == dib);
}

void dryRunPublishMaterializesImagePng()
{
    WindowsClipboardEndpoint endpoint;
    const protocol::ResponseStatus status =
        endpoint.publishBundle(
            ClipboardPublishRequest{imagePngBundle(bytes("png publish"))});

    assert(status == protocol::ResponseStatus::Ok);
    assert(endpoint.dryRunClipboardImagePng() == bytes("png publish"));
    assert(endpoint.dryRunClipboardText().empty());
    assert(endpoint.dryRunClipboardRtf().empty());
    assert(endpoint.diagnostics().publishes == 1);
    assert(endpoint.diagnostics().publishedOfferId == 27);
}

void dryRunPublishMaterializesImageDib()
{
    const protocol::ByteBuffer dib = sampleDib32();
    WindowsClipboardEndpoint endpoint;
    const protocol::ResponseStatus status =
        endpoint.publishBundle(ClipboardPublishRequest{imageDibBundle(dib)});

    assert(status == protocol::ResponseStatus::Ok);
    assert(endpoint.dryRunClipboardImageDib() == dib);
    assert(endpoint.dryRunClipboardImagePng().empty());
    assert(endpoint.diagnostics().publishes == 1);
    assert(endpoint.diagnostics().publishedOfferId == 28);
}

void dryRunDelayedRenderImagePngUsesRemoteReaderWhenRequested()
{
    auto reader = std::make_shared<FakeRemoteReader>();

    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 49;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImagePngFormat;
    descriptor.nativeFormatName = "PNG";
    descriptor.localFormatToken = windowsPngFormatToken();
    descriptor.formatId = 60;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 17;
    bundle.offerId = 28;
    bundle.ownerEpoch = 39;
    bundle.sequence = 50;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            83,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 0);

    assert(endpoint.renderDelayedFormatForNative(windowsPngFormatToken()) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastRequest.canonicalFormat == ImagePngFormat);
    assert(reader->lastTimeoutMs == 49);
    assert(endpoint.dryRunClipboardImagePng() == bytes("remote png"));
    assert(endpoint.diagnostics().imagePngRenders == 1);
}

void dryRunDelayedRenderRawImageDibUsesRemoteReaderWhenRequested()
{
    const protocol::ByteBuffer dib = sampleDib32();
    auto reader = std::make_shared<FakeRemoteReader>();
    reader->responseCanonicalFormat = ImageDibFormat;
    reader->responseEncoding = TransferEncodingMode::NativePassthrough;
    reader->responseBytes = dib;

    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 53;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImageDibFormat;
    descriptor.nativeFormatName = "CF_DIB";
    descriptor.localFormatToken = CF_DIB;
    descriptor.formatId = 62;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::NativePassthrough;

    TransferSourceBundle bundle;
    bundle.bundleId = 19;
    bundle.offerId = 30;
    bundle.ownerEpoch = 41;
    bundle.sequence = 52;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            85,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);

    assert(endpoint.renderDelayedFormatForNative(CF_DIB) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastRequest.canonicalFormat == ImageDibFormat);
    assert(reader->lastTimeoutMs == 53);
    assert(endpoint.dryRunClipboardImageDib() == dib);
    assert(endpoint.dryRunClipboardImagePng().empty());
    assert(endpoint.diagnostics().imageDibRenders == 1);
}

void dryRunDelayedRenderImageDibUsesRemoteReaderWhenRequested()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    reader->responseBytes = samplePng();

    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 51;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImagePngFormat;
    descriptor.nativeFormatName = "PNG";
    descriptor.localFormatToken = windowsPngFormatToken();
    descriptor.formatId = 61;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 18;
    bundle.offerId = 29;
    bundle.ownerEpoch = 40;
    bundle.sequence = 51;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            84,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);

    assert(endpoint.renderDelayedFormatForNative(CF_DIB) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastRequest.canonicalFormat == ImagePngFormat);
    assert(reader->lastTimeoutMs == 51);
    assert(endpoint.dryRunClipboardImagePng() == reader->responseBytes);
    assert(endpoint.diagnostics().imageDibRenders == 1);
}

void windowsDibPngTranscodingRoundTrip()
{
    const protocol::ByteBuffer png = samplePng();

    const protocol::ByteBuffer dib = windowsDibFromPngBytes(png);
    assert(dib.size() > sizeof(BITMAPINFOHEADER));
    BITMAPINFOHEADER dibHeader = {};
    std::memcpy(&dibHeader, dib.data(), sizeof(dibHeader));
    assert(dibHeader.biSize == sizeof(BITMAPINFOHEADER));
    assert(dibHeader.biWidth == 2);
    assert(dibHeader.biHeight == -2);
    assert(dibHeader.biBitCount == 32);

    const protocol::ByteBuffer dibV5 = windowsDibV5FromPngBytes(png);
    assert(dibV5.size() > sizeof(BITMAPV5HEADER));
    BITMAPV5HEADER dibV5Header = {};
    std::memcpy(&dibV5Header, dibV5.data(), sizeof(dibV5Header));
    assert(dibV5Header.bV5Size == sizeof(BITMAPV5HEADER));
    assert(dibV5Header.bV5Width == 2);
    assert(dibV5Header.bV5Height == -2);
    assert(dibV5Header.bV5BitCount == 32);

    const protocol::ByteBuffer pngFromDibV5 =
        windowsPngFromDibBytes(dibV5);
    assert(isPng(pngFromDibV5));
}

} // namespace

int main()
{
    windowsDibPngTranscodingRoundTrip();
    dryRunSnapshotBuildsImagePngBundle();
    dryRunSnapshotBuildsImageDibBundle();
    dryRunPublishMaterializesImagePng();
    dryRunPublishMaterializesImageDib();
    dryRunDelayedRenderImagePngUsesRemoteReaderWhenRequested();
    dryRunDelayedRenderRawImageDibUsesRemoteReaderWhenRequested();
    dryRunDelayedRenderImageDibUsesRemoteReaderWhenRequested();
    return 0;
}
