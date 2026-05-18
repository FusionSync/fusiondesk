#include "pc_clipboard_file_save.h"

#include "pc_shell_options.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/clipboard/clipboard_modules.h"
#include "fusiondesk/modules/clipboard/clipboard_transfer.h"

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

struct RemoteFileListView final {
    modules::clipboard::TransferSourceBundle bundle;
    modules::clipboard::TransferSourceId sourceId = 0;
    modules::clipboard::TransferFileList fileList;
};

modules::clipboard::ClipboardModuleBase* clipboardModuleForSession(
    session::Session& session)
{
    if (session.moduleHost() == nullptr)
        return nullptr;

    const std::string moduleId =
        session.role() == session::SessionRole::Client
            ? "clipboard.redirect.client"
            : "clipboard.redirect.agent";
    return dynamic_cast<modules::clipboard::ClipboardModuleBase*>(
        session.moduleHost()->module(moduleId));
}

RemoteFileListView remoteFileListView(
    session::Session& session,
    modules::clipboard::IClipboardRemoteReader* remoteReader,
    std::uint32_t timeoutMs,
    std::string* errorMessage)
{
    using namespace modules::clipboard;

    RemoteFileListView view;
    ClipboardModuleBase* module = clipboardModuleForSession(session);
    if (module == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard file save needs a running module";
        return view;
    }

    const ClipboardModuleSnapshot snapshot = module->snapshot();
    if (snapshot.remoteBundle.offerId == 0 ||
        snapshot.remoteBundle.sources.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard remote file list is not available";
        return view;
    }

    for (const std::shared_ptr<TransferSource>& source :
         snapshot.remoteBundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        for (const TransferFormatDescriptor& descriptor : formats) {
            if (descriptor.canonicalFormat != FdclFileListFormat)
                continue;

            TransferReadRequest request;
            request.bundleId = snapshot.remoteBundle.bundleId;
            request.offerId = snapshot.remoteBundle.offerId;
            request.ownerEpoch = snapshot.remoteBundle.ownerEpoch;
            request.sourceId = source->id();
            request.itemIndex = descriptor.itemIndex;
            request.formatId = descriptor.formatId;
            request.localFormatToken = descriptor.localFormatToken;
            request.canonicalFormat = FdclFileListFormat;
            request.acceptedMaxBytes = descriptor.estimatedBytes == 0
                                           ? 1024 * 1024
                                           : descriptor.estimatedBytes;
            request.streamAccepted = false;
            request.requestedEncoding =
                TransferEncodingMode::CanonicalBytes;

            TransferReadResult result = source->read(request);
            if (!result.ok() && remoteReader != nullptr)
                result = remoteReader->readRemoteFormat(request, timeoutMs);
            if (!result.ok()) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard remote file list read failed: " +
                        result.message;
                }
                return view;
            }

            const TransferFileListDecodeResult decoded =
                decodeTransferFileList(result.bytes);
            if (!decoded.ok) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard remote file list decode failed: " +
                        decoded.message;
                }
                return view;
            }

            view.bundle = snapshot.remoteBundle;
            view.sourceId = source->id();
            view.fileList = decoded.fileList;
            return view;
        }
    }

    if (errorMessage != nullptr)
        *errorMessage = "clipboard remote file-list format is not available";
    return view;
}

struct RemoteObjectLockGuard final {
    ~RemoteObjectLockGuard()
    {
        unlockSilently();
    }

    RemoteObjectLockGuard() = default;
    RemoteObjectLockGuard(const RemoteObjectLockGuard&) = delete;
    RemoteObjectLockGuard& operator=(const RemoteObjectLockGuard&) = delete;

    bool lock(modules::clipboard::IClipboardRemoteObjectLocker& locker,
              modules::clipboard::TransferObjectLockRequest request,
              std::uint32_t timeoutMs,
              std::string* errorMessage)
    {
        locker_ = &locker;
        request_ = request;
        timeoutMs_ = timeoutMs;

        const modules::clipboard::TransferObjectLockResult result =
            locker_->lockRemoteObject(request_, timeoutMs_);
        if (!result.ok()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file object lock failed: " +
                    result.message;
            }
            return false;
        }
        if (result.lockId == 0) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file object lock returned empty lock id";
            }
            return false;
        }

        request_.lockId = result.lockId;
        request_.leaseUsec = result.leaseUsec;
        locked_ = true;
        return true;
    }

    bool unlock(std::string* errorMessage)
    {
        if (!locked_ || locker_ == nullptr)
            return true;

        const modules::clipboard::TransferObjectLockResult result =
            locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
        if (result.ok())
            return true;

        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard remote file object unlock failed: " +
                result.message;
        }
        return false;
    }

private:
    void unlockSilently()
    {
        if (!locked_ || locker_ == nullptr)
            return;

        locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
    }

    modules::clipboard::IClipboardRemoteObjectLocker* locker_ = nullptr;
    modules::clipboard::TransferObjectLockRequest request_;
    std::uint32_t timeoutMs_ = 0;
    bool locked_ = false;
};

bool buildOutputPath(const std::filesystem::path& root,
                     const modules::clipboard::TransferFileDescriptor& file,
                     std::filesystem::path* output,
                     std::string* errorMessage)
{
    const std::string relative =
        !file.relativePath.empty() ? file.relativePath : file.displayName;
    if (relative.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard remote file has no relative path";
        return false;
    }

    const std::filesystem::path requested(relative);
    if (requested.is_absolute() || requested.has_root_name() ||
        requested.has_root_directory()) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard remote file path is absolute: " + relative;
        }
        return false;
    }

    std::filesystem::path safeRelative;
    for (const std::filesystem::path& part : requested) {
        const std::string value = part.string();
        if (value.empty() || value == ".")
            continue;
        if (value == "..") {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file path escapes output root: " +
                    relative;
            }
            return false;
        }
        safeRelative /= part;
    }
    if (safeRelative.empty()) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard remote file path is empty";
        return false;
    }

    *output = root / safeRelative;
    return true;
}

class FileOutputSink final
    : public modules::clipboard::ITransferFileRangeSink
{
public:
    explicit FileOutputSink(std::filesystem::path path)
        : path_(std::move(path))
    {
    }

    modules::clipboard::TransferFileDrainSinkResult writeRange(
        const modules::clipboard::TransferFileRangeRequest&,
        const protocol::ByteBuffer& bytes,
        bool) override
    {
        modules::clipboard::TransferFileDrainSinkResult result;
        if (!opened_) {
            stream_.open(path_, std::ios::binary | std::ios::trunc);
            opened_ = true;
        }
        if (!stream_.is_open()) {
            result.status = protocol::ResponseStatus::Failed;
            result.message =
                "clipboard save file cannot be opened: " + path_.string();
            return result;
        }
        if (!bytes.empty()) {
            stream_.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
        }
        if (!stream_) {
            result.status = protocol::ResponseStatus::Failed;
            result.message =
                "clipboard save file write failed: " + path_.string();
            return result;
        }
        return result;
    }

private:
    std::filesystem::path path_;
    std::ofstream stream_;
    bool opened_ = false;
};

} // namespace

bool clipboardSaveFilesRequested(int argc, char** argv)
{
    return !optionValue(argc, argv, "--save-clipboard-files-dir").empty();
}

bool saveClipboardRemoteFiles(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader,
    std::string* errorMessage)
{
    const std::string outputDir =
        optionValue(argc, argv, "--save-clipboard-files-dir");
    if (outputDir.empty())
        return true;

    std::shared_ptr<modules::clipboard::IClipboardRemoteFileReader>
        fileReader =
            std::dynamic_pointer_cast<
                modules::clipboard::IClipboardRemoteFileReader>(
                remoteReader);
    if (fileReader == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard file save needs a remote file reader";
        return false;
    }
    std::shared_ptr<modules::clipboard::IClipboardRemoteObjectLocker>
        objectLocker =
            std::dynamic_pointer_cast<
                modules::clipboard::IClipboardRemoteObjectLocker>(
                remoteReader);
    if (objectLocker == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = "clipboard file save needs a remote object locker";
        return false;
    }

    const std::uint32_t timeoutMs = static_cast<std::uint32_t>(
        intOptionValue(argc, argv, "--clipboard-file-read-timeout-ms", 3000));
    modules::clipboard::TransferFileDrainOptions drainOptions;
    drainOptions.timeoutMs = timeoutMs;
    const std::uint64_t chunkBytes =
        uint64OptionValue(argc, argv, "--clipboard-file-read-chunk-bytes", 0);
    if (chunkBytes != 0)
        drainOptions.chunkBytes = chunkBytes;

    std::string listError;
    const RemoteFileListView view =
        remoteFileListView(session, remoteReader.get(), timeoutMs, &listError);
    if (view.bundle.offerId == 0) {
        if (errorMessage != nullptr)
            *errorMessage = listError;
        return false;
    }

    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::absolute(std::filesystem::path(outputDir), error);
    if (error) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard save output path is invalid: " + outputDir;
        }
        return false;
    }
    std::filesystem::create_directories(root, error);
    if (error) {
        if (errorMessage != nullptr) {
            *errorMessage =
                "clipboard save output directory cannot be created: " +
                root.string();
        }
        return false;
    }

    std::uint64_t filesSaved = 0;
    std::uint64_t directoriesSaved = 0;
    std::uint64_t bytesSaved = 0;
    std::uint64_t chunksSaved = 0;
    for (std::size_t index = 0; index < view.fileList.files.size(); ++index) {
        const modules::clipboard::TransferFileDescriptor& descriptor =
            view.fileList.files[index];
        std::filesystem::path outputPath;
        if (!buildOutputPath(root, descriptor, &outputPath, errorMessage))
            return false;

        if (descriptor.directory) {
            std::filesystem::create_directories(outputPath, error);
            if (error) {
                if (errorMessage != nullptr) {
                    *errorMessage =
                        "clipboard save directory cannot be created: " +
                        outputPath.string();
                }
                return false;
            }
            ++directoriesSaved;
            continue;
        }

        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard save parent directory cannot be created: " +
                    outputPath.parent_path().string();
            }
            return false;
        }

        modules::clipboard::TransferObjectLockRequest lockRequest;
        lockRequest.bundleId = view.bundle.bundleId;
        lockRequest.offerId = view.bundle.offerId;
        lockRequest.ownerEpoch = view.bundle.ownerEpoch;
        lockRequest.sourceId = view.sourceId;
        lockRequest.objectId = descriptor.objectId;
        lockRequest.fileIndex = static_cast<std::uint32_t>(index);

        RemoteObjectLockGuard lockGuard;
        if (!lockGuard.lock(*objectLocker,
                            lockRequest,
                            timeoutMs,
                            errorMessage)) {
            return false;
        }

        modules::clipboard::TransferFileRangeRequest request;
        request.bundleId = view.bundle.bundleId;
        request.offerId = view.bundle.offerId;
        request.ownerEpoch = view.bundle.ownerEpoch;
        request.sourceId = view.sourceId;
        request.objectId = descriptor.objectId;
        request.fileIndex = static_cast<std::uint32_t>(index);
        request.offset = 0;
        drainOptions.maxTotalBytes = descriptor.sizeBytes == 0
                                         ? 0
                                         : descriptor.sizeBytes;

        FileOutputSink sink(outputPath);
        const modules::clipboard::TransferFileDrainResult drained =
            modules::clipboard::drainRemoteFileRange(*fileReader,
                                                     request,
                                                     sink,
                                                     drainOptions);
        if (!drained.ok()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file save failed for \"" +
                    descriptor.relativePath + "\": " + drained.message;
            }
            return false;
        }
        if (!drained.endOfFile) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    "clipboard remote file save did not reach eof for \"" +
                    descriptor.relativePath + "\"";
            }
            return false;
        }
        if (!lockGuard.unlock(errorMessage))
            return false;

        ++filesSaved;
        bytesSaved += drained.bytesWritten;
        chunksSaved += drained.chunksWritten;
    }

    std::cout << "clipboard.files.saved"
              << " dir=" << root.string()
              << " files=" << filesSaved
              << " directories=" << directoriesSaved
              << " bytes=" << bytesSaved
              << " chunks=" << chunksSaved
              << std::endl;
    return true;
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
