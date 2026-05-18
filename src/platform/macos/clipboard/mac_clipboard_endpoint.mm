#include "fusiondesk/platform/macos/clipboard/mac_clipboard_endpoint.h"

#include "mac_clipboard_local_files.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#import <AppKit/AppKit.h>

namespace fusiondesk {
namespace platform {
namespace macos {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

constexpr const char* MacTextType = "public.utf8-plain-text";
constexpr const char* MacHtmlType = "public.html";
constexpr const char* MacRtfType = "public.rtf";
constexpr const char* MacPngType = "public.png";
constexpr const char* MacTiffType = "public.tiff";
constexpr const char* MacFileUrlType = "public.file-url";

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

protocol::ByteBuffer bytesFromString(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

std::string stringFromBytes(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

NSString* stringFromUtf8(const std::string& value)
{
    return [[NSString alloc]
        initWithBytes:value.data()
               length:value.size()
             encoding:NSUTF8StringEncoding];
}

std::string utf8FromString(NSString* value)
{
    if (value == nil)
        return {};
    const char* bytes = [value UTF8String];
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

NSData* dataFromBytes(const protocol::ByteBuffer& bytes)
{
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

protocol::ByteBuffer bytesFromData(NSData* data)
{
    if (data == nil || [data length] == 0)
        return {};
    const auto* first =
        static_cast<const std::uint8_t*>([data bytes]);
    return protocol::ByteBuffer(first, first + [data length]);
}

NSPasteboard* generalPasteboard()
{
    return [NSPasteboard generalPasteboard];
}

std::int64_t pasteboardChangeCount()
{
    NSPasteboard* pasteboard = generalPasteboard();
    if (pasteboard == nil)
        return -1;
    return static_cast<std::int64_t>([pasteboard changeCount]);
}

bool descriptorIsText(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == TextPlainUtf8Format ||
           descriptor.nativeFormatName == MacTextType;
}

bool descriptorIsHtml(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == TextHtmlFormat ||
           descriptor.nativeFormatName == MacHtmlType;
}

bool descriptorIsRtf(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == TextRtfFormat ||
           descriptor.nativeFormatName == MacRtfType;
}

bool descriptorIsImagePng(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == ImagePngFormat ||
           descriptor.nativeFormatName == MacPngType;
}

bool descriptorIsFileList(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == FdclFileListFormat ||
           descriptor.nativeFormatName == MacFileUrlType ||
           descriptor.nativeFormatName == "text/uri-list";
}

bool relativePathIsTopLevel(const std::string& relativePath)
{
    std::filesystem::path path(relativePath);
    std::size_t parts = 0;
    for (const std::filesystem::path& part : path) {
        const std::string value = part.string();
        if (value.empty() || value == ".")
            continue;
        ++parts;
        if (parts > 1)
            return false;
    }
    return parts == 1;
}

std::filesystem::path pathFromFileUrl(NSURL* url)
{
    if (url == nil || ![url isFileURL])
        return {};

    const char* path = [url fileSystemRepresentation];
    return path == nullptr ? std::filesystem::path{} : std::filesystem::path(path);
}

std::vector<std::filesystem::path> filePathsFromPasteboard(
    NSPasteboard* pasteboard)
{
    std::vector<std::filesystem::path> result;
    if (pasteboard == nil)
        return result;

    NSDictionary* options = @{
        NSPasteboardURLReadingFileURLsOnlyKey : @YES
    };
    NSArray* urls =
        [pasteboard readObjectsForClasses:@[[NSURL class]] options:options];
    for (NSURL* url in urls) {
        std::filesystem::path path = pathFromFileUrl(url);
        if (!path.empty())
            result.push_back(std::move(path));
    }
    return result;
}

protocol::ByteBuffer pngFromImageData(NSData* data,
                                      std::uint32_t* width,
                                      std::uint32_t* height)
{
    if (width != nullptr)
        *width = 0;
    if (height != nullptr)
        *height = 0;
    if (data == nil || [data length] == 0)
        return {};

    NSImage* image = [[[NSImage alloc] initWithData:data] autorelease];
    if (image == nil)
        return {};
    NSData* tiff = [image TIFFRepresentation];
    if (tiff == nil)
        return {};

    NSBitmapImageRep* bitmap =
        [NSBitmapImageRep imageRepWithData:tiff];
    if (bitmap == nil)
        return {};

    if (width != nullptr)
        *width = static_cast<std::uint32_t>(std::max<NSInteger>(
            0,
            [bitmap pixelsWide]));
    if (height != nullptr)
        *height = static_cast<std::uint32_t>(std::max<NSInteger>(
            0,
            [bitmap pixelsHigh]));

    NSData* png =
        [bitmap representationUsingType:NSBitmapImageFileTypePNG
                             properties:@{}];
    return bytesFromData(png);
}

bool imageDimensionsFromPng(const protocol::ByteBuffer& png,
                            std::uint32_t* width,
                            std::uint32_t* height)
{
    if (width != nullptr)
        *width = 0;
    if (height != nullptr)
        *height = 0;
    NSData* data = dataFromBytes(png);
    NSBitmapImageRep* bitmap = [NSBitmapImageRep imageRepWithData:data];
    if (bitmap == nil)
        return false;
    if (width != nullptr)
        *width = static_cast<std::uint32_t>(std::max<NSInteger>(
            0,
            [bitmap pixelsWide]));
    if (height != nullptr)
        *height = static_cast<std::uint32_t>(std::max<NSInteger>(
            0,
            [bitmap pixelsHigh]));
    return true;
}

class MacRemoteObjectLockGuard final
{
public:
    ~MacRemoteObjectLockGuard()
    {
        unlockSilently();
    }

    MacRemoteObjectLockGuard() = default;
    MacRemoteObjectLockGuard(const MacRemoteObjectLockGuard&) = delete;
    MacRemoteObjectLockGuard& operator=(const MacRemoteObjectLockGuard&) = delete;

    protocol::ResponseStatus lock(IClipboardRemoteObjectLocker* locker,
                                  TransferObjectLockRequest request,
                                  std::uint32_t timeoutMs,
                                  std::string* message)
    {
        locker_ = locker;
        request_ = request;
        timeoutMs_ = timeoutMs;
        if (locker_ == nullptr)
            return protocol::ResponseStatus::Ok;

        const TransferObjectLockResult result =
            locker_->lockRemoteObject(request_, timeoutMs_);
        if (!result.ok()) {
            if (message != nullptr)
                *message = result.message;
            return result.status;
        }
        if (result.lockId == 0) {
            if (message != nullptr)
                *message = "macOS remote file object lock returned empty id";
            return protocol::ResponseStatus::Failed;
        }

        request_.lockId = result.lockId;
        request_.leaseUsec = result.leaseUsec;
        locked_ = true;
        return protocol::ResponseStatus::Ok;
    }

    protocol::ResponseStatus unlock(std::string* message)
    {
        if (!locked_ || locker_ == nullptr)
            return protocol::ResponseStatus::Ok;

        const TransferObjectLockResult result =
            locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
        if (result.ok())
            return protocol::ResponseStatus::Ok;

        if (message != nullptr)
            *message = result.message;
        return result.status;
    }

private:
    void unlockSilently()
    {
        if (!locked_ || locker_ == nullptr)
            return;
        locker_->unlockRemoteObject(request_, timeoutMs_);
        locked_ = false;
    }

    IClipboardRemoteObjectLocker* locker_ = nullptr;
    TransferObjectLockRequest request_;
    std::uint32_t timeoutMs_ = 0;
    bool locked_ = false;
};

class MacPromisedFileSink final : public ITransferFileRangeSink
{
public:
    explicit MacPromisedFileSink(std::filesystem::path path)
        : path_(std::move(path))
    {
    }

    TransferFileDrainSinkResult writeRange(
        const TransferFileRangeRequest&,
        const protocol::ByteBuffer& bytes,
        bool) override
    {
        TransferFileDrainSinkResult result;
        if (!opened_) {
            stream_.open(path_, std::ios::binary | std::ios::trunc);
            opened_ = true;
        }
        if (!stream_.is_open()) {
            result.status = protocol::ResponseStatus::Failed;
            result.message = "macOS promised file cannot be opened";
            return result;
        }
        if (!bytes.empty()) {
            stream_.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
        }
        if (!stream_) {
            result.status = protocol::ResponseStatus::Failed;
            result.message = "macOS promised file write failed";
            return result;
        }
        return result;
    }

private:
    std::filesystem::path path_;
    std::ofstream stream_;
    bool opened_ = false;
};

bool relativePathIsInTree(const std::string& rootRelativePath,
                          const std::string& candidateRelativePath)
{
    if (candidateRelativePath == rootRelativePath)
        return true;
    if (rootRelativePath.empty())
        return false;
    return candidateRelativePath.size() > rootRelativePath.size() &&
           candidateRelativePath.compare(0,
                                         rootRelativePath.size(),
                                         rootRelativePath) == 0 &&
           candidateRelativePath[rootRelativePath.size()] == '/';
}

std::filesystem::path relativePathBelowRoot(
    const std::string& rootRelativePath,
    const std::string& candidateRelativePath)
{
    if (candidateRelativePath == rootRelativePath)
        return {};
    if (!relativePathIsInTree(rootRelativePath, candidateRelativePath))
        return {};
    return std::filesystem::path(
        candidateRelativePath.substr(rootRelativePath.size() + 1));
}

std::string promisedFileName(const TransferFileDescriptor& file)
{
    std::string name = sanitizeTransferFileDisplayName(file.displayName);
    if (!name.empty() && name != "unnamed")
        return name;

    const std::filesystem::path relative(file.relativePath);
    name = sanitizeTransferFileDisplayName(relative.filename().generic_u8string());
    return name.empty() ? std::string("unnamed") : name;
}

std::string filePromiseType(const TransferFileDescriptor& file)
{
    if (file.directory)
        return "public.folder";

    std::string extension =
        std::filesystem::path(file.displayName).extension().generic_u8string();
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });

    if (extension == ".txt")
        return "public.plain-text";
    if (extension == ".html" || extension == ".htm")
        return "public.html";
    if (extension == ".rtf")
        return "public.rtf";
    if (extension == ".png")
        return "public.png";
    if (extension == ".jpg" || extension == ".jpeg")
        return "public.jpeg";
    if (extension == ".pdf")
        return "com.adobe.pdf";
    return "public.data";
}

protocol::ResponseStatus writeMacFilePromiseTree(
    const TransferSourceBundle& bundle,
    const TransferReadRequest& listRequest,
    const TransferFileList& fileList,
    std::uint32_t rootFileIndex,
    const std::filesystem::path& promisePath,
    IClipboardRemoteFileReader* remoteFileReader,
    IClipboardRemoteObjectLocker* remoteObjectLocker,
    std::uint64_t maxFileRangeBytes,
    std::uint64_t maxSingleFileBytes,
    std::string* message)
{
    if (rootFileIndex >= fileList.files.size()) {
        if (message != nullptr)
            *message = "macOS file promise root index is invalid";
        return protocol::ResponseStatus::InvalidArgument;
    }
    if (promisePath.empty()) {
        if (message != nullptr)
            *message = "macOS file promise output path is empty";
        return protocol::ResponseStatus::InvalidArgument;
    }

    const TransferFileDescriptor& root = fileList.files[rootFileIndex];
    const std::string rootRelative = sanitizeTransferFileRelativePath(
        root.relativePath.empty() ? root.displayName : root.relativePath);
    const std::filesystem::path outputRoot = promisePath;

    std::error_code error;
    if (root.directory) {
        std::filesystem::create_directories(outputRoot, error);
        if (error) {
            if (message != nullptr)
                *message = "macOS file promise root directory cannot be created";
            return protocol::ResponseStatus::Failed;
        }
    } else {
        std::filesystem::create_directories(outputRoot.parent_path(), error);
        if (error) {
            if (message != nullptr)
                *message = "macOS file promise parent directory cannot be created";
            return protocol::ResponseStatus::Failed;
        }
    }

    for (std::size_t index = 0; index < fileList.files.size(); ++index) {
        const TransferFileDescriptor& file = fileList.files[index];
        const std::string relative = sanitizeTransferFileRelativePath(
            file.relativePath.empty() ? file.displayName : file.relativePath);
        if (!relativePathIsInTree(rootRelative, relative))
            continue;

        const std::filesystem::path belowRoot =
            relativePathBelowRoot(rootRelative, relative);
        const std::filesystem::path outputPath =
            belowRoot.empty() ? outputRoot : outputRoot / belowRoot;

        if (file.directory) {
            std::filesystem::create_directories(outputPath, error);
            if (error) {
                if (message != nullptr)
                    *message = "macOS file promise directory cannot be created";
                return protocol::ResponseStatus::Failed;
            }
            continue;
        }

        if (remoteFileReader == nullptr) {
            if (message != nullptr)
                *message = "macOS file promise needs a remote file reader";
            return protocol::ResponseStatus::Failed;
        }
        if (maxSingleFileBytes != 0 && file.sizeBytes > maxSingleFileBytes) {
            if (message != nullptr)
                *message = "macOS promised file exceeds max single file bytes";
            return protocol::ResponseStatus::TooLarge;
        }

        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            if (message != nullptr)
                *message = "macOS promised file parent directory cannot be created";
            return protocol::ResponseStatus::Failed;
        }

        TransferObjectLockRequest lockRequest;
        lockRequest.bundleId = bundle.bundleId;
        lockRequest.offerId = bundle.offerId;
        lockRequest.ownerEpoch = bundle.ownerEpoch;
        lockRequest.sourceId = listRequest.sourceId;
        lockRequest.objectId = file.objectId;
        lockRequest.fileIndex = static_cast<std::uint32_t>(index);

        std::string lockMessage;
        MacRemoteObjectLockGuard lockGuard;
        const protocol::ResponseStatus locked =
            lockGuard.lock(remoteObjectLocker,
                           lockRequest,
                           30000,
                           &lockMessage);
        if (locked != protocol::ResponseStatus::Ok) {
            if (message != nullptr)
                *message = "macOS promised file object lock failed: " +
                           lockMessage;
            return locked;
        }

        TransferFileRangeRequest rangeRequest;
        rangeRequest.bundleId = bundle.bundleId;
        rangeRequest.offerId = bundle.offerId;
        rangeRequest.ownerEpoch = bundle.ownerEpoch;
        rangeRequest.sourceId = listRequest.sourceId;
        rangeRequest.objectId = file.objectId;
        rangeRequest.fileIndex = static_cast<std::uint32_t>(index);

        TransferFileDrainOptions drainOptions;
        drainOptions.chunkBytes = maxFileRangeBytes == 0
                                      ? DefaultTransferFileRangeChunkBytes
                                      : maxFileRangeBytes;
        drainOptions.timeoutMs = 30000;
        drainOptions.maxTotalBytes =
            file.sizeBytes == 0 ? maxSingleFileBytes : file.sizeBytes;

        MacPromisedFileSink sink(outputPath);
        const TransferFileDrainResult drained =
            drainRemoteFileRange(*remoteFileReader,
                                 rangeRequest,
                                 sink,
                                 drainOptions);
        if (!drained.ok() || !drained.endOfFile) {
            if (message != nullptr) {
                *message = "macOS promised file stream failed: " +
                           (drained.message.empty()
                                ? std::string("missing eof")
                                : drained.message);
            }
            return drained.ok() ? protocol::ResponseStatus::Failed
                                : drained.status;
        }

        std::string unlockMessage;
        const protocol::ResponseStatus unlocked =
            lockGuard.unlock(&unlockMessage);
        if (unlocked != protocol::ResponseStatus::Ok) {
            if (message != nullptr)
                *message = "macOS promised file object unlock failed: " +
                           unlockMessage;
            return unlocked;
        }
    }

    if (message != nullptr)
        *message = "macOS promised file tree written";
    return protocol::ResponseStatus::Ok;
}

} // namespace

static protocol::ResponseStatus writeMacFilePromiseTreeForProvider(
    const TransferSourceBundle& bundle,
    const TransferReadRequest& listRequest,
    const TransferFileList& fileList,
    std::uint32_t rootFileIndex,
    const std::filesystem::path& promisePath,
    IClipboardRemoteFileReader* remoteFileReader,
    IClipboardRemoteObjectLocker* remoteObjectLocker,
    std::uint64_t maxFileRangeBytes,
    std::uint64_t maxSingleFileBytes,
    std::string* message)
{
    return writeMacFilePromiseTree(bundle,
                                   listRequest,
                                   fileList,
                                   rootFileIndex,
                                   promisePath,
                                   remoteFileReader,
                                   remoteObjectLocker,
                                   maxFileRangeBytes,
                                   maxSingleFileBytes,
                                   message);
}

} // namespace clipboard
} // namespace macos
} // namespace platform
} // namespace fusiondesk

namespace {

struct FusionDeskMacFilePromiseState
{
    fusiondesk::modules::clipboard::TransferSourceBundle bundle;
    fusiondesk::modules::clipboard::TransferReadRequest listRequest;
    fusiondesk::modules::clipboard::TransferFileList fileList;
    std::uint32_t rootFileIndex = 0;
    std::string fileName;
    std::shared_ptr<
        fusiondesk::modules::clipboard::IClipboardRemoteFileReader>
        remoteFileReader;
    std::shared_ptr<
        fusiondesk::modules::clipboard::IClipboardRemoteObjectLocker>
        remoteObjectLocker;
    std::uint64_t maxFileRangeBytes = 0;
    std::uint64_t maxSingleFileBytes = 0;
};

NSString* fusionDesk2StringFromUtf8(const std::string& value)
{
    return [[[NSString alloc]
        initWithBytes:value.data()
               length:value.size()
             encoding:NSUTF8StringEncoding] autorelease];
}

NSError* fusionDesk2FilePromiseError(
    fusiondesk::protocol::ResponseStatus status,
    const std::string& message)
{
    NSString* description = fusionDesk2StringFromUtf8(
        message.empty() ? std::string("macOS file promise failed") : message);
    if (description == nil)
        description = @"macOS file promise failed";
    NSDictionary* info = @{
        NSLocalizedDescriptionKey : description
    };
    return [NSError
        errorWithDomain:@"com.fusiondesk.clipboard.filepromise"
                   code:static_cast<NSInteger>(status)
               userInfo:info];
}

std::filesystem::path fusionDesk2PathFromFileUrl(NSURL* url)
{
    if (url == nil || ![url isFileURL])
        return {};

    const char* path = [url fileSystemRepresentation];
    return path == nullptr ? std::filesystem::path{} : std::filesystem::path(path);
}

} // namespace

@interface FusionDeskMacPasteboardProvider
    : NSObject <NSPasteboardItemDataProvider>
{
@private
    fusiondesk::platform::macos::clipboard::MacClipboardEndpoint* endpoint_;
}
- (instancetype)initWithEndpoint:
    (fusiondesk::platform::macos::clipboard::MacClipboardEndpoint*)endpoint;
@end

@implementation FusionDeskMacPasteboardProvider

- (instancetype)initWithEndpoint:
    (fusiondesk::platform::macos::clipboard::MacClipboardEndpoint*)endpoint
{
    self = [super init];
    if (self != nil)
        endpoint_ = endpoint;
    return self;
}

- (void)pasteboard:(NSPasteboard*)pasteboard
              item:(NSPasteboardItem*)item
provideDataForType:(NSPasteboardType)type
{
    (void)pasteboard;
    if (endpoint_ == nullptr || item == nil || type == nil)
        return;

    const char* nativeType = [type UTF8String];
    if (nativeType == nullptr)
        return;

    fusiondesk::modules::clipboard::TransferReadResult result =
        endpoint_->renderPasteboardDataForNativeType(std::string(nativeType));
    if (!result.ok())
        return;

    NSData* data = [NSData dataWithBytes:result.bytes.data()
                                  length:result.bytes.size()];
    [item setData:data forType:type];
}

@end

@interface FusionDeskMacFilePromiseDelegate
    : NSObject <NSFilePromiseProviderDelegate>
{
@private
    FusionDeskMacFilePromiseState* state_;
    NSOperationQueue* operationQueue_;
}
- (instancetype)initWithState:(FusionDeskMacFilePromiseState*)state;
@end

@implementation FusionDeskMacFilePromiseDelegate

- (instancetype)initWithState:(FusionDeskMacFilePromiseState*)state
{
    self = [super init];
    if (self != nil) {
        state_ = state;
        operationQueue_ = [[NSOperationQueue alloc] init];
        [operationQueue_ setMaxConcurrentOperationCount:1];
        [operationQueue_ setName:@"FusionDeskMacFilePromise"];
    }
    return self;
}

- (void)dealloc
{
    [operationQueue_ cancelAllOperations];
    [operationQueue_ release];
    delete state_;
    [super dealloc];
}

- (NSString*)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider
                 fileNameForType:(NSString*)fileType
{
    (void)filePromiseProvider;
    (void)fileType;
    if (state_ == nullptr)
        return @"unnamed";
    NSString* name = fusionDesk2StringFromUtf8(state_->fileName);
    return name == nil ? @"unnamed" : name;
}

- (NSOperationQueue*)operationQueueForFilePromiseProvider:
    (NSFilePromiseProvider*)filePromiseProvider
{
    (void)filePromiseProvider;
    return operationQueue_;
}

- (void)filePromiseProvider:(NSFilePromiseProvider*)filePromiseProvider
          writePromiseToURL:(NSURL*)url
          completionHandler:(void (^)(NSError* error))completionHandler
{
    (void)filePromiseProvider;
    if (state_ == nullptr) {
        if (completionHandler != nil) {
            completionHandler(fusionDesk2FilePromiseError(
                fusiondesk::protocol::ResponseStatus::Failed,
                "macOS file promise state is unavailable"));
        }
        return;
    }

    __block NSError* promiseError = nil;
    NSError* coordinatorError = nil;
    NSFileCoordinator* coordinator =
        [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    if (coordinator == nil) {
        if (completionHandler != nil) {
            completionHandler(fusionDesk2FilePromiseError(
                fusiondesk::protocol::ResponseStatus::Failed,
                "macOS file promise coordinator cannot be created"));
        }
        return;
    }
    [coordinator coordinateWritingItemAtURL:url
                                    options:0
                                      error:&coordinatorError
                                 byAccessor:^(NSURL* coordinatedUrl) {
        const std::filesystem::path promisePath =
            fusionDesk2PathFromFileUrl(coordinatedUrl);
        std::string message;
        const fusiondesk::protocol::ResponseStatus status =
            fusiondesk::platform::macos::clipboard::
            writeMacFilePromiseTreeForProvider(
                state_->bundle,
                state_->listRequest,
                state_->fileList,
                state_->rootFileIndex,
                promisePath,
                state_->remoteFileReader.get(),
                state_->remoteObjectLocker.get(),
                state_->maxFileRangeBytes,
                state_->maxSingleFileBytes,
                &message);
        if (status != fusiondesk::protocol::ResponseStatus::Ok)
            promiseError = [fusionDesk2FilePromiseError(status, message) retain];
    }];
    [coordinator release];

    if (completionHandler != nil)
        completionHandler(coordinatorError != nil ? coordinatorError : promiseError);
    [promiseError release];
}

@end

namespace fusiondesk {
namespace platform {
namespace macos {
namespace clipboard {

MacClipboardEndpoint::MacClipboardEndpoint(
    MacClipboardEndpointOptions options,
    std::shared_ptr<IClipboardRemoteReader> remoteReader,
    std::shared_ptr<IClipboardRemoteFileReader> remoteFileReader,
    std::shared_ptr<IClipboardRemoteObjectLocker> remoteObjectLocker)
    : options_(std::move(options)),
      remoteReader_(std::move(remoteReader)),
      remoteFileReader_(std::move(remoteFileReader)),
      remoteObjectLocker_(std::move(remoteObjectLocker)),
      nextBundleId_(std::max<TransferBundleId>(1, options_.firstBundleId)),
      nextOfferId_(std::max<TransferOfferId>(1, options_.firstOfferId)),
      nextSourceId_(std::max<TransferSourceId>(1, options_.firstSourceId)),
      nextFormatId_(std::max<TransferFormatId>(1, options_.firstFormatId))
{
    if (remoteFileReader_ == nullptr)
        remoteFileReader_ =
            std::dynamic_pointer_cast<IClipboardRemoteFileReader>(remoteReader_);
    if (remoteObjectLocker_ == nullptr)
        remoteObjectLocker_ =
            std::dynamic_pointer_cast<IClipboardRemoteObjectLocker>(remoteReader_);
    lastObservedChangeCount_ = pasteboardChangeCount();
    diagnostics_.lastNativeChangeCount = lastObservedChangeCount_;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
}

MacClipboardEndpoint::~MacClipboardEndpoint()
{
    cleanupRemoteFilePublicationState();
    if (pasteboardProvider_ != nullptr) {
        [(id)pasteboardProvider_ release];
        pasteboardProvider_ = nullptr;
    }
}

ClipboardSnapshot MacClipboardEndpoint::snapshot()
{
    @autoreleasepool {
        ++diagnostics_.snapshots;
        NSPasteboard* pasteboard = generalPasteboard();
        if (pasteboard == nil) {
            diagnostics_.lastMessage = "macOS pasteboard is unavailable";
            return {};
        }

        const std::int64_t changeCount = pasteboardChangeCount();
        lastObservedChangeCount_ = changeCount;
        diagnostics_.lastNativeChangeCount = changeCount;
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = false;

        const std::vector<std::filesystem::path> inputPaths =
            filePathsFromPasteboard(pasteboard);
        if (!inputPaths.empty()) {
            TransferFileList fileList;
            std::vector<std::filesystem::path> paths;
            MacClipboardLocalFileOptions options;
            options.expandDirectories = options_.expandDroppedDirectories;
            options.maxFileCount = options_.maxFileCount;
            options.maxDirectoryDepth = options_.maxDirectoryDepth;
            options.maxFileRangeBytes = options_.maxFileRangeBytes;
            options.maxSingleFileBytes = options_.maxSingleFileBytes;
            if (readMacLocalFileList(fileList, &paths, inputPaths, options)) {
                return snapshotFromFileList(fileList,
                                            std::move(paths),
                                            static_cast<std::uint64_t>(
                                                std::max<std::int64_t>(
                                                    1,
                                                    changeCount)));
            }
        }

        NSData* pngData =
            [pasteboard dataForType:[NSString stringWithUTF8String:MacPngType]];
        if (pngData != nil && [pngData length] > 0) {
            protocol::ByteBuffer png = bytesFromData(pngData);
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            imageDimensionsFromPng(png, &width, &height);
            return snapshotFromImagePng(
                png,
                width,
                height,
                static_cast<std::uint64_t>(std::max<std::int64_t>(
                    1,
                    changeCount)));
        }

        NSData* tiffData =
            [pasteboard dataForType:[NSString stringWithUTF8String:MacTiffType]];
        if (tiffData != nil && [tiffData length] > 0) {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            protocol::ByteBuffer png = pngFromImageData(tiffData, &width, &height);
            if (!png.empty()) {
                return snapshotFromImagePng(
                    png,
                    width,
                    height,
                    static_cast<std::uint64_t>(std::max<std::int64_t>(
                        1,
                        changeCount)));
            }
        }

        NSData* htmlData =
            [pasteboard dataForType:[NSString stringWithUTF8String:MacHtmlType]];
        NSData* rtfData =
            [pasteboard dataForType:[NSString stringWithUTF8String:MacRtfType]];
        NSString* textValue =
            [pasteboard stringForType:[NSString stringWithUTF8String:MacTextType]];
        if (textValue == nil)
            textValue = [pasteboard stringForType:NSPasteboardTypeString];

        if ((htmlData != nil && [htmlData length] > 0) ||
            (rtfData != nil && [rtfData length] > 0)) {
            return snapshotFromFormattedText(
                utf8FromString(textValue),
                bytesFromData(htmlData),
                bytesFromData(rtfData),
                static_cast<std::uint64_t>(std::max<std::int64_t>(
                    1,
                    changeCount)));
        }

        if (textValue != nil) {
            return snapshotFromText(
                utf8FromString(textValue),
                static_cast<std::uint64_t>(std::max<std::int64_t>(
                    1,
                    changeCount)));
        }

        diagnostics_.lastMessage = "macOS pasteboard has no supported format";
        return {};
    }
}

protocol::ResponseStatus MacClipboardEndpoint::publishBundle(
    const ClipboardPublishRequest& request)
{
    @autoreleasepool {
        const protocol::ResponseStatus fileStatus =
            publishFileListBundle(request.bundle);
        if (fileStatus != protocol::ResponseStatus::NotFound)
            return fileStatus;
        return publishDelayedBundle(request.bundle);
    }
}

protocol::ResponseStatus MacClipboardEndpoint::clearPublishedBundle(
    TransferOfferId offerId)
{
    @autoreleasepool {
        ++diagnostics_.clears;
        if (publishedBundle_.offerId != offerId)
            return protocol::ResponseStatus::NotFound;

        NSPasteboard* pasteboard = generalPasteboard();
        if (pasteboard == nil) {
            diagnostics_.lastMessage = "macOS pasteboard is unavailable";
            return protocol::ResponseStatus::Failed;
        }

        [pasteboard clearContents];
        publishedBundle_ = {};
        if (pasteboardProvider_ != nullptr) {
            [(id)pasteboardProvider_ release];
            pasteboardProvider_ = nullptr;
        }
        cleanupRemoteFilePublicationState();
        diagnostics_.publishedOfferId = 0;
        lastPublishedChangeCount_ = pasteboardChangeCount();
        lastObservedChangeCount_ = lastPublishedChangeCount_;
        diagnostics_.lastNativeChangeCount = lastObservedChangeCount_;
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = false;
        diagnostics_.lastMessage = "macOS pasteboard cleared";
        return protocol::ResponseStatus::Ok;
    }
}

bool MacClipboardEndpoint::hasPendingClipboardChange() const
{
    if (!options_.enableChangeMonitor)
        return true;
    const std::int64_t current = pasteboardChangeCount();
    if (current < 0)
        return false;
    if (options_.suppressOwnClipboardUpdates &&
        current == lastPublishedChangeCount_) {
        return false;
    }
    return current != lastObservedChangeCount_;
}

void MacClipboardEndpoint::markClipboardChangeConsumed()
{
    lastObservedChangeCount_ = pasteboardChangeCount();
    diagnostics_.lastNativeChangeCount = lastObservedChangeCount_;
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = false;
}

MacClipboardEndpointDiagnostics MacClipboardEndpoint::diagnostics() const
{
    MacClipboardEndpointDiagnostics copy = diagnostics_;
    copy.nativeChangePending = hasPendingClipboardChange();
    copy.lastNativeChangeCount = pasteboardChangeCount();
    return copy;
}

TransferReadResult MacClipboardEndpoint::renderPasteboardDataForNativeType(
    const std::string& nativeType)
{
    ++diagnostics_.delayedRenders;
    if (publishedBundle_.offerId == 0) {
        TransferReadResult result;
        result.status = protocol::ResponseStatus::NotFound;
        result.message = "macOS delayed pasteboard bundle is empty";
        return result;
    }

    if (nativeType == MacHtmlType)
        return readBestHtml(publishedBundle_);
    if (nativeType == MacRtfType)
        return readBestRtf(publishedBundle_);
    if (nativeType == MacPngType)
        return readBestImagePng(publishedBundle_);
    if (nativeType == MacTextType ||
        nativeType == std::string([NSPasteboardTypeString UTF8String])) {
        return readBestText(publishedBundle_);
    }

    TransferReadResult result;
    result.status = protocol::ResponseStatus::NotFound;
    result.message = "macOS pasteboard native type is not supported";
    return result;
}

TransferReadResult MacClipboardEndpoint::readBestFormat(
    const TransferSourceBundle& bundle,
    const std::string& canonicalFormat,
    bool (*descriptorMatches)(const TransferFormatDescriptor&),
    const std::string& notFoundMessage)
{
    TransferReadResult result;
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
        const auto format = std::find_if(formats.begin(),
                                         formats.end(),
                                         descriptorMatches);
        if (format == formats.end())
            continue;

        TransferReadRequest request;
        request.bundleId = bundle.bundleId;
        request.offerId = bundle.offerId;
        request.ownerEpoch = bundle.ownerEpoch;
        request.sourceId = source->id();
        request.itemIndex = format->itemIndex;
        request.formatId = format->formatId;
        request.localFormatToken = format->localFormatToken;
        request.canonicalFormat = canonicalFormat;
        request.acceptedMaxBytes = options_.maxInlineBytes;
        request.requestedEncoding = TransferEncodingMode::CanonicalBytes;

        result = source->read(request);
        if (result.status == protocol::ResponseStatus::Unsupported &&
            remoteReader_ != nullptr) {
            result = remoteReader_->readRemoteFormat(
                request,
                options_.delayedReadTimeoutMs == 0
                    ? 1000
                    : options_.delayedReadTimeoutMs);
        }
        if (result.ok() && result.canonicalFormat.empty())
            result.canonicalFormat = canonicalFormat;
        return result;
    }

    result.status = protocol::ResponseStatus::NotFound;
    result.message = notFoundMessage;
    return result;
}

TransferReadResult MacClipboardEndpoint::readBestText(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextPlainUtf8Format,
                          descriptorIsText,
                          "macOS pasteboard text format is not found");
}

TransferReadResult MacClipboardEndpoint::readBestHtml(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextHtmlFormat,
                          descriptorIsHtml,
                          "macOS pasteboard html format is not found");
}

TransferReadResult MacClipboardEndpoint::readBestRtf(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          TextRtfFormat,
                          descriptorIsRtf,
                          "macOS pasteboard rtf format is not found");
}

TransferReadResult MacClipboardEndpoint::readBestImagePng(
    const TransferSourceBundle& bundle)
{
    return readBestFormat(bundle,
                          ImagePngFormat,
                          descriptorIsImagePng,
                          "macOS pasteboard image/png format is not found");
}

protocol::ResponseStatus MacClipboardEndpoint::publishDelayedBundle(
    const TransferSourceBundle& bundle)
{
    ++diagnostics_.publishes;
    if (bundle.offerId == 0 || !hasSupportedDelayedFormat(bundle)) {
        diagnostics_.lastMessage =
            "macOS delayed pasteboard bundle has no supported format";
        return protocol::ResponseStatus::InvalidArgument;
    }

    NSPasteboard* pasteboard = generalPasteboard();
    if (pasteboard == nil) {
        diagnostics_.lastMessage = "macOS pasteboard is unavailable";
        return protocol::ResponseStatus::Failed;
    }

    NSMutableArray* types = [NSMutableArray array];
    if (hasHtmlFormat(bundle))
        [types addObject:[NSString stringWithUTF8String:MacHtmlType]];
    if (hasRtfFormat(bundle))
        [types addObject:[NSString stringWithUTF8String:MacRtfType]];
    if (hasImagePngFormat(bundle))
        [types addObject:[NSString stringWithUTF8String:MacPngType]];
    if (hasTextFormat(bundle)) {
        NSString* textType = [NSString stringWithUTF8String:MacTextType];
        [types addObject:textType];
        if (![NSPasteboardTypeString isEqualToString:textType])
            [types addObject:NSPasteboardTypeString];
    }
    if ([types count] == 0)
        return protocol::ResponseStatus::NotFound;

    FusionDeskMacPasteboardProvider* provider =
        [[FusionDeskMacPasteboardProvider alloc] initWithEndpoint:this];
    NSPasteboardItem* item = [[[NSPasteboardItem alloc] init] autorelease];
    publishedBundle_ = bundle;
    const BOOL providerSet = [item setDataProvider:provider forTypes:types];
    if (!providerSet) {
        publishedBundle_ = {};
        [provider release];
        diagnostics_.lastMessage =
            "macOS pasteboard provider registration failed";
        return protocol::ResponseStatus::Failed;
    }

    [pasteboard clearContents];
    const BOOL written = [pasteboard writeObjects:@[item]];
    if (!written) {
        publishedBundle_ = {};
        [provider release];
        diagnostics_.lastMessage = "macOS pasteboard write failed";
        return protocol::ResponseStatus::Failed;
    }

    if (pasteboardProvider_ != nullptr)
        [(id)pasteboardProvider_ release];
    pasteboardProvider_ = provider;
    cleanupRemoteFilePublicationState();
    diagnostics_.publishedOfferId = bundle.offerId;
    diagnostics_.lastNativeSequence = bundle.sequence;
    lastPublishedChangeCount_ = pasteboardChangeCount();
    lastObservedChangeCount_ = lastPublishedChangeCount_;
    diagnostics_.lastNativeChangeCount = lastObservedChangeCount_;
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = false;
    ++diagnostics_.delayedPublishes;
    diagnostics_.lastMessage = "macOS delayed pasteboard bundle published";
    return protocol::ResponseStatus::Ok;
}

protocol::ResponseStatus MacClipboardEndpoint::publishFileListBundle(
    const TransferSourceBundle& bundle)
{
    TransferReadRequest listRequest;
    TransferReadResult listRead;
    bool foundFileList = false;
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats = source->formats();
        const auto fileList = std::find_if(formats.begin(),
                                           formats.end(),
                                           descriptorIsFileList);
        if (fileList == formats.end())
            continue;

        foundFileList = true;
        listRequest.bundleId = bundle.bundleId;
        listRequest.offerId = bundle.offerId;
        listRequest.ownerEpoch = bundle.ownerEpoch;
        listRequest.sourceId = source->id();
        listRequest.itemIndex = fileList->itemIndex;
        listRequest.formatId = fileList->formatId;
        listRequest.localFormatToken = fileList->localFormatToken;
        listRequest.canonicalFormat = FdclFileListFormat;
        listRequest.acceptedMaxBytes = options_.maxInlineBytes;
        listRequest.streamAccepted = false;
        listRequest.requestedEncoding = TransferEncodingMode::CanonicalBytes;

        listRead = source->read(listRequest);
        if (listRead.status == protocol::ResponseStatus::Unsupported &&
            remoteReader_ != nullptr) {
            listRead = remoteReader_->readRemoteFormat(listRequest, 1000);
        }
        break;
    }

    if (!foundFileList)
        return protocol::ResponseStatus::NotFound;
    if (!listRead.ok()) {
        ++diagnostics_.readFailures;
        ++diagnostics_.remoteFilePromiseFailures;
        diagnostics_.lastMessage = listRead.message.empty()
                                       ? "macOS remote file-list read failed"
                                       : listRead.message;
        return listRead.status;
    }

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(listRead.bytes, options_.maxFileCount);
    if (!decoded.ok) {
        ++diagnostics_.remoteFilePromiseFailures;
        diagnostics_.lastMessage =
            "macOS remote file-list decode failed: " + decoded.message;
        return decoded.status;
    }
    if (decoded.fileList.files.empty()) {
        ++diagnostics_.remoteFilePromiseFailures;
        diagnostics_.lastMessage = "macOS remote file-list is empty";
        return protocol::ResponseStatus::NotFound;
    }

    std::vector<std::size_t> rootIndexes;
    for (std::size_t index = 0; index < decoded.fileList.files.size(); ++index) {
        const TransferFileDescriptor& file = decoded.fileList.files[index];
        if (!file.directory && remoteFileReader_ == nullptr) {
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                "macOS remote file promise needs a remote file reader";
            return protocol::ResponseStatus::Failed;
        }
        if (!file.directory &&
            options_.maxSingleFileBytes != 0 &&
            file.sizeBytes > options_.maxSingleFileBytes) {
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                "macOS promised file exceeds max single file bytes";
            return protocol::ResponseStatus::TooLarge;
        }
        const std::string relative =
            !file.relativePath.empty() ? file.relativePath : file.displayName;
        if (relativePathIsTopLevel(relative))
            rootIndexes.push_back(index);
    }

    if (rootIndexes.empty()) {
        rootIndexes.reserve(decoded.fileList.files.size());
        for (std::size_t index = 0; index < decoded.fileList.files.size(); ++index)
            rootIndexes.push_back(index);
    }

    NSPasteboard* pasteboard = generalPasteboard();
    if (pasteboard == nil) {
        ++diagnostics_.remoteFilePromiseFailures;
        diagnostics_.lastMessage = "macOS pasteboard is unavailable";
        return protocol::ResponseStatus::Failed;
    }

    NSMutableArray* providers = [NSMutableArray array];
    NSMutableArray* keepAlive = [[NSMutableArray alloc] init];
    for (std::size_t rootIndex : rootIndexes) {
        const TransferFileDescriptor& file = decoded.fileList.files[rootIndex];
        FusionDeskMacFilePromiseState* state =
            new FusionDeskMacFilePromiseState;
        state->bundle = bundle;
        state->listRequest = listRequest;
        state->fileList = decoded.fileList;
        state->rootFileIndex = static_cast<std::uint32_t>(rootIndex);
        state->fileName = promisedFileName(file);
        state->remoteFileReader = remoteFileReader_;
        state->remoteObjectLocker = remoteObjectLocker_;
        state->maxFileRangeBytes = options_.maxFileRangeBytes;
        state->maxSingleFileBytes = options_.maxSingleFileBytes;

        FusionDeskMacFilePromiseDelegate* delegate =
            [[FusionDeskMacFilePromiseDelegate alloc] initWithState:state];
        if (delegate == nil) {
            delete state;
            [keepAlive release];
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                "macOS remote file promise delegate cannot be created";
            return protocol::ResponseStatus::Failed;
        }
        NSString* fileType = stringFromUtf8(filePromiseType(file));
        NSFilePromiseProvider* provider =
            [[NSFilePromiseProvider alloc] initWithFileType:fileType
                                                   delegate:delegate];
        [fileType release];
        if (provider == nil) {
            [delegate release];
            [keepAlive release];
            ++diagnostics_.remoteFilePromiseFailures;
            diagnostics_.lastMessage =
                "macOS remote file promise provider cannot be created";
            return protocol::ResponseStatus::Failed;
        }
        [providers addObject:provider];
        [keepAlive addObject:delegate];
        [keepAlive addObject:provider];
        [delegate release];
        [provider release];
    }

    if ([providers count] == 0) {
        [keepAlive release];
        ++diagnostics_.remoteFilePromiseFailures;
        diagnostics_.lastMessage =
            "macOS remote file promise publication has no providers";
        return protocol::ResponseStatus::NotFound;
    }

    [pasteboard clearContents];
    const BOOL written = [pasteboard writeObjects:providers];
    if (!written) {
        [keepAlive release];
        ++diagnostics_.remoteFilePromiseFailures;
        diagnostics_.lastMessage =
            "macOS remote file promises were not written to pasteboard";
        return protocol::ResponseStatus::Failed;
    }

    if (pasteboardProvider_ != nullptr) {
        [(id)pasteboardProvider_ release];
        pasteboardProvider_ = nullptr;
    }
    cleanupRemoteFilePublicationState();
    filePromiseDelegates_ = keepAlive;
    publishedBundle_ = bundle;
    diagnostics_.publishedOfferId = bundle.offerId;
    diagnostics_.lastNativeSequence = bundle.sequence;
    lastPublishedChangeCount_ = pasteboardChangeCount();
    lastObservedChangeCount_ = lastPublishedChangeCount_;
    diagnostics_.lastNativeChangeCount = lastObservedChangeCount_;
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = false;
    ++diagnostics_.remoteFilePromisePublishes;
    diagnostics_.remoteFilePromiseProviders +=
        static_cast<std::uint64_t>([providers count]);
    ++diagnostics_.publishes;
    diagnostics_.lastMessage = "macOS remote file promises published";
    return protocol::ResponseStatus::Ok;
}

ClipboardSnapshot MacClipboardEndpoint::snapshotFromText(
    const std::string& text,
    std::uint64_t sequence)
{
    ClipboardSnapshot snapshot;
    if (text.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = TextPlainUtf8Format;
    descriptor.nativeFormatName = MacTextType;
    descriptor.localFormatToken = 1;
    descriptor.formatId = nextFormatId_++;
    descriptor.estimatedBytes = text.size();
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    MaterializedTransferEntry entry;
    entry.descriptor = descriptor;
    entry.bytes = bytesFromString(text);

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
    presentation.displayName = "macOS pasteboard text";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Text;
    bundle.presentation = std::move(presentation);

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot MacClipboardEndpoint::snapshotFromFormattedText(
    const std::string& text,
    const protocol::ByteBuffer& html,
    const protocol::ByteBuffer& rtf,
    std::uint64_t sequence)
{
    ClipboardSnapshot snapshot;
    std::vector<MaterializedTransferEntry> entries;
    if (!html.empty()) {
        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = TextHtmlFormat;
        descriptor.nativeFormatName = MacHtmlType;
        descriptor.localFormatToken = 3;
        descriptor.formatId = nextFormatId_++;
        descriptor.estimatedBytes = html.size();
        descriptor.canInline = true;
        descriptor.canStream = false;
        descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;
        entries.push_back(MaterializedTransferEntry{descriptor, html});
    }

    if (!rtf.empty()) {
        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = TextRtfFormat;
        descriptor.nativeFormatName = MacRtfType;
        descriptor.localFormatToken = 4;
        descriptor.formatId = nextFormatId_++;
        descriptor.estimatedBytes = rtf.size();
        descriptor.canInline = true;
        descriptor.canStream = false;
        descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;
        entries.push_back(MaterializedTransferEntry{descriptor, rtf});
    }

    if (!text.empty()) {
        TransferFormatDescriptor descriptor;
        descriptor.canonicalFormat = TextPlainUtf8Format;
        descriptor.nativeFormatName = MacTextType;
        descriptor.localFormatToken = 1;
        descriptor.formatId = nextFormatId_++;
        descriptor.estimatedBytes = text.size();
        descriptor.canInline = true;
        descriptor.canStream = false;
        descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;
        entries.push_back(MaterializedTransferEntry{descriptor,
                                                    bytesFromString(text)});
    }

    if (entries.empty())
        return snapshot;

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
        std::move(entries)));

    TransferPresentation presentation;
    presentation.displayName = !html.empty() ? "macOS pasteboard HTML"
                                             : "macOS pasteboard RTF";
    presentation.itemCount = 1;
    presentation.sourceKind = TransferSourceKind::Text;
    bundle.presentation = std::move(presentation);

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot MacClipboardEndpoint::snapshotFromImagePng(
    const protocol::ByteBuffer& pngBytes,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t sequence)
{
    ClipboardSnapshot snapshot;
    if (pngBytes.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = ImagePngFormat;
    descriptor.nativeFormatName = MacPngType;
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
    presentation.displayName = "macOS pasteboard image";
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

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    return snapshot;
}

ClipboardSnapshot MacClipboardEndpoint::snapshotFromFileList(
    const TransferFileList& fileList,
    std::vector<std::filesystem::path> paths,
    std::uint64_t sequence)
{
    ClipboardSnapshot snapshot;
    if (fileList.files.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = MacFileUrlType;
    descriptor.localFormatToken = 6;
    descriptor.formatId = nextFormatId_++;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = encodeTransferFileList(fileList).size();
    descriptor.canInline = true;
    descriptor.canStream = paths.size() == fileList.files.size();
    descriptor.preferredEncoding = TransferEncodingMode::CanonicalBytes;

    TransferPresentation presentation;
    presentation.itemCount = static_cast<std::uint32_t>(fileList.files.size());
    presentation.sourceKind = TransferSourceKind::FileList;
    presentation.allowedActions = transfer_action::Copy;
    presentation.preferredAction = TransferAction::Copy;
    presentation.displayName =
        fileList.files.size() == 1
            ? sanitizeTransferFileDisplayName(fileList.files.front().displayName)
            : std::string("files");

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
    bundle.presentation = presentation;
    if (descriptor.canStream) {
        bundle.sources.push_back(createLocalMacFileTransferSource(
            nextSourceId_++,
            descriptor,
            fileList,
            std::move(paths),
            options_.maxFileRangeBytes,
            options_.maxSingleFileBytes));
    } else {
        bundle.sources.push_back(std::make_shared<FileGroupTransferSource>(
            nextSourceId_++,
            descriptor,
            fileList));
    }

    snapshot.ownerEpoch = bundle.ownerEpoch;
    snapshot.sequence = bundle.sequence;
    snapshot.bundle = std::move(bundle);
    ++diagnostics_.fileListSnapshots;
    return snapshot;
}

bool MacClipboardEndpoint::hasTextFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(), formats.end(), descriptorIsText))
            return true;
    }
    return false;
}

bool MacClipboardEndpoint::hasHtmlFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(), formats.end(), descriptorIsHtml))
            return true;
    }
    return false;
}

bool MacClipboardEndpoint::hasRtfFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(), formats.end(), descriptorIsRtf))
            return true;
    }
    return false;
}

bool MacClipboardEndpoint::hasImagePngFormat(
    const TransferSourceBundle& bundle) const
{
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;
        const std::vector<TransferFormatDescriptor> formats = source->formats();
        if (std::any_of(formats.begin(), formats.end(), descriptorIsImagePng))
            return true;
    }
    return false;
}

bool MacClipboardEndpoint::hasSupportedDelayedFormat(
    const TransferSourceBundle& bundle) const
{
    return hasTextFormat(bundle) || hasHtmlFormat(bundle) ||
           hasRtfFormat(bundle) || hasImagePngFormat(bundle);
}

void MacClipboardEndpoint::cleanupRemoteFilePublicationState()
{
    if (filePromiseDelegates_ != nullptr) {
        [(id)filePromiseDelegates_ release];
        filePromiseDelegates_ = nullptr;
    }
}

} // namespace clipboard
} // namespace macos
} // namespace platform
} // namespace fusiondesk
