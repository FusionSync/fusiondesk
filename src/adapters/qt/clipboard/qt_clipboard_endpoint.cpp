#include "fusiondesk/adapters/qt/clipboard/qt_clipboard_endpoint.h"

#include "qt_clipboard_local_files.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include <QByteArray>
#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QMetaObject>
#include <QMimeData>
#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QVariant>

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace clipboard {

using namespace fusiondesk::modules::clipboard;

namespace {

constexpr const char* QtRtfMimeName = "text/rtf";
constexpr const char* QtRtfAlternateMimeName = "application/rtf";

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

std::string stringFromBytes(const protocol::ByteBuffer& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

QClipboard* qtClipboard()
{
    return QGuiApplication::clipboard();
}

bool descriptorIsFileList(const TransferFormatDescriptor& descriptor)
{
    return descriptor.canonicalFormat == FdclFileListFormat ||
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

bool clipboardContainsUrls(QClipboard* clipboard, const QList<QUrl>& urls)
{
    if (clipboard == nullptr || urls.empty())
        return false;

    const QMimeData* current = clipboard->mimeData();
    if (current == nullptr || !current->hasUrls())
        return false;

    const QList<QUrl> currentUrls = current->urls();
    if (currentUrls.size() < urls.size())
        return false;

    for (const QUrl& expected : urls) {
        bool found = false;
        for (const QUrl& actual : currentUrls) {
            if (actual == expected ||
                (actual.isLocalFile() && expected.isLocalFile() &&
                 actual.toLocalFile() == expected.toLocalFile())) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

bool clipboardContainsText(QClipboard* clipboard, const QString& text)
{
    if (clipboard == nullptr)
        return false;

    return clipboard->text() == text;
}

bool clipboardContainsHtml(QClipboard* clipboard,
                           const QString& html,
                           const QString& text)
{
    if (clipboard == nullptr)
        return false;

    const QMimeData* current = clipboard->mimeData();
    if (current == nullptr || !current->hasHtml() ||
        current->html() != html) {
        return false;
    }

    return text.isEmpty() || current->text() == text;
}

QByteArray byteArrayFromBytes(const protocol::ByteBuffer& bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<int>(bytes.size()));
}

protocol::ByteBuffer bytesFromByteArray(const QByteArray& bytes)
{
    return protocol::ByteBuffer(
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        reinterpret_cast<const std::uint8_t*>(bytes.constData()) +
            bytes.size());
}

bool encodeImagePng(const QImage& image, protocol::ByteBuffer* bytes)
{
    if (bytes == nullptr || image.isNull())
        return false;

    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly))
        return false;
    if (!image.save(&buffer, "PNG"))
        return false;
    *bytes = bytesFromByteArray(encoded);
    return !bytes->empty();
}

bool sameImagePixels(const QImage& left, const QImage& right)
{
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return false;

    const QImage normalizedLeft =
        left.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage normalizedRight =
        right.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (normalizedLeft.size() != normalizedRight.size())
        return false;

    const int bytesPerRow = normalizedLeft.width() * 4;
    for (int y = 0; y < normalizedLeft.height(); ++y) {
        if (std::memcmp(normalizedLeft.constScanLine(y),
                        normalizedRight.constScanLine(y),
                        static_cast<std::size_t>(bytesPerRow)) != 0) {
            return false;
        }
    }
    return true;
}

bool clipboardContainsImage(QClipboard* clipboard, const QImage& image)
{
    if (clipboard == nullptr || image.isNull())
        return false;

    return sameImagePixels(clipboard->image(), image);
}

bool clipboardContainsMimeBytes(QClipboard* clipboard,
                                const char* mimeName,
                                const QByteArray& bytes,
                                const QString& text)
{
    if (clipboard == nullptr)
        return false;

    const QMimeData* current = clipboard->mimeData();
    if (current == nullptr || !current->hasFormat(mimeName) ||
        current->data(mimeName) != bytes) {
        return false;
    }

    return text.isEmpty() || current->text() == text;
}

std::string utf8FromQString(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

bool buildMaterializedOutputPath(
    const std::filesystem::path& root,
    const TransferFileDescriptor& file,
    std::filesystem::path* output)
{
    const std::string relative =
        !file.relativePath.empty() ? file.relativePath : file.displayName;
    if (relative.empty())
        return false;

    const std::filesystem::path requested(relative);
    if (requested.is_absolute() || requested.has_root_name() ||
        requested.has_root_directory()) {
        return false;
    }

    std::filesystem::path safeRelative;
    for (const std::filesystem::path& part : requested) {
        const std::string value = part.string();
        if (value.empty() || value == ".")
            continue;
        if (value == "..")
            return false;
        safeRelative /= part;
    }
    if (safeRelative.empty())
        return false;

    *output = root / safeRelative;
    return true;
}

class QtRemoteObjectLockGuard final
{
public:
    ~QtRemoteObjectLockGuard()
    {
        unlockSilently();
    }

    QtRemoteObjectLockGuard() = default;
    QtRemoteObjectLockGuard(const QtRemoteObjectLockGuard&) = delete;
    QtRemoteObjectLockGuard& operator=(const QtRemoteObjectLockGuard&) = delete;

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
                *message = "qt remote file object lock returned empty id";
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

class QtMaterializedFileSink final : public ITransferFileRangeSink
{
public:
    explicit QtMaterializedFileSink(std::filesystem::path path)
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
            result.message = "qt materialized file cannot be opened";
            return result;
        }
        if (!bytes.empty()) {
            stream_.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
        }
        if (!stream_) {
            result.status = protocol::ResponseStatus::Failed;
            result.message = "qt materialized file write failed";
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

QtClipboardEndpoint::QtClipboardEndpoint(
    QtClipboardEndpointOptions options,
    std::shared_ptr<IClipboardRemoteReader> remoteReader,
    std::shared_ptr<IClipboardRemoteFileReader> remoteFileReader,
    std::shared_ptr<IClipboardRemoteObjectLocker> remoteObjectLocker)
    : options_(std::move(options)),
      remoteReader_(std::move(remoteReader)),
      remoteFileReader_(std::move(remoteFileReader)),
      remoteObjectLocker_(std::move(remoteObjectLocker)),
      nextBundleId_(options_.firstBundleId),
      nextOfferId_(options_.firstOfferId),
      nextSourceId_(options_.firstSourceId),
      nextFormatId_(options_.firstFormatId)
{
    if (remoteFileReader_ == nullptr)
        remoteFileReader_ =
            std::dynamic_pointer_cast<IClipboardRemoteFileReader>(remoteReader_);
    if (remoteObjectLocker_ == nullptr)
        remoteObjectLocker_ =
            std::dynamic_pointer_cast<IClipboardRemoteObjectLocker>(remoteReader_);

    QClipboard* clipboard = qtClipboard();
    if (clipboard != nullptr && options_.enableChangeMonitor) {
        auto connection = new QMetaObject::Connection;
        *connection = QObject::connect(
            clipboard,
            &QClipboard::dataChanged,
            clipboard,
            [this]() {
                handleNativeClipboardChanged();
            });
        changeConnection_ = connection;
    }
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
}

QtClipboardEndpoint::~QtClipboardEndpoint()
{
    cleanupMaterializedRemoteFiles();

    auto* connection =
        static_cast<QMetaObject::Connection*>(changeConnection_);
    if (connection != nullptr) {
        QObject::disconnect(*connection);
        delete connection;
    }
}

ClipboardSnapshot QtClipboardEndpoint::snapshot()
{
    ++diagnostics_.snapshots;
    QClipboard* clipboard = qtClipboard();
    if (clipboard == nullptr) {
        diagnostics_.lastMessage = "qt clipboard is unavailable";
        return {};
    }

    const QMimeData* mimeData = clipboard->mimeData();
    if (mimeData != nullptr && mimeData->hasUrls()) {
        TransferFileList fileList;
        std::vector<std::filesystem::path> paths;
        QtClipboardLocalFileOptions options;
        options.expandDirectories = options_.expandDroppedDirectories;
        options.maxFileCount = options_.maxFileCount;
        options.maxDirectoryDepth = options_.maxDirectoryDepth;
        options.maxFileRangeBytes = options_.maxFileRangeBytes;
        options.maxSingleFileBytes = options_.maxSingleFileBytes;
        if (readQtLocalFileUrlList(fileList, &paths, mimeData->urls(), options)) {
            return snapshotFromFileList(fileList,
                                        std::move(paths),
                                        ++diagnostics_.lastNativeSequence);
        }
    }

    if (mimeData != nullptr && mimeData->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mimeData->imageData());
        protocol::ByteBuffer pngBytes;
        if (!image.isNull() && encodeImagePng(image, &pngBytes)) {
            return snapshotFromImagePng(
                pngBytes,
                static_cast<std::uint32_t>(image.width()),
                static_cast<std::uint32_t>(image.height()),
                ++diagnostics_.lastNativeSequence);
        }
    }

    if (mimeData != nullptr &&
        (mimeData->hasHtml() ||
         mimeData->hasFormat(QtRtfMimeName) ||
         mimeData->hasFormat(QtRtfAlternateMimeName))) {
        const std::string html =
            mimeData->hasHtml() ? utf8FromQString(mimeData->html())
                                : std::string{};
        std::string rtf;
        if (mimeData->hasFormat(QtRtfMimeName)) {
            rtf = stringFromBytes(
                bytesFromByteArray(mimeData->data(QtRtfMimeName)));
        } else if (mimeData->hasFormat(QtRtfAlternateMimeName)) {
            rtf = stringFromBytes(
                bytesFromByteArray(mimeData->data(QtRtfAlternateMimeName)));
        }
        const std::string text =
            mimeData->hasText()
                ? utf8FromQString(mimeData->text())
                : utf8FromQString(clipboard->text());
        return snapshotFromFormattedText(text,
                                         html,
                                         rtf,
                                         ++diagnostics_.lastNativeSequence);
    }

    return snapshotFromText(utf8FromQString(clipboard->text()),
                            ++diagnostics_.lastNativeSequence);
}

protocol::ResponseStatus QtClipboardEndpoint::publishBundle(
    const ClipboardPublishRequest& request)
{
    QClipboard* clipboard = qtClipboard();
    if (clipboard == nullptr) {
        diagnostics_.lastMessage = "qt clipboard is unavailable";
        return protocol::ResponseStatus::Failed;
    }

    const protocol::ResponseStatus fileStatus =
        publishFileListBundle(request.bundle);
    if (fileStatus != protocol::ResponseStatus::NotFound)
        return fileStatus;

    const protocol::ResponseStatus imageStatus =
        publishImagePngBundle(request.bundle);
    if (imageStatus == protocol::ResponseStatus::Ok)
        return imageStatus;

    TransferReadResult html = readBestHtml(request.bundle);
    if (html.ok()) {
        TransferReadResult fallbackText = readBestText(request.bundle);
        std::string textValue;
        if (fallbackText.ok())
            textValue = stringFromBytes(fallbackText.bytes);

        const std::string htmlValue = stringFromBytes(html.bytes);
        cleanupMaterializedRemoteFiles();
        const QString publishedHtml =
            QString::fromUtf8(htmlValue.data(),
                              static_cast<int>(htmlValue.size()));
        const QString publishedText =
            QString::fromUtf8(textValue.data(),
                              static_cast<int>(textValue.size()));
        bool observedHtml = false;
        for (int attempt = 0; attempt < 20 && !observedHtml; ++attempt) {
            auto* mimeData = new QMimeData;
            mimeData->setHtml(publishedHtml);
            if (!publishedText.isEmpty())
                mimeData->setText(publishedText);
            settingClipboard_ = true;
            clipboard->setMimeData(mimeData);
            settingClipboard_ = false;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            observedHtml =
                clipboardContainsHtml(clipboard, publishedHtml, publishedText);
            if (!observedHtml)
                QThread::msleep(25);
        }
        if (!observedHtml) {
            diagnostics_.lastMessage =
                "qt clipboard html was not observed on clipboard";
            return protocol::ResponseStatus::Failed;
        }

        lastPublishedText_ = textValue;
        lastPublishedHtml_ = htmlValue;
        lastPublishedRtf_.clear();
        lastPublishedImagePng_.clear();
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
        diagnostics_.publishedOfferId = request.bundle.offerId;
        diagnostics_.lastNativeSequence = request.bundle.sequence;
        ++diagnostics_.publishes;
        diagnostics_.lastMessage = "qt clipboard html published";
        return protocol::ResponseStatus::Ok;
    }

    TransferReadResult rtf = readBestRtf(request.bundle);
    if (rtf.ok()) {
        TransferReadResult fallbackText = readBestText(request.bundle);
        std::string textValue;
        if (fallbackText.ok())
            textValue = stringFromBytes(fallbackText.bytes);

        const QByteArray publishedRtf = byteArrayFromBytes(rtf.bytes);
        const QString publishedText =
            QString::fromUtf8(textValue.data(),
                              static_cast<int>(textValue.size()));
        cleanupMaterializedRemoteFiles();
        bool observedRtf = false;
        for (int attempt = 0; attempt < 20 && !observedRtf; ++attempt) {
            auto* mimeData = new QMimeData;
            mimeData->setData(QtRtfMimeName, publishedRtf);
            if (!publishedText.isEmpty())
                mimeData->setText(publishedText);
            settingClipboard_ = true;
            clipboard->setMimeData(mimeData);
            settingClipboard_ = false;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            observedRtf = clipboardContainsMimeBytes(clipboard,
                                                     QtRtfMimeName,
                                                     publishedRtf,
                                                     publishedText);
            if (!observedRtf)
                QThread::msleep(25);
        }
        if (!observedRtf) {
            diagnostics_.lastMessage =
                "qt clipboard rtf was not observed on clipboard";
            return protocol::ResponseStatus::Failed;
        }

        lastPublishedText_ = textValue;
        lastPublishedHtml_.clear();
        lastPublishedRtf_ = stringFromBytes(rtf.bytes);
        lastPublishedImagePng_.clear();
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
        diagnostics_.publishedOfferId = request.bundle.offerId;
        diagnostics_.lastNativeSequence = request.bundle.sequence;
        ++diagnostics_.publishes;
        diagnostics_.lastMessage = "qt clipboard rtf published";
        return protocol::ResponseStatus::Ok;
    }

    TransferReadResult text = readBestText(request.bundle);
    if (!text.ok()) {
        ++diagnostics_.readFailures;
        if (imageStatus != protocol::ResponseStatus::NotFound) {
            diagnostics_.lastMessage =
                diagnostics_.lastMessage.empty()
                    ? "qt clipboard image/png publish failed"
                    : diagnostics_.lastMessage;
            return imageStatus;
        }
        if (html.status != protocol::ResponseStatus::NotFound) {
            diagnostics_.lastMessage = html.message;
            return html.status;
        }
        if (rtf.status != protocol::ResponseStatus::NotFound) {
            diagnostics_.lastMessage = rtf.message;
            return rtf.status;
        }
        diagnostics_.lastMessage = text.message;
        return text.status;
    }

    const std::string value = stringFromBytes(text.bytes);
    cleanupMaterializedRemoteFiles();
    const QString publishedText =
        QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    bool observedText = false;
    for (int attempt = 0; attempt < 20 && !observedText; ++attempt) {
        settingClipboard_ = true;
        clipboard->setText(publishedText);
        settingClipboard_ = false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        observedText = clipboardContainsText(clipboard, publishedText);
        if (!observedText)
            QThread::msleep(25);
    }
    if (!observedText) {
        diagnostics_.lastMessage =
            "qt clipboard text was not observed on clipboard";
        return protocol::ResponseStatus::Failed;
    }

    lastPublishedText_ = value;
    lastPublishedHtml_.clear();
    lastPublishedRtf_.clear();
    lastPublishedImagePng_.clear();
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
    diagnostics_.publishedOfferId = request.bundle.offerId;
    diagnostics_.lastNativeSequence = request.bundle.sequence;
    ++diagnostics_.publishes;
    diagnostics_.lastMessage = "qt clipboard text published";
    return protocol::ResponseStatus::Ok;
}

protocol::ResponseStatus QtClipboardEndpoint::clearPublishedBundle(
    TransferOfferId offerId)
{
    if (diagnostics_.publishedOfferId != 0 &&
        offerId != 0 &&
        diagnostics_.publishedOfferId != offerId) {
        return protocol::ResponseStatus::Conflict;
    }

    QClipboard* clipboard = qtClipboard();
    if (clipboard == nullptr) {
        diagnostics_.lastMessage = "qt clipboard is unavailable";
        return protocol::ResponseStatus::Failed;
    }

    settingClipboard_ = true;
    clipboard->clear();
    settingClipboard_ = false;
    lastPublishedText_.clear();
    lastPublishedHtml_.clear();
    lastPublishedRtf_.clear();
    lastPublishedImagePng_.clear();
    cleanupMaterializedRemoteFiles();
    diagnostics_.publishedOfferId = 0;
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
    ++diagnostics_.clears;
    diagnostics_.lastMessage = "qt clipboard cleared";
    return protocol::ResponseStatus::Ok;
}

bool QtClipboardEndpoint::hasPendingClipboardChange() const
{
    return pendingNativeClipboardChange_;
}

void QtClipboardEndpoint::markClipboardChangeConsumed()
{
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
}

QtClipboardEndpointDiagnostics QtClipboardEndpoint::diagnostics() const
{
    QtClipboardEndpointDiagnostics copy = diagnostics_;
    copy.nativeChangePending = pendingNativeClipboardChange_;
    return copy;
}

protocol::ResponseStatus QtClipboardEndpoint::publishImagePngBundle(
    const TransferSourceBundle& bundle)
{
    TransferReadResult image = readBestImagePng(bundle);
    if (image.status == protocol::ResponseStatus::NotFound)
        return protocol::ResponseStatus::NotFound;
    if (!image.ok()) {
        ++diagnostics_.readFailures;
        diagnostics_.lastMessage = image.message;
        return image.status;
    }

    const QByteArray encoded = byteArrayFromBytes(image.bytes);
    QImage publishedImage;
    if (!publishedImage.loadFromData(encoded, "PNG") ||
        publishedImage.isNull()) {
        diagnostics_.lastMessage = "qt clipboard image/png decode failed";
        return protocol::ResponseStatus::Failed;
    }

    QClipboard* clipboard = qtClipboard();
    if (clipboard == nullptr) {
        diagnostics_.lastMessage = "qt clipboard is unavailable";
        return protocol::ResponseStatus::Failed;
    }

    cleanupMaterializedRemoteFiles();
    bool observedImage = false;
    for (int attempt = 0; attempt < 20 && !observedImage; ++attempt) {
        settingClipboard_ = true;
        clipboard->setImage(publishedImage);
        settingClipboard_ = false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        observedImage = clipboardContainsImage(clipboard, publishedImage);
        if (!observedImage)
            QThread::msleep(25);
    }
    if (!observedImage) {
        diagnostics_.lastMessage =
            "qt clipboard image was not observed on clipboard";
        return protocol::ResponseStatus::Failed;
    }

    lastPublishedText_.clear();
    lastPublishedHtml_.clear();
    lastPublishedRtf_.clear();
    lastPublishedImagePng_ = image.bytes;
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
    diagnostics_.publishedOfferId = bundle.offerId;
    diagnostics_.lastNativeSequence = bundle.sequence;
    ++diagnostics_.publishes;
    diagnostics_.lastMessage = "qt clipboard image/png published";
    return protocol::ResponseStatus::Ok;
}

protocol::ResponseStatus QtClipboardEndpoint::publishFileListBundle(
    const TransferSourceBundle& bundle)
{
    TransferReadRequest listRequest;
    TransferReadResult listRead;
    bool foundFileList = false;
    for (const std::shared_ptr<TransferSource>& source : bundle.sources) {
        if (source == nullptr)
            continue;

        const std::vector<TransferFormatDescriptor> formats =
            source->formats();
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
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage = listRead.message.empty()
                                       ? "qt remote file-list read failed"
                                       : listRead.message;
        return listRead.status;
    }

    const TransferFileListDecodeResult decoded =
        decodeTransferFileList(listRead.bytes, options_.maxFileCount);
    if (!decoded.ok) {
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage =
            "qt remote file-list decode failed: " + decoded.message;
        return decoded.status;
    }
    if (decoded.fileList.files.empty()) {
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage = "qt remote file-list is empty";
        return protocol::ResponseStatus::NotFound;
    }

    for (const TransferFileDescriptor& file : decoded.fileList.files) {
        if (!file.directory &&
            remoteFileReader_ == nullptr) {
            ++diagnostics_.remoteFileMaterializationFailures;
            diagnostics_.lastMessage =
                "qt remote file publication needs a remote file reader";
            return protocol::ResponseStatus::Failed;
        }
        if (!file.directory &&
            options_.maxSingleFileBytes != 0 &&
            file.sizeBytes > options_.maxSingleFileBytes) {
            ++diagnostics_.remoteFileMaterializationFailures;
            diagnostics_.lastMessage =
                "qt remote file exceeds max single file bytes";
            return protocol::ResponseStatus::TooLarge;
        }
    }

    cleanupMaterializedRemoteFiles();
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(error) /
        ("fusiondesk_qt_clipboard_" +
         std::to_string(bundle.offerId) + "_" +
         std::to_string(monotonicNowUsec()));
    if (error) {
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage =
            "qt remote file temp directory path is unavailable";
        return protocol::ResponseStatus::Failed;
    }
    std::filesystem::create_directories(root, error);
    if (error) {
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage =
            "qt remote file temp directory cannot be created";
        return protocol::ResponseStatus::Failed;
    }

    QList<QUrl> urls;
    std::vector<QUrl> fallbackFileUrls;
    std::uint64_t materializedFiles = 0;
    std::uint64_t materializedDirectories = 0;
    std::uint64_t materializedBytes = 0;
    for (std::size_t index = 0; index < decoded.fileList.files.size(); ++index) {
        const TransferFileDescriptor& file = decoded.fileList.files[index];
        std::filesystem::path outputPath;
        if (!buildMaterializedOutputPath(root, file, &outputPath)) {
            ++diagnostics_.remoteFileMaterializationFailures;
            diagnostics_.lastMessage =
                "qt remote file path is unsafe: " + file.relativePath;
            std::filesystem::remove_all(root, error);
            return protocol::ResponseStatus::InvalidArgument;
        }

        if (file.directory) {
            std::filesystem::create_directories(outputPath, error);
            if (error) {
                ++diagnostics_.remoteFileMaterializationFailures;
                diagnostics_.lastMessage =
                    "qt remote directory cannot be materialized";
                std::filesystem::remove_all(root, error);
                return protocol::ResponseStatus::Failed;
            }
            ++materializedDirectories;
            if (relativePathIsTopLevel(
                    !file.relativePath.empty()
                        ? file.relativePath
                        : file.displayName)) {
                urls.push_back(QUrl::fromLocalFile(
                    QString::fromStdString(outputPath.string())));
            }
            continue;
        }

        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            ++diagnostics_.remoteFileMaterializationFailures;
            diagnostics_.lastMessage =
                "qt remote file parent directory cannot be materialized";
            std::filesystem::remove_all(root, error);
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
        QtRemoteObjectLockGuard lockGuard;
        const protocol::ResponseStatus locked =
            lockGuard.lock(remoteObjectLocker_.get(),
                           lockRequest,
                           30000,
                           &lockMessage);
        if (locked != protocol::ResponseStatus::Ok) {
            ++diagnostics_.remoteFileMaterializationFailures;
            diagnostics_.lastMessage =
                "qt remote file object lock failed: " + lockMessage;
            std::filesystem::remove_all(root, error);
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
        drainOptions.chunkBytes = options_.maxFileRangeBytes == 0
                                      ? DefaultTransferFileRangeChunkBytes
                                      : options_.maxFileRangeBytes;
        drainOptions.timeoutMs = 30000;
        drainOptions.maxTotalBytes =
            file.sizeBytes == 0 ? options_.maxSingleFileBytes : file.sizeBytes;

        QtMaterializedFileSink sink(outputPath);
        const TransferFileDrainResult drained =
            drainRemoteFileRange(*remoteFileReader_,
                                 rangeRequest,
                                 sink,
                                 drainOptions);
        if (!drained.ok() || !drained.endOfFile) {
            ++diagnostics_.remoteFileMaterializationFailures;
            diagnostics_.lastMessage =
                "qt remote file materialization failed: " +
                (drained.message.empty()
                     ? std::string("missing eof")
                     : drained.message);
            std::filesystem::remove_all(root, error);
            return drained.ok() ? protocol::ResponseStatus::Failed
                                : drained.status;
        }

        std::string unlockMessage;
        const protocol::ResponseStatus unlocked =
            lockGuard.unlock(&unlockMessage);
        if (unlocked != protocol::ResponseStatus::Ok) {
            ++diagnostics_.remoteFileMaterializationFailures;
            diagnostics_.lastMessage =
                "qt remote file object unlock failed: " + unlockMessage;
            std::filesystem::remove_all(root, error);
            return unlocked;
        }

        ++materializedFiles;
        materializedBytes += drained.bytesWritten;
        const QUrl fileUrl = QUrl::fromLocalFile(
            QString::fromStdString(outputPath.string()));
        fallbackFileUrls.push_back(fileUrl);
        if (relativePathIsTopLevel(
                !file.relativePath.empty() ? file.relativePath : file.displayName)) {
            urls.push_back(fileUrl);
        }
    }

    if (urls.empty()) {
        for (const QUrl& url : fallbackFileUrls)
            urls.push_back(url);
    }
    if (urls.empty()) {
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage = "qt remote file publication has no urls";
        std::filesystem::remove_all(root, error);
        return protocol::ResponseStatus::NotFound;
    }

    QClipboard* clipboard = qtClipboard();
    if (clipboard == nullptr) {
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage = "qt clipboard is unavailable";
        std::filesystem::remove_all(root, error);
        return protocol::ResponseStatus::Failed;
    }

    bool publishedUrls = false;
    for (int attempt = 0; attempt < 20 && !publishedUrls; ++attempt) {
        auto* mimeData = new QMimeData;
        mimeData->setUrls(urls);
        settingClipboard_ = true;
        clipboard->setMimeData(mimeData);
        settingClipboard_ = false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        publishedUrls = clipboardContainsUrls(clipboard, urls);
        if (!publishedUrls)
            QThread::msleep(25);
    }
    if (!publishedUrls) {
        ++diagnostics_.remoteFileMaterializationFailures;
        diagnostics_.lastMessage =
            "qt remote file urls were not observed on clipboard";
        std::filesystem::remove_all(root, error);
        return protocol::ResponseStatus::Failed;
    }

    materializedRemoteFilesRoot_ = root;
    lastPublishedText_.clear();
    lastPublishedHtml_.clear();
    lastPublishedRtf_.clear();
    lastPublishedImagePng_.clear();
    pendingNativeClipboardChange_ = false;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
    diagnostics_.publishedOfferId = bundle.offerId;
    diagnostics_.lastNativeSequence = bundle.sequence;
    diagnostics_.remoteFilesMaterialized += materializedFiles;
    diagnostics_.remoteDirectoriesMaterialized += materializedDirectories;
    diagnostics_.remoteFileBytesMaterialized += materializedBytes;
    ++diagnostics_.remoteFilePublishes;
    ++diagnostics_.publishes;
    diagnostics_.lastMessage = "qt remote file clipboard published";
    return protocol::ResponseStatus::Ok;
}

ClipboardSnapshot QtClipboardEndpoint::snapshotFromFileList(
    const TransferFileList& fileList,
    std::vector<std::filesystem::path> paths,
    std::uint64_t sequence)
{
    ClipboardSnapshot snapshot;
    if (fileList.files.empty())
        return snapshot;

    TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = FdclFileListFormat;
    descriptor.nativeFormatName = "text/uri-list";
    descriptor.localFormatToken = 2;
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
        bundle.sources.push_back(createLocalQtFileTransferSource(
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

void QtClipboardEndpoint::handleNativeClipboardChanged()
{
    ++diagnostics_.nativeChangeNotifications;
    if (settingClipboard_) {
        if (options_.suppressOwnClipboardUpdates) {
            ++diagnostics_.ownerSuppressions;
            pendingNativeClipboardChange_ = false;
            diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
            return;
        }
    }

    QClipboard* clipboard = qtClipboard();
    const std::string current =
        clipboard != nullptr ? utf8FromQString(clipboard->text()) : std::string{};
    std::string currentHtml;
    std::string currentRtf;
    protocol::ByteBuffer currentImagePng;
    if (clipboard != nullptr) {
        const QMimeData* mimeData = clipboard->mimeData();
        if (mimeData != nullptr) {
            if (mimeData->hasHtml())
                currentHtml = utf8FromQString(mimeData->html());
            if (mimeData->hasFormat(QtRtfMimeName)) {
                currentRtf = stringFromBytes(
                    bytesFromByteArray(mimeData->data(QtRtfMimeName)));
            }
            if (mimeData->hasImage()) {
                const QImage image = qvariant_cast<QImage>(mimeData->imageData());
                encodeImagePng(image, &currentImagePng);
            }
        }
    }
    if (options_.suppressOwnClipboardUpdates &&
        (!lastPublishedText_.empty() || !lastPublishedHtml_.empty() ||
         !lastPublishedRtf_.empty() || !lastPublishedImagePng_.empty()) &&
        current == lastPublishedText_ &&
        currentHtml == lastPublishedHtml_ &&
        currentRtf == lastPublishedRtf_ &&
        currentImagePng == lastPublishedImagePng_) {
        ++diagnostics_.ownerSuppressions;
        pendingNativeClipboardChange_ = false;
        diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
        return;
    }

    pendingNativeClipboardChange_ = true;
    diagnostics_.nativeChangePending = pendingNativeClipboardChange_;
}

void QtClipboardEndpoint::cleanupMaterializedRemoteFiles()
{
    if (materializedRemoteFilesRoot_.empty())
        return;

    std::error_code ignored;
    std::filesystem::remove_all(materializedRemoteFilesRoot_, ignored);
    materializedRemoteFilesRoot_.clear();
}

} // namespace clipboard
} // namespace qt
} // namespace adapters
} // namespace fusiondesk
