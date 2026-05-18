#include "fusiondesk/platform/macos/clipboard/mac_clipboard_endpoint.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
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

    auto fakeFileReader = std::make_shared<FakeRemoteFileReader>();
    macclip::MacClipboardEndpoint endpoint(options, {}, fakeFileReader);
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

    const clipboard::TransferSourceBundle fileBundle = makeFilePromiseBundle();
    const fusiondesk::protocol::ResponseStatus filePublishStatus =
        endpoint.publishBundle(clipboard::ClipboardPublishRequest{fileBundle});
    assert(filePublishStatus == fusiondesk::protocol::ResponseStatus::Ok);

    const macclip::MacClipboardEndpointDiagnostics fileDiagnostics =
        endpoint.diagnostics();
    assert(fileDiagnostics.remoteFilePromisePublishes >= 1);
    assert(fileDiagnostics.remoteFilePromiseProviders >= 1);
    assert(fileDiagnostics.publishedOfferId == fileBundle.offerId);
    return 0;
}
