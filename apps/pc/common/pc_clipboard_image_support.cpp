#include "pc_clipboard_shell.h"

#include "pc_shell_options.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"
#endif

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

protocol::ByteBuffer readBinaryFile(const std::string& path,
                                     std::string* errorMessage)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (errorMessage != nullptr)
            *errorMessage = "failed to open image/png file: " + path;
        return {};
    }

    return protocol::ByteBuffer(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

modules::clipboard::TransferSourceBundle makeImagePngSeedBundle(
    protocol::ByteBuffer bytes)
{
    using namespace modules::clipboard;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImagePngFormat;
    descriptor.nativeFormatName = "PNG";
    descriptor.localFormatToken = 0;
    descriptor.formatId = 1;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = bytes.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = std::move(bytes);

    TransferSourceBundle bundle;
    bundle.bundleId = 1;
    bundle.offerId = 1;
    bundle.ownerEpoch = 1;
    bundle.sequence = 1;
    bundle.origin = TransferOrigin::Clipboard;
    bundle.side = TransferSide::Local;
    bundle.sources.push_back(std::make_shared<MaterializedTransferSource>(
        1,
        std::vector<MaterializedTransferEntry>{std::move(entry)}));

    TransferPresentation presentation;
    presentation.displayName = "PC clipboard seed image";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Image;
    IconRepresentation icon;
    icon.format = ImagePngFormat;
    icon.bytes = descriptor.estimatedBytes;
    presentation.icons.push_back(icon);
    bundle.presentation = std::move(presentation);
    return bundle;
}

modules::clipboard::TransferReadResult readEndpointImagePng(
    modules::clipboard::IClipboardEndpoint& endpoint)
{
    using namespace modules::clipboard;

    const ClipboardSnapshot snapshot = endpoint.snapshot();
    for (const std::shared_ptr<TransferSource>& source :
         snapshot.bundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        auto it = std::find_if(
            formats.begin(),
            formats.end(),
            [](const TransferFormatDescriptor& descriptor) {
                return descriptor.canonicalFormat == ImagePngFormat;
            });
        if (it == formats.end())
            continue;

        TransferReadRequest request;
        request.bundleId = snapshot.bundle.bundleId;
        request.offerId = snapshot.bundle.offerId;
        request.ownerEpoch = snapshot.bundle.ownerEpoch;
        request.sourceId = source->id();
        request.itemIndex = it->itemIndex;
        request.formatId = it->formatId;
        request.localFormatToken = it->localFormatToken;
        request.canonicalFormat = ImagePngFormat;
        request.acceptedMaxBytes = it->estimatedBytes == 0
                                       ? 16ULL * 1024ULL * 1024ULL
                                       : it->estimatedBytes;
        request.streamAccepted = false;
        request.requestedEncoding = TransferEncodingMode::CanonicalBytes;
        TransferReadResult result = source->read(request);
        if (result.ok())
            return result;
        return result;
    }

    TransferReadResult result;
    result.status = protocol::ResponseStatus::NotFound;
    result.message = "clipboard endpoint image/png format is not available";
    return result;
}

} // namespace

bool clipboardImagePngRequirementRequested(int argc, char** argv)
{
    return !optionValue(argc, argv, "--require-clipboard-image-png").empty();
}

bool clipboardImagePngRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage)
{
    const std::string expectedPath =
        optionValue(argc, argv, "--require-clipboard-image-png");
    if (expectedPath.empty())
        return true;

    if (endpoint == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage =
                "clipboard image/png requirement needs a clipboard endpoint";
        return false;
    }

    std::string fileError;
    const protocol::ByteBuffer expected =
        readBinaryFile(expectedPath, &fileError);
    if (!fileError.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = fileError;
        return false;
    }
    if (expected.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard image/png requirement file is empty";
        return false;
    }

    const modules::clipboard::TransferReadResult result =
        readEndpointImagePng(*endpoint);
    if (!result.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard image/png requirement read failed: " +
                result.message;
        }
        return false;
    }

    if (result.bytes == expected)
        return true;

    if (errorMessage != nullptr) {
        *errorMessage =
            "clipboard image/png requirement failed: expectedBytes=" +
            std::to_string(expected.size()) +
            " actualBytes=" +
            std::to_string(result.bytes.size());
    }
    return false;
}

bool verifyClipboardImagePngRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    std::string errorMessage;
    if (clipboardImagePngRequirementSatisfied(argc,
                                              argv,
                                              endpoint,
                                              &errorMessage)) {
        return true;
    }

    writeShellError(errorMessage);
    return false;
}

bool seedClipboardImagePngIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    const std::string seedPath =
        optionValue(argc, argv, "--clipboard-seed-image-png");
    if (seedPath.empty())
        return true;

    if (endpoint == nullptr) {
        writeShellError("clipboard seed image/png needs a clipboard endpoint");
        return false;
    }

    std::string fileError;
    protocol::ByteBuffer seed = readBinaryFile(seedPath, &fileError);
    if (!fileError.empty()) {
        writeShellError(fileError);
        return false;
    }
    if (seed.empty()) {
        writeShellError("clipboard seed image/png file is empty");
        return false;
    }

    const protocol::ResponseStatus status =
        endpoint->publishBundle(modules::clipboard::ClipboardPublishRequest{
            makeImagePngSeedBundle(std::move(seed))});
    if (status == protocol::ResponseStatus::Ok)
        return true;

    std::string message =
        "clipboard seed image/png publish failed: status=" +
        std::to_string(static_cast<int>(status));
#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
    auto windowsEndpoint =
        std::dynamic_pointer_cast<
            platform::windows::clipboard::WindowsClipboardEndpoint>(endpoint);
    if (windowsEndpoint != nullptr) {
        const platform::windows::clipboard::WindowsClipboardEndpointDiagnostics
            diagnostics = windowsEndpoint->diagnostics();
        message += " nativeFailures=" +
                   std::to_string(diagnostics.nativeFailures) +
                   " lastNativeError=" +
                   std::to_string(diagnostics.lastNativeError) +
                   " message=" + diagnostics.lastMessage;
    }
#endif
    writeShellError(message);
    return false;
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
