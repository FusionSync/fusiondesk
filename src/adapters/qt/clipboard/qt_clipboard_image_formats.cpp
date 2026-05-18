#include "fusiondesk/adapters/qt/clipboard/qt_clipboard_endpoint.h"

#include <chrono>
#include <utility>

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

bool descriptorIsImagePng(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == ImagePngFormat ||
           descriptor.nativeFormatName == "image/png";
}

} // namespace

TransferReadResult QtClipboardEndpoint::readBestImagePng(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          ImagePngFormat,
                          descriptorIsImagePng,
                          "qt clipboard image/png format is not found");
}

ClipboardSnapshot QtClipboardEndpoint::snapshotFromImagePng(
    const protocol::ByteBuffer& pngBytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t sequence)
{
    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImagePngFormat;
    descriptor.nativeFormatName = "image/png";
    descriptor.localFormatToken = 5;
    descriptor.formatId = nextFormatId_++;
    descriptor.estimatedBytes = pngBytes.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = pngBytes;

    TransferSourceBundle bundle;
    bundle.bundleId = nextBundleId_++;
    bundle.offerId = nextOfferId_++;
    bundle.ownerEpoch = sequence == 0 ? 1 : sequence;
    bundle.sequence = sequence;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.originSessionId = options_.originSessionId;
    bundle.policyVersion = options_.policyVersion;
    bundle.createdMonotonicUsec = monotonicNowUsec();
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        nextSourceId_++,
        std::vector<MaterializedTransferEntry>{std::move(entry)}));

    TransferPresentation presentation;
    presentation.displayName = "Qt clipboard image";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Image;
    IconRepresentation icon;
    icon.format = ImagePngFormat;
    icon.width = width;
    icon.height = height;
    icon.bytes = pngBytes.size();
    icon.sensitive = true;
    presentation.icons.push_back(icon);
    bundle.presentation = std::move(presentation);

    ClipboardSnapshot snapshot;
    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

} // namespace clipboard
} // namespace qt
} // namespace adapters
} // namespace fusiondesk
