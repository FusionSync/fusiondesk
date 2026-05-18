#include "pc_clipboard_shell.h"

#include "pc_shell_options.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#if defined(FUSIONDESK_PC_HAS_WINDOWS_FEATURE_ADAPTERS)
#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"
#endif

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

struct FormattedTextOption final {
    const char* seedOption = nullptr;
    const char* requireOption = nullptr;
    const char* canonicalFormat = nullptr;
    const char* nativeFormat = nullptr;
    modules::clipboard::TransferEncodingMode encoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;
    const char* label = nullptr;
};

const FormattedTextOption kFormattedTextOptions[] = {
    {"--clipboard-seed-html-file",
     "--require-clipboard-html-file",
     modules::clipboard::TextHtmlFormat,
     "HTML Format",
     modules::clipboard::TransferEncodingMode::CanonicalBytes,
     "text/html"},
    {"--clipboard-seed-rtf-file",
     "--require-clipboard-rtf-file",
     modules::clipboard::TextRtfFormat,
     "Rich Text Format",
     modules::clipboard::TransferEncodingMode::NativePassthrough,
     "text/rtf"},
};

protocol::ByteBuffer readFileBytes(const std::string& path,
                                   const char* label,
                                   std::string* errorMessage)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (errorMessage != nullptr)
            *errorMessage = std::string("failed to open ") + label +
                            " file: " + path;
        return {};
    }

    return protocol::ByteBuffer(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

const FormattedTextOption* requestedSeedOption(int argc, char** argv)
{
    const FormattedTextOption* requested = nullptr;
    for (const FormattedTextOption& option : kFormattedTextOptions) {
        if (optionValue(argc, argv, option.seedOption).empty())
            continue;
        if (requested != nullptr)
            return nullptr;
        requested = &option;
    }
    return requested;
}

bool multipleSeedOptionsRequested(int argc, char** argv)
{
    int count = 0;
    for (const FormattedTextOption& option : kFormattedTextOptions) {
        if (!optionValue(argc, argv, option.seedOption).empty())
            ++count;
    }
    return count > 1;
}

modules::clipboard::TransferSourceBundle makeFormattedTextSeedBundle(
    const FormattedTextOption& option,
    protocol::ByteBuffer bytes)
{
    using namespace modules::clipboard;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = option.canonicalFormat;
    descriptor.nativeFormatName = option.nativeFormat;
    descriptor.localFormatToken = 0;
    descriptor.formatId = 1;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = bytes.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = option.encoding;

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
    presentation.displayName = std::string("PC clipboard seed ") + option.label;
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Text;
    bundle.presentation = std::move(presentation);
    return bundle;
}

modules::clipboard::TransferReadResult readEndpointFormat(
    modules::clipboard::IClipboardEndpoint& endpoint,
    const FormattedTextOption& option)
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
            [&option](const TransferFormatDescriptor& descriptor) {
                return descriptor.canonicalFormat == option.canonicalFormat;
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
        request.canonicalFormat = option.canonicalFormat;
        request.acceptedMaxBytes = it->estimatedBytes == 0
                                       ? 1024ULL * 1024ULL
                                       : it->estimatedBytes;
        request.streamAccepted = false;
        request.requestedEncoding = option.encoding;
        return source->read(request);
    }

    TransferReadResult result;
    result.status = protocol::ResponseStatus::NotFound;
    result.message =
        std::string("clipboard endpoint ") + option.label +
        " format is not available";
    return result;
}

bool requirementSatisfiedForOption(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    const FormattedTextOption& option,
    std::string* errorMessage)
{
    const std::string expectedPath =
        optionValue(argc, argv, option.requireOption);
    if (expectedPath.empty())
        return true;

    if (endpoint == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage =
                std::string("clipboard ") + option.label +
                " requirement needs a clipboard endpoint";
        }
        return false;
    }

    std::string fileError;
    const protocol::ByteBuffer expected =
        readFileBytes(expectedPath, option.label, &fileError);
    if (!fileError.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = fileError;
        return false;
    }
    if (expected.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                std::string("clipboard ") + option.label +
                " requirement file is empty";
        }
        return false;
    }

    const modules::clipboard::TransferReadResult result =
        readEndpointFormat(*endpoint, option);
    if (!result.ok()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                std::string("clipboard ") + option.label +
                " requirement read failed: " + result.message;
        }
        return false;
    }

    if (result.bytes == expected)
        return true;

    if (errorMessage != nullptr) {
        *errorMessage =
            std::string("clipboard ") + option.label +
            " requirement failed: expectedBytes=" +
            std::to_string(expected.size()) +
            " actualBytes=" +
            std::to_string(result.bytes.size());
    }
    return false;
}

} // namespace

bool clipboardFormattedTextRequirementRequested(int argc, char** argv)
{
    for (const FormattedTextOption& option : kFormattedTextOptions) {
        if (!optionValue(argc, argv, option.requireOption).empty())
            return true;
    }
    return false;
}

bool clipboardFormattedTextRequirementSatisfied(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    std::string* errorMessage)
{
    for (const FormattedTextOption& option : kFormattedTextOptions) {
        if (!requirementSatisfiedForOption(argc,
                                           argv,
                                           endpoint,
                                           option,
                                           errorMessage)) {
            return false;
        }
    }
    return true;
}

bool verifyClipboardFormattedTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    std::string errorMessage;
    if (clipboardFormattedTextRequirementSatisfied(argc,
                                                   argv,
                                                   endpoint,
                                                   &errorMessage)) {
        return true;
    }

    writeShellError(errorMessage);
    return false;
}

bool seedClipboardFormattedTextIfRequested(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint)
{
    if (multipleSeedOptionsRequested(argc, argv)) {
        writeShellError(
            "only one formatted clipboard seed file can be used at a time");
        return false;
    }

    const FormattedTextOption* option = requestedSeedOption(argc, argv);
    if (option == nullptr)
        return true;

    if (endpoint == nullptr) {
        writeShellError(std::string("clipboard seed ") + option->label +
                        " needs a clipboard endpoint");
        return false;
    }

    std::string fileError;
    protocol::ByteBuffer seed =
        readFileBytes(optionValue(argc, argv, option->seedOption),
                      option->label,
                      &fileError);
    if (!fileError.empty()) {
        writeShellError(fileError);
        return false;
    }
    if (seed.empty()) {
        writeShellError(std::string("clipboard seed ") + option->label +
                        " file is empty");
        return false;
    }

    const protocol::ResponseStatus status =
        endpoint->publishBundle(modules::clipboard::ClipboardPublishRequest{
            makeFormattedTextSeedBundle(*option, std::move(seed))});
    if (status == protocol::ResponseStatus::Ok)
        return true;

    std::string message =
        std::string("clipboard seed ") + option->label +
        " publish failed: status=" +
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
