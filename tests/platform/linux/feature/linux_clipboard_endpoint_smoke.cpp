#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/platform/linux/clipboard/linux_clipboard_endpoint.h"

namespace {

fusiondesk::protocol::ByteBuffer bytes(const std::string& value)
{
    return fusiondesk::protocol::ByteBuffer(value.begin(), value.end());
}

fusiondesk::modules::clipboard::TransferSourceBundle makeTextBundle()
{
    using namespace fusiondesk::modules::clipboard;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = "text/plain;charset=utf-8";
    descriptor.formatId = 3;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 5;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytes("hello");

    TransferSourceBundle bundle;
    bundle.bundleId = 1;
    bundle.offerId = 2;
    bundle.ownerEpoch = 3;
    bundle.sequence = 4;
    bundle.origin = TransferOrigin::RemoteOffer;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            7,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

fusiondesk::modules::clipboard::TransferSourceBundle makeFileListBundle()
{
    using namespace fusiondesk::modules::clipboard;

    TransferFileDescriptor file;
    file.objectId = 9;
    file.displayName = "remote.txt";
    file.relativePath = "remote.txt";
    file.sizeBytes = 12;
    file.directory = false;

    TransferFileList fileList;
    fileList.files.push_back(file);

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "application/x-fdcl-file-list";
    descriptor.formatId = 4;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 128;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = encodeTransferFileList(fileList);

    TransferSourceBundle bundle;
    bundle.bundleId = 11;
    bundle.offerId = 12;
    bundle.ownerEpoch = 13;
    bundle.sequence = 14;
    bundle.origin = TransferOrigin::RemoteOffer;
    bundle.side = TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<MaterializedTransferSource>(
            15,
            std::vector<MaterializedTransferEntry>{entry}));
    return bundle;
}

class FakeRemoteFileReader final
    : public fusiondesk::modules::clipboard::IClipboardRemoteFileReader
{
public:
    fusiondesk::modules::clipboard::TransferFileRangeResult readRemoteFileRange(
        const fusiondesk::modules::clipboard::TransferFileRangeRequest&,
        std::uint32_t) override
    {
        fusiondesk::modules::clipboard::TransferFileRangeResult result;
        result.status = fusiondesk::protocol::ResponseStatus::Ok;
        result.bytes = bytes("remote bytes");
        result.endOfFile = true;
        return result;
    }
};

void fakeBackendStartsAndReportsEmptySnapshot()
{
    namespace linuxclip = fusiondesk::platform::linux_desktop::clipboard;

    linuxclip::LinuxClipboardEndpointOptions options;
    options.backend = linuxclip::LinuxClipboardBackend::Fake;
    options.requestTimeoutMs = 1000;
    options.ownerWindowName = "FusionDesk Linux Clipboard Smoke";

    linuxclip::LinuxClipboardEndpoint endpoint(options);
    linuxclip::LinuxClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.started);

    const fusiondesk::modules::clipboard::ClipboardSnapshot snapshot =
        endpoint.snapshot();
    assert(snapshot.bundle.sources.empty());

    diagnostics = endpoint.diagnostics();
    assert(diagnostics.snapshots == 1);
    assert(diagnostics.targetListReads == 1);
}

void fakeBackendPublishesAndClearsPromisedTargets()
{
    namespace linuxclip = fusiondesk::platform::linux_desktop::clipboard;
    using namespace fusiondesk::modules::clipboard;

    linuxclip::LinuxClipboardEndpointOptions options;
    options.backend = linuxclip::LinuxClipboardBackend::Fake;
    options.requestTimeoutMs = 1000;

    linuxclip::LinuxClipboardEndpoint endpoint(options);
    assert(endpoint.publishBundle(ClipboardPublishRequest{makeTextBundle()}) ==
           fusiondesk::protocol::ResponseStatus::Ok);

    linuxclip::LinuxClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.publishes == 1);
    assert(diagnostics.delayedPublishes == 1);
    assert(diagnostics.publishedOfferId == 2);

    assert(endpoint.clearPublishedBundle(2) ==
           fusiondesk::protocol::ResponseStatus::Ok);
    diagnostics = endpoint.diagnostics();
    assert(diagnostics.clears == 1);
    assert(diagnostics.publishedOfferId == 0);
}

void fakeBackendRejectsRemoteFileListWhenFusePromiseDisabled()
{
    namespace linuxclip = fusiondesk::platform::linux_desktop::clipboard;
    using namespace fusiondesk::modules::clipboard;

    linuxclip::LinuxClipboardEndpointOptions options;
    options.backend = linuxclip::LinuxClipboardBackend::Fake;
    options.requestTimeoutMs = 1000;
    options.enableFusePromise = false;

    linuxclip::LinuxClipboardEndpoint endpoint(
        options,
        {},
        std::make_shared<FakeRemoteFileReader>());
    assert(endpoint.publishBundle(ClipboardPublishRequest{makeFileListBundle()}) ==
           fusiondesk::protocol::ResponseStatus::Unsupported);

    const linuxclip::LinuxClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.remoteFilePromiseFailures == 1);
    assert(diagnostics.remoteFilePromisePublishes == 0);
    assert(!diagnostics.fusePromiseActive);
}

} // namespace

int main()
{
    fakeBackendStartsAndReportsEmptySnapshot();
    fakeBackendPublishesAndClearsPromisedTargets();
    fakeBackendRejectsRemoteFileListWhenFusePromiseDisabled();
    return 0;
}
