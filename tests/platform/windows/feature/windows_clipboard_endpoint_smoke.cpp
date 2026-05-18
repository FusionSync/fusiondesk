#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

#if defined(_WIN32)
#include "windows_clipboard_ole_data_object.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <ole2.h>
#include <shlobj.h>
#endif

using namespace fusiondesk;
using namespace fusiondesk::modules::clipboard;
using namespace fusiondesk::platform::windows::clipboard;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

TransferSourceBundle textBundle(std::string text)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = text.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes(text);

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

TransferSourceBundle rtfBundle(std::string rtf)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextRtfFormat;
    descriptor.nativeFormatName = "Rich Text Format";
    descriptor.localFormatToken = windowsRtfFormatToken();
    descriptor.formatId = 57;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = rtf.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes(rtf);

    TransferSourceBundle bundle;
    bundle.bundleId = 15;
    bundle.offerId = 26;
    bundle.ownerEpoch = 37;
    bundle.sequence = 48;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            81,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

TransferFileList sampleFileList()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 9001;
    file.displayName = "..\\report.pdf";
    file.sizeBytes = 4096;
    file.lastModifiedUnixUsec = 123456;
    list.files.push_back(file);
    return list;
}

TransferFileList nestedFileList()
{
    TransferFileList list;
    TransferFileDescriptor file;
    file.objectId = 9101;
    file.displayName = "report.pdf";
    file.relativePath = "folder/report.pdf";
    file.sizeBytes = 2048;
    list.files.push_back(file);
    return list;
}

TransferSourceBundle fileListBundle()
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.localFormatToken = windowsFileGroupDescriptorFormatToken();
    descriptor.formatId = 56;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 128;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 12;
    bundle.offerId = 23;
    bundle.ownerEpoch = 34;
    bundle.sequence = 45;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<FileGroupTransferSource>(
            78,
            descriptor,
            sampleFileList()));
    return bundle;
}

DragSurfaceCoordinate dragPoint(std::int32_t x, std::int32_t y)
{
    DragSurfaceCoordinate point;
    point.coordinateSpace = DragCoordinateSpace::RemoteLogical;
    point.x = x;
    point.y = y;
    point.surfaceWidth = 1280;
    point.surfaceHeight = 720;
    point.scale = 1.0;
    return point;
}

class FakeRemoteReader final : public IClipboardRemoteReader
                             , public IClipboardRemoteFileReader
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
        result.canonicalFormat = responseFormat;
        result.encoding = TransferEncodingMode::CanonicalBytes;
        result.bytes = responseBytes.empty() ? bytes(responseText) : responseBytes;
        return result;
    }

    TransferFileRangeResult readRemoteFileRange(
        const TransferFileRangeRequest& request,
        std::uint32_t timeoutMs) override
    {
        ++fileRangeCalls;
        lastFileRangeRequest = request;
        lastFileRangeTimeoutMs = timeoutMs;

        TransferFileRangeResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.bytes = responseFileBytes.empty()
                           ? bytes("remote file")
                           : responseFileBytes;
        result.endOfFile = true;
        return result;
    }

    int calls = 0;
    int fileRangeCalls = 0;
    std::uint32_t lastTimeoutMs = 0;
    std::uint32_t lastFileRangeTimeoutMs = 0;
    TransferReadRequest lastRequest;
    TransferFileRangeRequest lastFileRangeRequest;
    std::string responseFormat = TextPlainUtf8Format;
    std::string responseText = "remote text";
    protocol::ByteBuffer responseBytes;
    protocol::ByteBuffer responseFileBytes;
};

void textConversionRoundTrips()
{
    const protocol::ByteBuffer windowsText =
        windowsCfUnicodeTextFromCanonicalUtf8(bytes("a\nb"));
    assert(windowsText.size() >= 6);
    const protocol::ByteBuffer canonical =
        canonicalUtf8FromWindowsCfUnicodeText(windowsText);
    assert(canonical == bytes("a\nb"));
}

void htmlConversionRoundTrips()
{
    const protocol::ByteBuffer windowsHtml =
        windowsHtmlFromCanonicalHtml(bytes("<b>a</b>"));
    const protocol::ByteBuffer canonical =
        canonicalHtmlFromWindowsHtml(windowsHtml);
    assert(canonical == bytes("<b>a</b>"));
}

void fileGroupDescriptorConversionRoundTrips()
{
    const protocol::ByteBuffer descriptor =
        windowsFileGroupDescriptorFromTransferFileList(sampleFileList());
    assert(!descriptor.empty());

    const TransferFileListDecodeResult decoded =
        transferFileListFromWindowsFileGroupDescriptor(descriptor);
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 1);
    assert(decoded.fileList.files.front().displayName == "___report.pdf");
    assert(decoded.fileList.files.front().sizeBytes == 4096);
    assert(decoded.fileList.files.front().lastModifiedUnixUsec == 123456);
}

void fileGroupDescriptorPreservesRelativePaths()
{
    const protocol::ByteBuffer descriptor =
        windowsFileGroupDescriptorFromTransferFileList(nestedFileList());
    assert(!descriptor.empty());

    const TransferFileListDecodeResult decoded =
        transferFileListFromWindowsFileGroupDescriptor(descriptor);
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 1);
    assert(decoded.fileList.files.front().displayName == "report.pdf");
    assert(decoded.fileList.files.front().relativePath == "folder/report.pdf");
    assert(decoded.fileList.files.front().sizeBytes == 2048);
}

void dryRunSnapshotBuildsMaterializedTextBundle()
{
    WindowsClipboardEndpoint endpoint;
    endpoint.setDryRunClipboardText("hello\nworld");

    const ClipboardSnapshot snapshot = endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);

    const std::vector<TransferFormatDescriptor> formats =
        snapshot.bundle.sources.front()->formats();
    assert(formats.size() == 1);
    assert(formats.front().canonicalFormat == TextPlainUtf8Format);
    assert(formats.front().nativeFormatName == "CF_UNICODETEXT");

    TransferReadRequest request;
    request.bundleId = snapshot.bundle.bundleId;
    request.offerId = snapshot.bundle.offerId;
    request.ownerEpoch = snapshot.bundle.ownerEpoch;
    request.sourceId = snapshot.bundle.sources.front()->id();
    request.itemIndex = formats.front().itemIndex;
    request.formatId = formats.front().formatId;
    request.canonicalFormat = TextPlainUtf8Format;
    request.acceptedMaxBytes = 1024;
    const TransferReadResult result =
        snapshot.bundle.sources.front()->read(request);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.bytes == bytes("hello\nworld"));
}

void dryRunSnapshotBuildsMaterializedRtfBundle()
{
    WindowsClipboardEndpoint endpoint;
    endpoint.setDryRunClipboardRtf("{\\rtf1\\ansi remote}");

    const ClipboardSnapshot snapshot = endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);

    const std::vector<TransferFormatDescriptor> formats =
        snapshot.bundle.sources.front()->formats();
    assert(formats.size() == 1);
    assert(formats.front().canonicalFormat == TextRtfFormat);
    assert(formats.front().nativeFormatName == "Rich Text Format");

    TransferReadRequest request;
    request.bundleId = snapshot.bundle.bundleId;
    request.offerId = snapshot.bundle.offerId;
    request.ownerEpoch = snapshot.bundle.ownerEpoch;
    request.sourceId = snapshot.bundle.sources.front()->id();
    request.itemIndex = formats.front().itemIndex;
    request.formatId = formats.front().formatId;
    request.localFormatToken = formats.front().localFormatToken;
    request.canonicalFormat = TextRtfFormat;
    request.acceptedMaxBytes = 1024;
    const TransferReadResult result =
        snapshot.bundle.sources.front()->read(request);
    assert(result.status == protocol::ResponseStatus::Ok);
    assert(result.bytes == bytes("{\\rtf1\\ansi remote}"));
}

void dryRunSnapshotBuildsFileListBundle()
{
    WindowsClipboardEndpoint endpoint;
    endpoint.setDryRunClipboardFileList(sampleFileList());

    const ClipboardSnapshot snapshot = endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);
    assert(snapshot.bundle.presentation.has_value());
    assert(snapshot.bundle.presentation->sourceKind == TransferSourceKind::FileList);
    assert(snapshot.bundle.presentation->itemCount == 1);

    const std::vector<TransferFormatDescriptor> formats =
        snapshot.bundle.sources.front()->formats();
    assert(formats.size() == 1);
    assert(formats.front().canonicalFormat == FdclFileListFormat);
    assert(formats.front().nativeFormatName == "CF_HDROP");

    TransferReadRequest request;
    request.bundleId = snapshot.bundle.bundleId;
    request.offerId = snapshot.bundle.offerId;
    request.ownerEpoch = snapshot.bundle.ownerEpoch;
    request.sourceId = snapshot.bundle.sources.front()->id();
    request.itemIndex = formats.front().itemIndex;
    request.formatId = formats.front().formatId;
    request.localFormatToken = formats.front().localFormatToken;
    request.canonicalFormat = FdclFileListFormat;
    request.acceptedMaxBytes = 1024;
    const TransferReadResult result =
        snapshot.bundle.sources.front()->read(request);
    assert(result.status == protocol::ResponseStatus::Ok);

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(result.bytes);
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 1);
    assert(decoded.fileList.files.front().displayName == "___report.pdf");
    assert(endpoint.diagnostics().fileListSnapshots == 1);
    assert(!endpoint.dryRunFileGroupDescriptor().empty());
}

void dryRunDragSinkRecordsCoordinates()
{
    WindowsClipboardEndpoint endpoint;
    assert(endpoint.publishBundle(ClipboardPublishRequest{fileListBundle()}) ==
           protocol::ResponseStatus::Ok);

    DragSessionStart start;
    start.dragSessionId = 7001;
    start.bundleId = 12;
    start.offerId = 23;
    start.ownerEpoch = 34;
    start.allowedActions = transfer_action::Copy;
    start.preferredAction = TransferAction::Copy;
    start.start = dragPoint(10, 20);
    assert(endpoint.dragStart(start) == protocol::ResponseStatus::Ok);
    assert(endpoint.dragMove(7001, dragPoint(30, 40), TransferAction::Copy) ==
           protocol::ResponseStatus::Ok);
    assert(endpoint.dragDrop(7001, dragPoint(50, 60), TransferAction::Copy) ==
           protocol::ResponseStatus::Ok);
    assert(endpoint.dragMove(7001, dragPoint(70, 80), TransferAction::Copy) ==
           protocol::ResponseStatus::Conflict);

    start.dragSessionId = 7002;
    start.start = dragPoint(15, 25);
    assert(endpoint.dragStart(start) == protocol::ResponseStatus::Ok);
    assert(endpoint.dragCancel(7002, DragCancelReason::UserCancelled) ==
           protocol::ResponseStatus::Ok);

    const WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.dragStarts == 2);
    assert(diagnostics.dragMoves == 1);
    assert(diagnostics.dragDrops == 1);
    assert(diagnostics.dragCancels == 1);
    assert(diagnostics.lastDragSessionId == 7002);
    assert(diagnostics.activeDragSessionId == 0);
    assert(diagnostics.lastDragX == 15);
    assert(diagnostics.lastDragY == 25);

    start.offerId = 999;
    assert(endpoint.dragStart(start) == protocol::ResponseStatus::Conflict);
}

void nativeDragLoopRequiresExplicitOptIn()
{
    WindowsClipboardEndpointOptions options;
    options.dryRun = false;
    options.useDelayedTextRendering = false;
    options.materializeTextOnPublish = false;
    WindowsClipboardEndpoint endpoint(options);
    assert(endpoint.publishBundle(ClipboardPublishRequest{fileListBundle()}) ==
           protocol::ResponseStatus::Ok);

    DragSessionStart start;
    start.dragSessionId = 7001;
    start.bundleId = 12;
    start.offerId = 23;
    start.ownerEpoch = 34;
    start.allowedActions = transfer_action::Copy;
    start.preferredAction = TransferAction::Copy;
    start.start = dragPoint(10, 20);
    assert(endpoint.dragStart(start) == protocol::ResponseStatus::Unsupported);

    const WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.dragStarts == 1);
    assert(diagnostics.nativeDragLoops == 0);
    assert(diagnostics.lastMessage.find("disabled") != std::string::npos);
}

void nativeDragPreflightBuildsOleDataObjectWithoutStartingLoop()
{
#if defined(_WIN32)
    auto reader = std::make_shared<FakeRemoteReader>();
    reader->responseFileBytes = bytes("range");

    WindowsClipboardEndpointOptions options;
    options.dryRun = true;
    options.useDelayedTextRendering = false;
    options.materializeTextOnPublish = false;
    options.enableNativeDragLoop = true;
    options.nativeDragPreflightOnly = true;

    WindowsClipboardEndpoint endpoint(options, reader);
    assert(endpoint.publishBundle(ClipboardPublishRequest{fileListBundle()}) ==
           protocol::ResponseStatus::Ok);

    DragSessionStart start;
    start.dragSessionId = 7003;
    start.bundleId = 12;
    start.offerId = 23;
    start.ownerEpoch = 34;
    start.allowedActions = transfer_action::Copy;
    start.preferredAction = TransferAction::Copy;
    start.start = dragPoint(10, 20);
    assert(endpoint.dragStart(start) == protocol::ResponseStatus::Ok);

    const WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.dragStarts == 1);
    assert(diagnostics.nativeDragLoops == 1);
    assert(diagnostics.nativeDragPreflights == 1);
    assert(diagnostics.nativeDragPreflightReads == 1);
    assert(diagnostics.nativeDragPreflightBytes == 5);
    assert(diagnostics.nativeDragDrops == 0);
    assert(diagnostics.nativeDragCancels == 0);
    assert(diagnostics.activeDragSessionId == 0);
    assert(diagnostics.lastMessage.find("preflight completed") !=
           std::string::npos);
    assert(reader->fileRangeCalls == 1);
    assert(reader->lastFileRangeRequest.objectId == 9001);
    assert(reader->lastFileRangeRequest.requestedBytes == 16);
#endif
}

void remoteFileDropSourceWaitsForProgrammaticMouseClick()
{
#if defined(_WIN32)
    IDropSource* source = createRemoteFileDropSource();
    assert(source != nullptr);
    assert(source->QueryContinueDrag(FALSE, 0) == S_OK);
    assert(source->QueryContinueDrag(FALSE, MK_LBUTTON) == S_OK);
    assert(source->QueryContinueDrag(FALSE, MK_LBUTTON) == S_OK);
    assert(source->QueryContinueDrag(FALSE, 0) == DRAGDROP_S_DROP);
    source->Release();

    IDropSource* cancellable = createRemoteFileDropSource();
    assert(cancellable != nullptr);
    assert(cancellable->QueryContinueDrag(TRUE, 0) == DRAGDROP_S_CANCEL);
    cancellable->Release();
#endif
}

void dryRunPublishMaterializesText()
{
    WindowsClipboardEndpoint endpoint;
    const protocol::ResponseStatus status =
        endpoint.publishBundle(ClipboardPublishRequest{textBundle("published")});

    assert(status == protocol::ResponseStatus::Ok);
    assert(endpoint.dryRunClipboardText() == "published");
    assert(endpoint.diagnostics().publishes == 1);
    assert(endpoint.diagnostics().publishedOfferId == 22);
}

void dryRunPublishMaterializesRtf()
{
    WindowsClipboardEndpoint endpoint;
    const protocol::ResponseStatus status =
        endpoint.publishBundle(
            ClipboardPublishRequest{rtfBundle("{\\rtf1\\ansi published}")});

    assert(status == protocol::ResponseStatus::Ok);
    assert(endpoint.dryRunClipboardRtf() == "{\\rtf1\\ansi published}");
    assert(endpoint.dryRunClipboardText().empty());
    assert(endpoint.diagnostics().publishes == 1);
    assert(endpoint.diagnostics().publishedOfferId == 26);
}

void dryRunPublishMaterializesFileList()
{
    WindowsClipboardEndpoint endpoint;
    const protocol::ResponseStatus status =
        endpoint.publishBundle(ClipboardPublishRequest{fileListBundle()});

    assert(status == protocol::ResponseStatus::Ok);
    const TransferFileList fileList = endpoint.dryRunClipboardFileList();
    assert(fileList.files.size() == 1);
    assert(fileList.files.front().displayName == "___report.pdf");
    assert(fileList.files.front().sizeBytes == 4096);
    const TransferFileListDecodeResult descriptor =
        transferFileListFromWindowsFileGroupDescriptor(
            endpoint.dryRunFileGroupDescriptor());
    assert(descriptor.ok);
    assert(descriptor.fileList.files.front().displayName == "___report.pdf");
    assert(endpoint.diagnostics().fileListRenders == 1);
}

void dryRunPublishCanUseRemoteReader()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    WindowsClipboardEndpoint endpoint({}, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            77,
            std::vector<TransferFormatDescriptor>{descriptor}));

    const protocol::ResponseStatus status =
        endpoint.publishBundle(ClipboardPublishRequest{bundle});

    assert(status == protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastRequest.bundleId == 11);
    assert(reader->lastRequest.offerId == 22);
    assert(reader->lastRequest.sourceId == 77);
    assert(reader->lastTimeoutMs == 1000);
    assert(endpoint.dryRunClipboardText() == "remote text");
}

void dryRunDelayedRenderUsesRemoteReaderWhenRequested()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 42;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            77,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 0);

    assert(endpoint.renderDelayedFormatForNative(13) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastTimeoutMs == 42);
    assert(endpoint.dryRunClipboardText() == "remote text");
    assert(endpoint.diagnostics().delayedRenders == 1);
}

void dryRunRenderAllUsesRemoteReaderBeforeOwnerRelease()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 43;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            77,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);
    assert(endpoint.renderAllDelayedFormatsForNative() ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastTimeoutMs == 43);
    assert(endpoint.dryRunClipboardText() == "remote text");
    assert(endpoint.diagnostics().delayedRenderAlls == 1);
    assert(endpoint.diagnostics().delayedRenders == 1);
}

void dryRunDelayedRenderHtmlUsesRemoteReaderWhenRequested()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    reader->responseFormat = TextHtmlFormat;
    reader->responseText = "<b>remote</b>";

    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 45;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextHtmlFormat;
    descriptor.nativeFormatName = "HTML Format";
    descriptor.localFormatToken = windowsHtmlFormatToken();
    descriptor.formatId = 56;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 12;
    bundle.offerId = 23;
    bundle.ownerEpoch = 34;
    bundle.sequence = 45;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            78,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 0);

    assert(endpoint.renderDelayedFormatForNative(windowsHtmlFormatToken()) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastRequest.canonicalFormat == TextHtmlFormat);
    assert(reader->lastTimeoutMs == 45);
    assert(endpoint.dryRunClipboardHtml() == "<b>remote</b>");
    assert(endpoint.diagnostics().htmlRenders == 1);
}

void dryRunDelayedRenderRtfUsesRemoteReaderWhenRequested()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    reader->responseFormat = TextRtfFormat;
    reader->responseText = "{\\rtf1\\ansi remote}";

    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 48;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextRtfFormat;
    descriptor.nativeFormatName = "Rich Text Format";
    descriptor.localFormatToken = windowsRtfFormatToken();
    descriptor.formatId = 59;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 16;
    bundle.offerId = 27;
    bundle.ownerEpoch = 38;
    bundle.sequence = 49;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            82,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 0);

    assert(endpoint.renderDelayedFormatForNative(windowsRtfFormatToken()) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastRequest.canonicalFormat == TextRtfFormat);
    assert(reader->lastTimeoutMs == 48);
    assert(endpoint.dryRunClipboardRtf() == "{\\rtf1\\ansi remote}");
    assert(endpoint.diagnostics().rtfRenders == 1);
}

void dryRunDelayedRenderFileListUsesRemoteReaderWhenRequested()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    reader->responseFormat = FdclFileListFormat;
    reader->responseBytes = encodeTransferFileList(sampleFileList());

    WindowsClipboardEndpointOptions options;
    options.materializeTextOnPublish = false;
    options.delayedReadTimeoutMs = 46;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.localFormatToken = 0;
    descriptor.formatId = 57;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 13;
    bundle.offerId = 24;
    bundle.ownerEpoch = 35;
    bundle.sequence = 46;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            79,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 0);

    assert(endpoint.renderDelayedFormatForNative(
               windowsFileGroupDescriptorFormatToken()) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);
    assert(reader->lastRequest.canonicalFormat == FdclFileListFormat);
    assert(reader->lastRequest.localFormatToken == 0);
    assert(reader->lastTimeoutMs == 46);

    const TransferFileList fileList = endpoint.dryRunClipboardFileList();
    assert(fileList.files.size() == 1);
    assert(fileList.files.front().displayName == "___report.pdf");
    assert(endpoint.diagnostics().fileListRenders == 1);
}

TransferSourceBundle remoteTextBundle()
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "CF_UNICODETEXT";
    descriptor.localFormatToken = 13;
    descriptor.formatId = 55;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 22;
    bundle.ownerEpoch = 33;
    bundle.sequence = 44;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            77,
            std::vector<TransferFormatDescriptor>{descriptor}));
    return bundle;
}

#if defined(_WIN32)
std::string readNativeClipboardText()
{
    assert(OpenClipboard(nullptr));
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    assert(handle != nullptr);
    const SIZE_T size = GlobalSize(handle);
    const void* locked = GlobalLock(handle);
    assert(locked != nullptr);

    protocol::ByteBuffer nativeBytes(static_cast<std::size_t>(size));
    if (size > 0)
        std::memcpy(nativeBytes.data(), locked, static_cast<std::size_t>(size));
    GlobalUnlock(handle);
    CloseClipboard();

    const protocol::ByteBuffer canonical =
        canonicalUtf8FromWindowsCfUnicodeText(nativeBytes);
    return std::string(canonical.begin(), canonical.end());
}

void nativeDelayedRenderSmokeIfRequested()
{
    const char* enabled =
        std::getenv("FUSIONDESK_WINDOWS_CLIPBOARD_NATIVE_SMOKE");
    if (enabled == nullptr || std::string(enabled) != "1")
        return;

    auto reader = std::make_shared<FakeRemoteReader>();
    WindowsClipboardEndpointOptions options;
    options.dryRun = false;
    options.materializeTextOnPublish = false;
    options.useDelayedTextRendering = true;
    options.delayedReadTimeoutMs = 44;
    WindowsClipboardEndpoint endpoint(options, reader);

    assert(endpoint.publishBundle(ClipboardPublishRequest{remoteTextBundle()}) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 0);

    const std::string text = readNativeClipboardText();
    assert(text == "remote text");
    assert(reader->calls == 1);
    assert(reader->lastTimeoutMs == 44);
    assert(endpoint.diagnostics().delayedPublishes == 1);
    assert(endpoint.diagnostics().delayedRenders == 1);

    assert(endpoint.clearPublishedBundle(22) == protocol::ResponseStatus::Ok);
}

void nativeFileClipboardSmokeIfRequested()
{
    const char* enabled =
        std::getenv("FUSIONDESK_WINDOWS_CLIPBOARD_NATIVE_FILE_SMOKE");
    if (enabled == nullptr || std::string(enabled) != "1")
        return;

    auto reader = std::make_shared<FakeRemoteReader>();
    reader->responseFormat = FdclFileListFormat;
    reader->responseBytes = encodeTransferFileList(sampleFileList());
    reader->responseFileBytes = bytes("range");

    WindowsClipboardEndpointOptions options;
    options.dryRun = false;
    options.materializeTextOnPublish = false;
    options.useDelayedTextRendering = true;
    options.delayedReadTimeoutMs = 47;
    WindowsClipboardEndpoint endpoint(options, reader);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "FileGroupDescriptorW";
    descriptor.formatId = 58;
    descriptor.itemIndex = 0;
    descriptor.canInline = true;
    descriptor.canStream = true;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = 14;
    bundle.offerId = 25;
    bundle.ownerEpoch = 36;
    bundle.sequence = 47;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<RemoteFdclTransferSource>(
            80,
            std::vector<TransferFormatDescriptor>{descriptor}));

    assert(endpoint.publishBundle(ClipboardPublishRequest{bundle}) ==
           protocol::ResponseStatus::Ok);
    assert(reader->calls == 1);

    IDataObject* dataObject = nullptr;
    assert(OleGetClipboard(&dataObject) == S_OK);
    assert(dataObject != nullptr);

    FORMATETC descriptorFormat = {};
    descriptorFormat.cfFormat =
        static_cast<CLIPFORMAT>(windowsFileGroupDescriptorFormatToken());
    descriptorFormat.dwAspect = DVASPECT_CONTENT;
    descriptorFormat.lindex = -1;
    descriptorFormat.tymed = TYMED_HGLOBAL;

    STGMEDIUM descriptorMedium = {};
    assert(dataObject->GetData(&descriptorFormat, &descriptorMedium) == S_OK);
    const SIZE_T descriptorSize = GlobalSize(descriptorMedium.hGlobal);
    const void* descriptorLocked = GlobalLock(descriptorMedium.hGlobal);
    assert(descriptorLocked != nullptr);
    protocol::ByteBuffer descriptorBytes(static_cast<std::size_t>(descriptorSize));
    std::memcpy(descriptorBytes.data(), descriptorLocked, descriptorBytes.size());
    GlobalUnlock(descriptorMedium.hGlobal);
    ReleaseStgMedium(&descriptorMedium);

    const TransferFileListDecodeResult descriptorDecoded =
        transferFileListFromWindowsFileGroupDescriptor(descriptorBytes);
    assert(descriptorDecoded.ok);
    assert(descriptorDecoded.fileList.files.size() == 1);
    assert(descriptorDecoded.fileList.files.front().displayName == "___report.pdf");

    FORMATETC contentsFormat = {};
    contentsFormat.cfFormat =
        static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileContents"));
    contentsFormat.dwAspect = DVASPECT_CONTENT;
    contentsFormat.lindex = 0;
    contentsFormat.tymed = TYMED_ISTREAM;

    STGMEDIUM contentsMedium = {};
    assert(dataObject->GetData(&contentsFormat, &contentsMedium) == S_OK);
    char buffer[16] = {};
    ULONG readBytes = 0;
    assert(contentsMedium.pstm->Read(buffer, 5, &readBytes) == S_OK ||
           readBytes == 5);
    assert(readBytes == 5);
    assert(std::string(buffer, buffer + readBytes) == "range");
    ReleaseStgMedium(&contentsMedium);
    dataObject->Release();
    OleSetClipboard(nullptr);

    assert(reader->fileRangeCalls == 1);
    assert(reader->lastFileRangeRequest.objectId == 9001);
    assert(reader->lastFileRangeTimeoutMs == 47);
}
#else
void nativeDelayedRenderSmokeIfRequested()
{
}

void nativeFileClipboardSmokeIfRequested()
{
}
#endif

void clearPublishedBundleClearsDryRunText()
{
    WindowsClipboardEndpoint endpoint;
    assert(endpoint.publishBundle(ClipboardPublishRequest{textBundle("x")}) ==
           protocol::ResponseStatus::Ok);
    assert(endpoint.clearPublishedBundle(22) == protocol::ResponseStatus::Ok);
    assert(endpoint.dryRunClipboardText().empty());
    assert(endpoint.clearPublishedBundle(22) == protocol::ResponseStatus::NotFound);
}

} // namespace

int main()
{
    textConversionRoundTrips();
    htmlConversionRoundTrips();
    fileGroupDescriptorConversionRoundTrips();
    fileGroupDescriptorPreservesRelativePaths();
    dryRunSnapshotBuildsMaterializedTextBundle();
    dryRunSnapshotBuildsMaterializedRtfBundle();
    dryRunSnapshotBuildsFileListBundle();
    dryRunDragSinkRecordsCoordinates();
    nativeDragLoopRequiresExplicitOptIn();
    nativeDragPreflightBuildsOleDataObjectWithoutStartingLoop();
    remoteFileDropSourceWaitsForProgrammaticMouseClick();
    dryRunPublishMaterializesText();
    dryRunPublishMaterializesRtf();
    dryRunPublishMaterializesFileList();
    dryRunPublishCanUseRemoteReader();
    dryRunDelayedRenderUsesRemoteReaderWhenRequested();
    dryRunRenderAllUsesRemoteReaderBeforeOwnerRelease();
    dryRunDelayedRenderHtmlUsesRemoteReaderWhenRequested();
    dryRunDelayedRenderRtfUsesRemoteReaderWhenRequested();
    dryRunDelayedRenderFileListUsesRemoteReaderWhenRequested();
    nativeDelayedRenderSmokeIfRequested();
    nativeFileClipboardSmokeIfRequested();
    clearPublishedBundleClearsDryRunText();
    return 0;
}
