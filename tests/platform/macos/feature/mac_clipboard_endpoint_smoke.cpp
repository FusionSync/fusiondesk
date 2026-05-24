#include "fusiondesk/platform/macos/clipboard/mac_clipboard_endpoint.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace clipboard = fusiondesk::modules::clipboard;
namespace macclip = fusiondesk::platform::macos::clipboard;

namespace {

clipboard::TransferSourceBundle makeTextBundle(const std::string& text)
{
    clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = clipboard::TextPlainUtf8Format;
    descriptor.nativeFormatName = "public.utf8-plain-text";
    descriptor.localFormatToken = 1;
    descriptor.formatId = 1;
    descriptor.estimatedBytes = text.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = clipboard::TransferEncodingMode::CanonicalBytes;

    clipboard::MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes.assign(text.begin(), text.end());

    clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 1;
    bundle.offerId = 1;
    bundle.ownerEpoch = 1;
    bundle.sequence = 1;
    bundle.origin = clipboard::TransferOrigin::RemoteOffer;
    bundle.side = clipboard::TransferSide::Remote;
    bundle.sources.push_back(std::make_shared<clipboard::MaterializedTransferSource>(
        1,
        std::vector<clipboard::MaterializedTransferEntry>{entry}));
    return bundle;
}

clipboard::TransferSourceBundle makeFormattedTextBundle(
    const std::string& text,
    const std::string& html,
    const std::string& rtf)
{
    std::vector<clipboard::MaterializedTransferEntry> entries;

    clipboard::TransferFormatDescriptor htmlDescriptor;
    htmlDescriptor.canonicalFormat = clipboard::TextHtmlFormat;
    htmlDescriptor.nativeFormatName = "public.html";
    htmlDescriptor.localFormatToken = 3;
    htmlDescriptor.formatId = 3;
    htmlDescriptor.estimatedBytes = html.size();
    htmlDescriptor.canInline = true;
    htmlDescriptor.canStream = false;
    htmlDescriptor.preferredEncoding =
        clipboard::TransferEncodingMode::CanonicalBytes;
    clipboard::MaterializedTransferEntry htmlEntry;
    htmlEntry.descriptor = htmlDescriptor;
    htmlEntry.bytes.assign(html.begin(), html.end());
    entries.push_back(std::move(htmlEntry));

    clipboard::TransferFormatDescriptor rtfDescriptor;
    rtfDescriptor.canonicalFormat = clipboard::TextRtfFormat;
    rtfDescriptor.nativeFormatName = "public.rtf";
    rtfDescriptor.localFormatToken = 4;
    rtfDescriptor.formatId = 4;
    rtfDescriptor.estimatedBytes = rtf.size();
    rtfDescriptor.canInline = true;
    rtfDescriptor.canStream = false;
    rtfDescriptor.preferredEncoding =
        clipboard::TransferEncodingMode::CanonicalBytes;
    clipboard::MaterializedTransferEntry rtfEntry;
    rtfEntry.descriptor = rtfDescriptor;
    rtfEntry.bytes.assign(rtf.begin(), rtf.end());
    entries.push_back(std::move(rtfEntry));

    clipboard::TransferFormatDescriptor textDescriptor;
    textDescriptor.canonicalFormat = clipboard::TextPlainUtf8Format;
    textDescriptor.nativeFormatName = "public.utf8-plain-text";
    textDescriptor.localFormatToken = 1;
    textDescriptor.formatId = 1;
    textDescriptor.estimatedBytes = text.size();
    textDescriptor.canInline = true;
    textDescriptor.canStream = false;
    textDescriptor.preferredEncoding =
        clipboard::TransferEncodingMode::CanonicalBytes;
    clipboard::MaterializedTransferEntry textEntry;
    textEntry.descriptor = textDescriptor;
    textEntry.bytes.assign(text.begin(), text.end());
    entries.push_back(std::move(textEntry));

    clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 3;
    bundle.offerId = 3;
    bundle.ownerEpoch = 1;
    bundle.sequence = 3;
    bundle.origin = clipboard::TransferOrigin::RemoteOffer;
    bundle.side = clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<clipboard::MaterializedTransferSource>(
            3,
            std::move(entries)));
    return bundle;
}

clipboard::TransferSourceBundle makeFilePromiseBundle()
{
    clipboard::TransferFileDescriptor file;
    file.objectId = 1;
    file.displayName = "macos-promised-file.txt";
    file.relativePath = file.displayName;
    file.sizeBytes = 11;
    file.directory = false;

    clipboard::TransferFileList fileList;
    fileList.files.push_back(file);

    clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = clipboard::FdclFileListFormat;
    descriptor.nativeFormatName = "public.file-url";
    descriptor.localFormatToken = 2;
    descriptor.formatId = 2;
    descriptor.estimatedBytes =
        clipboard::encodeTransferFileList(fileList).size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = clipboard::TransferEncodingMode::CanonicalBytes;

    clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 2;
    bundle.offerId = 2;
    bundle.ownerEpoch = 1;
    bundle.sequence = 2;
    bundle.origin = clipboard::TransferOrigin::RemoteOffer;
    bundle.side = clipboard::TransferSide::Remote;
    bundle.sources.push_back(std::make_shared<clipboard::FileGroupTransferSource>(
        2,
        descriptor,
        fileList));
    return bundle;
}

clipboard::TransferSourceBundle makeRemoteOnlyTextBundle()
{
    clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = clipboard::TextPlainUtf8Format;
    descriptor.nativeFormatName = "public.utf8-plain-text";
    descriptor.localFormatToken = 1;
    descriptor.formatId = 11;
    descriptor.itemIndex = 2;
    descriptor.estimatedBytes = 32;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding =
        clipboard::TransferEncodingMode::CanonicalBytes;

    clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 12;
    bundle.ownerEpoch = 13;
    bundle.sequence = 14;
    bundle.origin = clipboard::TransferOrigin::RemoteOffer;
    bundle.side = clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<clipboard::RemoteFdclTransferSource>(
            15,
            std::vector<clipboard::TransferFormatDescriptor>{descriptor}));
    return bundle;
}

class FakeRemoteFormatReader final : public clipboard::IClipboardRemoteReader
{
public:
    clipboard::TransferReadResult readRemoteFormat(
        const clipboard::TransferReadRequest& request,
        std::uint32_t timeoutMs) override
    {
        ++calls;
        lastRequest = request;
        lastTimeoutMs = timeoutMs;

        clipboard::TransferReadResult result;
        result.status = status;
        result.canonicalFormat = request.canonicalFormat;
        result.encoding = clipboard::TransferEncodingMode::CanonicalBytes;
        result.bytes.assign(payload.begin(), payload.end());
        result.message = message;
        return result;
    }

    int calls = 0;
    clipboard::TransferReadRequest lastRequest;
    std::uint32_t lastTimeoutMs = 0;
    fusiondesk::protocol::ResponseStatus status =
        fusiondesk::protocol::ResponseStatus::Ok;
    std::string payload = "remote macOS text";
    std::string message;
};

class FakeRemoteFileReader final : public clipboard::IClipboardRemoteFileReader
{
public:
    clipboard::TransferFileRangeResult readRemoteFileRange(
        const clipboard::TransferFileRangeRequest& request,
        std::uint32_t) override
    {
        clipboard::TransferFileRangeResult result;
        const std::string content = "hello macOS";
        if (request.offset >= content.size()) {
            result.status = fusiondesk::protocol::ResponseStatus::Ok;
            result.endOfFile = true;
            return result;
        }
        const std::size_t count = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                request.requestedBytes,
                content.size() - static_cast<std::size_t>(request.offset)));
        result.bytes.assign(content.begin() +
                                static_cast<std::ptrdiff_t>(request.offset),
                            content.begin() +
                                static_cast<std::ptrdiff_t>(request.offset + count));
        result.status = fusiondesk::protocol::ResponseStatus::Ok;
        result.endOfFile = request.offset + count >= content.size();
        return result;
    }
};

void validatesPureMacEndpointRejections()
{
    macclip::MacClipboardEndpointOptions options;
    options.enableChangeMonitor = false;
    macclip::MacClipboardEndpoint endpoint(options);

    assert(endpoint.hasPendingClipboardChange());
    assert(endpoint.publishBundle(clipboard::ClipboardPublishRequest{}) ==
           fusiondesk::protocol::ResponseStatus::InvalidArgument);

    clipboard::TransferReadResult missing =
        endpoint.renderPasteboardDataForNativeType("public.utf8-plain-text");
    assert(missing.status == fusiondesk::protocol::ResponseStatus::NotFound);
    assert(endpoint.clearPublishedBundle(999) ==
           fusiondesk::protocol::ResponseStatus::NotFound);

    const macclip::MacClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.publishes >= 1);
    assert(diagnostics.delayedRenders >= 1);
    assert(diagnostics.clears >= 1);
}

void validatesFilePromisePreflightFailures()
{
    macclip::MacClipboardEndpoint endpointWithoutReader;
    assert(endpointWithoutReader.publishBundle(
               clipboard::ClipboardPublishRequest{makeFilePromiseBundle()}) ==
           fusiondesk::protocol::ResponseStatus::Failed);
    macclip::MacClipboardEndpointDiagnostics missingReaderDiagnostics =
        endpointWithoutReader.diagnostics();
    assert(missingReaderDiagnostics.remoteFilePromiseFailures >= 1);

    macclip::MacClipboardEndpointOptions limitedOptions;
    limitedOptions.maxSingleFileBytes = 5;
    auto fakeFileReader = std::make_shared<FakeRemoteFileReader>();
    macclip::MacClipboardEndpoint endpointWithLimit(limitedOptions,
                                                    {},
                                                    fakeFileReader);
    assert(endpointWithLimit.publishBundle(
               clipboard::ClipboardPublishRequest{makeFilePromiseBundle()}) ==
           fusiondesk::protocol::ResponseStatus::TooLarge);
    macclip::MacClipboardEndpointDiagnostics tooLargeDiagnostics =
        endpointWithLimit.diagnostics();
    assert(tooLargeDiagnostics.remoteFilePromiseFailures >= 1);
}

void validatesNativeTextPublication()
{
    auto fakeFileReader = std::make_shared<FakeRemoteFileReader>();
    macclip::MacClipboardEndpoint endpoint({}, {}, fakeFileReader);
    const std::string expected = "fusiondesk macOS clipboard smoke";
    const clipboard::TransferSourceBundle bundle = makeTextBundle(expected);
    const fusiondesk::protocol::ResponseStatus publishStatus =
        endpoint.publishBundle(clipboard::ClipboardPublishRequest{bundle});
    assert(publishStatus == fusiondesk::protocol::ResponseStatus::Ok);

    clipboard::TransferReadResult rendered =
        endpoint.renderPasteboardDataForNativeType("public.utf8-plain-text");
    assert(rendered.ok());
    assert(std::string(rendered.bytes.begin(), rendered.bytes.end()) == expected);

    const macclip::MacClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.publishes >= 1);
    assert(diagnostics.delayedPublishes >= 1);
    assert(diagnostics.publishedOfferId == bundle.offerId);
}

void validatesNativeFormattedTextPublication()
{
    macclip::MacClipboardEndpoint endpoint;
    const std::string text = "formatted macOS clipboard";
    const std::string html = "<p>formatted macOS clipboard</p>";
    const std::string rtf = "{\\rtf1 formatted macOS clipboard}";
    const clipboard::TransferSourceBundle bundle =
        makeFormattedTextBundle(text, html, rtf);

    assert(endpoint.publishBundle(
               clipboard::ClipboardPublishRequest{bundle}) ==
           fusiondesk::protocol::ResponseStatus::Ok);

    clipboard::TransferReadResult renderedHtml =
        endpoint.renderPasteboardDataForNativeType("public.html");
    assert(renderedHtml.ok());
    assert(std::string(renderedHtml.bytes.begin(),
                       renderedHtml.bytes.end()) == html);

    clipboard::TransferReadResult renderedRtf =
        endpoint.renderPasteboardDataForNativeType("public.rtf");
    assert(renderedRtf.ok());
    assert(std::string(renderedRtf.bytes.begin(), renderedRtf.bytes.end()) ==
           rtf);

    clipboard::TransferReadResult renderedText =
        endpoint.renderPasteboardDataForNativeType("public.utf8-plain-text");
    assert(renderedText.ok());
    assert(std::string(renderedText.bytes.begin(), renderedText.bytes.end()) ==
           text);

    clipboard::TransferReadResult unsupported =
        endpoint.renderPasteboardDataForNativeType("public.unknown");
    assert(unsupported.status ==
           fusiondesk::protocol::ResponseStatus::NotFound);
    assert(endpoint.clearPublishedBundle(bundle.offerId) ==
           fusiondesk::protocol::ResponseStatus::Ok);
    assert(endpoint.renderPasteboardDataForNativeType(
               "public.utf8-plain-text")
               .status == fusiondesk::protocol::ResponseStatus::NotFound);
}

void validatesRemoteReaderFallback()
{
    macclip::MacClipboardEndpointOptions options;
    options.delayedReadTimeoutMs = 77;
    auto remoteReader = std::make_shared<FakeRemoteFormatReader>();
    macclip::MacClipboardEndpoint endpoint(options, remoteReader);
    const clipboard::TransferSourceBundle bundle = makeRemoteOnlyTextBundle();

    assert(endpoint.publishBundle(
               clipboard::ClipboardPublishRequest{bundle}) ==
           fusiondesk::protocol::ResponseStatus::Ok);

    clipboard::TransferReadResult rendered =
        endpoint.renderPasteboardDataForNativeType("public.utf8-plain-text");
    assert(rendered.ok());
    assert(std::string(rendered.bytes.begin(), rendered.bytes.end()) ==
           remoteReader->payload);
    assert(remoteReader->calls == 1);
    assert(remoteReader->lastTimeoutMs == 77);
    assert(remoteReader->lastRequest.bundleId == bundle.bundleId);
    assert(remoteReader->lastRequest.offerId == bundle.offerId);
    assert(remoteReader->lastRequest.ownerEpoch == bundle.ownerEpoch);
    assert(remoteReader->lastRequest.sourceId == 15);
    assert(remoteReader->lastRequest.itemIndex == 2);
    assert(remoteReader->lastRequest.formatId == 11);
    assert(remoteReader->lastRequest.localFormatToken == 1);
    assert(remoteReader->lastRequest.canonicalFormat ==
           clipboard::TextPlainUtf8Format);
    assert(remoteReader->lastRequest.acceptedMaxBytes ==
           options.maxInlineBytes);
    assert(!remoteReader->lastRequest.streamAccepted);
    assert(remoteReader->lastRequest.requestedEncoding ==
           clipboard::TransferEncodingMode::CanonicalBytes);
}

void validatesNativeFilePromisePublication()
{
    auto fakeFileReader = std::make_shared<FakeRemoteFileReader>();
    macclip::MacClipboardEndpoint endpoint({}, {}, fakeFileReader);
    const clipboard::TransferSourceBundle fileBundle = makeFilePromiseBundle();
    const fusiondesk::protocol::ResponseStatus filePublishStatus =
        endpoint.publishBundle(clipboard::ClipboardPublishRequest{fileBundle});
    assert(filePublishStatus == fusiondesk::protocol::ResponseStatus::Ok);

    const macclip::MacClipboardEndpointDiagnostics fileDiagnostics =
        endpoint.diagnostics();
    assert(fileDiagnostics.remoteFilePromisePublishes >= 1);
    assert(fileDiagnostics.remoteFilePromiseProviders >= 1);
    assert(fileDiagnostics.publishedOfferId == fileBundle.offerId);
}

} // namespace

int main()
{
    macclip::MacClipboardEndpointOptions options;
    options.enableChangeMonitor = true;
    options.suppressOwnClipboardUpdates = true;
    assert(options.firstBundleId == 1);

    const char* validate =
        std::getenv("FUSIONDESK_VALIDATE_MACOS_NATIVE_CLIPBOARD");
    if (validate == nullptr || std::string(validate) != "1") {
        std::cout << "macOS native clipboard validation skipped; set "
                     "FUSIONDESK_VALIDATE_MACOS_NATIVE_CLIPBOARD=1"
                  << std::endl;
        return 0;
    }

    validatesPureMacEndpointRejections();
    validatesFilePromisePreflightFailures();
    validatesNativeTextPublication();
    validatesNativeFormattedTextPublication();
    validatesRemoteReaderFallback();
    validatesNativeFilePromisePublication();
    return 0;
}
