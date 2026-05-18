#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QList>
#include <QMimeData>
#include <QString>
#include <QThread>
#include <QTemporaryDir>
#include <QUrl>

#include "fusiondesk/adapters/qt/clipboard/qt_clipboard_endpoint.h"

using namespace fusiondesk;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

std::string stringFromBytes(const protocol::ByteBuffer& value)
{
    return std::string(value.begin(), value.end());
}

QString qStringFromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

bool waitForClipboardText(const std::string& expected, int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    const QString expectedText = qStringFromUtf8(expected);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (clipboard->text() == expectedText)
            return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return clipboard->text() == expectedText;
}

bool waitForClipboardHtml(const std::string& expectedHtml,
                          const std::string& expectedText,
                          int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    const QString html = qStringFromUtf8(expectedHtml);
    const QString text = qStringFromUtf8(expectedText);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const QMimeData* mimeData = clipboard->mimeData();
        if (mimeData != nullptr &&
            mimeData->hasHtml() &&
            mimeData->html() == html &&
            (expectedText.empty() || mimeData->text() == text)) {
            return true;
        }
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    const QMimeData* mimeData = clipboard->mimeData();
    return mimeData != nullptr &&
           mimeData->hasHtml() &&
           mimeData->html() == html &&
           (expectedText.empty() || mimeData->text() == text);
}

bool waitForClipboardRtf(const std::string& expectedRtf,
                         const std::string& expectedText,
                         int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    const QByteArray rtf(expectedRtf.data(),
                         static_cast<int>(expectedRtf.size()));
    const QString text = qStringFromUtf8(expectedText);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const QMimeData* mimeData = clipboard->mimeData();
        if (mimeData != nullptr &&
            mimeData->hasFormat("text/rtf") &&
            mimeData->data("text/rtf") == rtf &&
            (expectedText.empty() || mimeData->text() == text)) {
            return true;
        }
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    const QMimeData* mimeData = clipboard->mimeData();
    return mimeData != nullptr &&
           mimeData->hasFormat("text/rtf") &&
           mimeData->data("text/rtf") == rtf &&
           (expectedText.empty() || mimeData->text() == text);
}

bool setClipboardTextAndWait(const std::string& text, int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    const QString expectedText = qStringFromUtf8(text);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        clipboard->setText(expectedText);
        if (waitForClipboardText(text, 100))
            return true;
        QThread::msleep(20);
    }
    return false;
}

bool setClipboardHtmlAndWait(const std::string& html,
                             const std::string& text,
                             int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        auto* mimeData = new QMimeData;
        mimeData->setHtml(qStringFromUtf8(html));
        mimeData->setText(qStringFromUtf8(text));
        clipboard->setMimeData(mimeData);
        if (waitForClipboardHtml(html, text, 100))
            return true;
        QThread::msleep(20);
    }
    return false;
}

bool setClipboardRtfAndWait(const std::string& rtf,
                            const std::string& text,
                            int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    const QByteArray rtfBytes(rtf.data(), static_cast<int>(rtf.size()));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        auto* mimeData = new QMimeData;
        mimeData->setData("text/rtf", rtfBytes);
        mimeData->setText(qStringFromUtf8(text));
        clipboard->setMimeData(mimeData);
        if (waitForClipboardRtf(rtf, text, 100))
            return true;
        QThread::msleep(20);
    }
    return false;
}

bool waitForClipboardUrls(int count, int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const QMimeData* mimeData = clipboard->mimeData();
        if (mimeData != nullptr &&
            mimeData->hasUrls() &&
            mimeData->urls().size() == count) {
            return true;
        }
        QThread::msleep(10);
    }

    const QMimeData* mimeData = clipboard->mimeData();
    return mimeData != nullptr &&
           mimeData->hasUrls() &&
           mimeData->urls().size() == count;
}

bool setClipboardUrlsAndWait(const QList<QUrl>& urls, int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        auto* mimeData = new QMimeData;
        mimeData->setUrls(urls);
        clipboard->setMimeData(mimeData);
        if (waitForClipboardUrls(urls.size(), 100))
            return true;
        QThread::msleep(20);
    }
    return false;
}

bool waitForClipboardEmpty(int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (clipboard->text().isEmpty())
            return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return clipboard->text().isEmpty();
}

void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

modules::clipboard::TransferFormatDescriptor textDescriptor()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    descriptor.nativeFormatName = "text/plain;charset=utf-8";
    descriptor.localFormatToken = 1;
    descriptor.formatId = 11;
    descriptor.estimatedBytes = 128;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;
    return descriptor;
}

modules::clipboard::TransferFormatDescriptor htmlDescriptor()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::TextHtmlFormat;
    descriptor.nativeFormatName = "text/html";
    descriptor.localFormatToken = 3;
    descriptor.formatId = 12;
    descriptor.estimatedBytes = 128;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;
    return descriptor;
}

modules::clipboard::TransferFormatDescriptor rtfDescriptor()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::TextRtfFormat;
    descriptor.nativeFormatName = "text/rtf";
    descriptor.localFormatToken = 4;
    descriptor.formatId = 13;
    descriptor.estimatedBytes = 128;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;
    return descriptor;
}

modules::clipboard::TransferSourceBundle textBundle(const std::string& text)
{
    modules::clipboard::MaterializedTransferEntry entry;
    entry.descriptor = textDescriptor();
    entry.bytes = bytes(text);

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 100;
    bundle.offerId = 200;
    bundle.ownerEpoch = 300;
    bundle.sequence = 400;
    bundle.origin = modules::clipboard::TransferOrigin::Clipboard;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::MaterializedTransferSource>(
            7,
            std::vector<modules::clipboard::MaterializedTransferEntry>{
                std::move(entry)}));
    return bundle;
}

modules::clipboard::TransferSourceBundle rtfBundle(const std::string& rtf,
                                                   const std::string& text)
{
    modules::clipboard::MaterializedTransferEntry rtfEntry;
    rtfEntry.descriptor = rtfDescriptor();
    rtfEntry.bytes = bytes(rtf);

    modules::clipboard::MaterializedTransferEntry textEntry;
    textEntry.descriptor = textDescriptor();
    textEntry.bytes = bytes(text);

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 121;
    bundle.offerId = 221;
    bundle.ownerEpoch = 321;
    bundle.sequence = 421;
    bundle.origin = modules::clipboard::TransferOrigin::Clipboard;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::MaterializedTransferSource>(
            9,
            std::vector<modules::clipboard::MaterializedTransferEntry>{
                std::move(rtfEntry),
                std::move(textEntry)}));
    return bundle;
}

modules::clipboard::TransferSourceBundle htmlBundle(const std::string& html,
                                                    const std::string& text)
{
    modules::clipboard::MaterializedTransferEntry htmlEntry;
    htmlEntry.descriptor = htmlDescriptor();
    htmlEntry.bytes = bytes(html);

    modules::clipboard::MaterializedTransferEntry textEntry;
    textEntry.descriptor = textDescriptor();
    textEntry.bytes = bytes(text);

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 120;
    bundle.offerId = 220;
    bundle.ownerEpoch = 320;
    bundle.sequence = 420;
    bundle.origin = modules::clipboard::TransferOrigin::Clipboard;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::MaterializedTransferSource>(
            8,
            std::vector<modules::clipboard::MaterializedTransferEntry>{
                std::move(htmlEntry),
                std::move(textEntry)}));
    return bundle;
}

modules::clipboard::TransferSourceBundle htmlRtfBundle(const std::string& html,
                                                       const std::string& rtf,
                                                       const std::string& text)
{
    modules::clipboard::MaterializedTransferEntry htmlEntry;
    htmlEntry.descriptor = htmlDescriptor();
    htmlEntry.bytes = bytes(html);

    modules::clipboard::MaterializedTransferEntry rtfEntry;
    rtfEntry.descriptor = rtfDescriptor();
    rtfEntry.bytes = bytes(rtf);

    modules::clipboard::MaterializedTransferEntry textEntry;
    textEntry.descriptor = textDescriptor();
    textEntry.bytes = bytes(text);

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 122;
    bundle.offerId = 223;
    bundle.ownerEpoch = 322;
    bundle.sequence = 422;
    bundle.origin = modules::clipboard::TransferOrigin::Clipboard;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::MaterializedTransferSource>(
            10,
            std::vector<modules::clipboard::MaterializedTransferEntry>{
                std::move(htmlEntry),
                std::move(rtfEntry),
                std::move(textEntry)}));
    return bundle;
}

class FakeRemoteReader final
    : public modules::clipboard::IClipboardRemoteReader
{
public:
    modules::clipboard::TransferReadResult readRemoteFormat(
        const modules::clipboard::TransferReadRequest& request,
        std::uint32_t timeoutMs) override
    {
        lastRequest = request;
        lastTimeoutMs = timeoutMs;
        ++reads;

        modules::clipboard::TransferReadResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
        result.encoding =
            modules::clipboard::TransferEncodingMode::CanonicalBytes;
        result.bytes = bytes("remote qt text");
        return result;
    }

    modules::clipboard::TransferReadRequest lastRequest;
    std::uint32_t lastTimeoutMs = 0;
    int reads = 0;
};

class FakeRemoteFileReader final
    : public modules::clipboard::IClipboardRemoteReader,
      public modules::clipboard::IClipboardRemoteFileReader,
      public modules::clipboard::IClipboardRemoteObjectLocker
{
public:
    FakeRemoteFileReader()
    {
        modules::clipboard::TransferFileDescriptor directory;
        directory.objectId = 1;
        directory.displayName = "payload_dir";
        directory.relativePath = "payload_dir";
        directory.directory = true;
        fileList.files.push_back(directory);

        modules::clipboard::TransferFileDescriptor alpha;
        alpha.objectId = 2;
        alpha.displayName = "alpha.txt";
        alpha.relativePath = "payload_dir/alpha.txt";
        alpha.sizeBytes = 13;
        fileList.files.push_back(alpha);

        modules::clipboard::TransferFileDescriptor loose;
        loose.objectId = 3;
        loose.displayName = "loose.txt";
        loose.relativePath = "loose.txt";
        loose.sizeBytes = 13;
        fileList.files.push_back(loose);
    }

    modules::clipboard::TransferReadResult readRemoteFormat(
        const modules::clipboard::TransferReadRequest& request,
        std::uint32_t timeoutMs) override
    {
        lastReadRequest = request;
        lastReadTimeoutMs = timeoutMs;
        ++formatReads;

        modules::clipboard::TransferReadResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.canonicalFormat = modules::clipboard::FdclFileListFormat;
        result.encoding =
            modules::clipboard::TransferEncodingMode::CanonicalBytes;
        result.bytes = modules::clipboard::encodeTransferFileList(fileList);
        return result;
    }

    modules::clipboard::TransferFileRangeResult readRemoteFileRange(
        const modules::clipboard::TransferFileRangeRequest& request,
        std::uint32_t timeoutMs) override
    {
        lastRangeRequest = request;
        lastRangeTimeoutMs = timeoutMs;
        ++rangeReads;

        modules::clipboard::TransferFileRangeResult result;
        if (request.fileIndex >= fileList.files.size()) {
            result.status = protocol::ResponseStatus::NotFound;
            return result;
        }
        const modules::clipboard::TransferFileDescriptor& descriptor =
            fileList.files[request.fileIndex];
        if (descriptor.objectId != request.objectId) {
            result.status = protocol::ResponseStatus::NotFound;
            return result;
        }

        const std::string content =
            descriptor.relativePath == "payload_dir/alpha.txt"
                ? "qt alpha file"
                : "qt loose file";
        if (request.offset > content.size()) {
            result.status = protocol::ResponseStatus::InvalidArgument;
            return result;
        }

        const std::size_t offset = static_cast<std::size_t>(request.offset);
        const std::size_t count = static_cast<std::size_t>(
            std::min<std::uint64_t>(
                request.requestedBytes,
                static_cast<std::uint64_t>(content.size() - offset)));
        result.status = protocol::ResponseStatus::Ok;
        result.bytes = bytes(content.substr(offset, count));
        result.endOfFile = offset + count >= content.size();
        return result;
    }

    modules::clipboard::TransferObjectLockResult lockRemoteObject(
        const modules::clipboard::TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) override
    {
        lastLockRequest = request;
        lastLockTimeoutMs = timeoutMs;
        ++locks;

        modules::clipboard::TransferObjectLockResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.lockId = 7000 + locks;
        result.leaseUsec = 1000000;
        return result;
    }

    modules::clipboard::TransferObjectLockResult unlockRemoteObject(
        const modules::clipboard::TransferObjectLockRequest& request,
        std::uint32_t timeoutMs) override
    {
        lastUnlockRequest = request;
        lastUnlockTimeoutMs = timeoutMs;
        ++unlocks;

        modules::clipboard::TransferObjectLockResult result;
        result.status = protocol::ResponseStatus::Ok;
        result.lockId = request.lockId;
        result.leaseUsec = request.leaseUsec;
        return result;
    }

    modules::clipboard::TransferFileList fileList;
    modules::clipboard::TransferReadRequest lastReadRequest;
    modules::clipboard::TransferFileRangeRequest lastRangeRequest;
    modules::clipboard::TransferObjectLockRequest lastLockRequest;
    modules::clipboard::TransferObjectLockRequest lastUnlockRequest;
    std::uint32_t lastReadTimeoutMs = 0;
    std::uint32_t lastRangeTimeoutMs = 0;
    std::uint32_t lastLockTimeoutMs = 0;
    std::uint32_t lastUnlockTimeoutMs = 0;
    int formatReads = 0;
    int rangeReads = 0;
    int locks = 0;
    int unlocks = 0;
};

modules::clipboard::TransferSourceBundle remoteTextBundle()
{
    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 101;
    bundle.offerId = 201;
    bundle.ownerEpoch = 301;
    bundle.sequence = 401;
    bundle.origin = modules::clipboard::TransferOrigin::RemoteOffer;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::RemoteFdclTransferSource>(
            17,
            std::vector<modules::clipboard::TransferFormatDescriptor>{
                textDescriptor()}));
    return bundle;
}

modules::clipboard::TransferSourceBundle remoteFileBundle()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::FdclFileListFormat;
    descriptor.nativeFormatName = "text/uri-list";
    descriptor.localFormatToken = 2;
    descriptor.formatId = 22;
    descriptor.itemIndex = 0;
    descriptor.estimatedBytes = 512;
    descriptor.canInline = true;
    descriptor.canStream = true;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 111;
    bundle.offerId = 222;
    bundle.ownerEpoch = 333;
    bundle.sequence = 444;
    bundle.origin = modules::clipboard::TransferOrigin::RemoteOffer;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::RemoteFdclTransferSource>(
            27,
            std::vector<modules::clipboard::TransferFormatDescriptor>{
                descriptor}));
    return bundle;
}

void snapshotBuildsTextBundle()
{
    assert(setClipboardTextAndWait("qt local text"));
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;

    const modules::clipboard::ClipboardSnapshot snapshot =
        endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);
    assert(snapshot.bundle.origin ==
           modules::clipboard::TransferOrigin::Clipboard);

    modules::clipboard::TransferReadRequest request;
    request.sourceId = snapshot.bundle.sources.front()->id();
    request.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    const modules::clipboard::TransferReadResult read =
        snapshot.bundle.sources.front()->read(request);
    assert(read.ok());
    assert(stringFromBytes(read.bytes) == "qt local text");
}

void snapshotBuildsHtmlBundle()
{
    const std::string html = "<p><b>qt local html</b></p>";
    const std::string text = "qt local html";
    assert(setClipboardHtmlAndWait(html, text));
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;

    const modules::clipboard::ClipboardSnapshot snapshot =
        endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);
    assert(snapshot.bundle.presentation.has_value());
    assert(snapshot.bundle.presentation->sourceKind ==
           modules::clipboard::TransferSourceKind::Text);

    const std::shared_ptr<modules::clipboard::TransferSource> source =
        snapshot.bundle.sources.front();
    const std::vector<modules::clipboard::TransferFormatDescriptor> formats =
        source->formats();
    const auto htmlFormat = std::find_if(
        formats.begin(),
        formats.end(),
        [](const modules::clipboard::TransferFormatDescriptor& descriptor) {
            return descriptor.canonicalFormat ==
                   modules::clipboard::TextHtmlFormat;
        });
    const auto textFormat = std::find_if(
        formats.begin(),
        formats.end(),
        [](const modules::clipboard::TransferFormatDescriptor& descriptor) {
            return descriptor.canonicalFormat ==
                   modules::clipboard::TextPlainUtf8Format;
        });
    assert(htmlFormat != formats.end());
    assert(textFormat != formats.end());

    modules::clipboard::TransferReadRequest htmlRequest;
    htmlRequest.sourceId = source->id();
    htmlRequest.canonicalFormat = modules::clipboard::TextHtmlFormat;
    const modules::clipboard::TransferReadResult htmlRead =
        source->read(htmlRequest);
    assert(htmlRead.ok());
    assert(stringFromBytes(htmlRead.bytes) == html);

    modules::clipboard::TransferReadRequest textRequest;
    textRequest.sourceId = source->id();
    textRequest.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    const modules::clipboard::TransferReadResult textRead =
        source->read(textRequest);
    assert(textRead.ok());
    assert(stringFromBytes(textRead.bytes) == text);
}

void snapshotBuildsRtfBundle()
{
    const std::string rtf = "{\\rtf1\\ansi qt local rtf}";
    const std::string text = "qt local rtf";
    assert(setClipboardRtfAndWait(rtf, text));
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;

    const modules::clipboard::ClipboardSnapshot snapshot =
        endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.sources.size() == 1);
    assert(snapshot.bundle.presentation.has_value());
    assert(snapshot.bundle.presentation->sourceKind ==
           modules::clipboard::TransferSourceKind::Text);

    const std::shared_ptr<modules::clipboard::TransferSource> source =
        snapshot.bundle.sources.front();
    const std::vector<modules::clipboard::TransferFormatDescriptor> formats =
        source->formats();
    const auto rtfFormat = std::find_if(
        formats.begin(),
        formats.end(),
        [](const modules::clipboard::TransferFormatDescriptor& descriptor) {
            return descriptor.canonicalFormat ==
                   modules::clipboard::TextRtfFormat;
        });
    const auto textFormat = std::find_if(
        formats.begin(),
        formats.end(),
        [](const modules::clipboard::TransferFormatDescriptor& descriptor) {
            return descriptor.canonicalFormat ==
                   modules::clipboard::TextPlainUtf8Format;
        });
    assert(rtfFormat != formats.end());
    assert(textFormat != formats.end());

    modules::clipboard::TransferReadRequest rtfRequest;
    rtfRequest.sourceId = source->id();
    rtfRequest.canonicalFormat = modules::clipboard::TextRtfFormat;
    const modules::clipboard::TransferReadResult rtfRead =
        source->read(rtfRequest);
    assert(rtfRead.ok());
    assert(stringFromBytes(rtfRead.bytes) == rtf);

    modules::clipboard::TransferReadRequest textRequest;
    textRequest.sourceId = source->id();
    textRequest.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    const modules::clipboard::TransferReadResult textRead =
        source->read(textRequest);
    assert(textRead.ok());
    assert(stringFromBytes(textRead.bytes) == text);
}

std::size_t findFileIndex(
    const modules::clipboard::TransferFileList& fileList,
    const std::string& relativePath)
{
    for (std::size_t i = 0; i < fileList.files.size(); ++i) {
        if (fileList.files[i].relativePath == relativePath)
            return i;
    }
    return fileList.files.size();
}

void snapshotBuildsFileListBundleFromLocalUrls()
{
    QTemporaryDir root;
    assert(root.isValid());

    const std::filesystem::path base(root.path().toStdString());
    const std::filesystem::path payloadDir = base / "payload_dir";
    const std::filesystem::path alpha = payloadDir / "alpha.txt";
    const std::filesystem::path beta = payloadDir / "nested" / "beta.txt";
    const std::filesystem::path loose = base / "loose.txt";
    writeFile(alpha, "qt alpha file");
    writeFile(beta, "qt beta file");
    writeFile(loose, "qt loose file");

    QList<QUrl> urls;
    urls.push_back(QUrl::fromLocalFile(QString::fromStdString(payloadDir.string())));
    urls.push_back(QUrl::fromLocalFile(QString::fromStdString(loose.string())));
    assert(setClipboardUrlsAndWait(urls));

    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    const modules::clipboard::ClipboardSnapshot snapshot =
        endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.presentation.has_value());
    assert(snapshot.bundle.presentation->sourceKind ==
           modules::clipboard::TransferSourceKind::FileList);
    assert(snapshot.bundle.sources.size() == 1);
    assert(endpoint.diagnostics().fileListSnapshots == 1);

    const std::shared_ptr<modules::clipboard::TransferSource> source =
        snapshot.bundle.sources.front();
    const std::vector<modules::clipboard::TransferFormatDescriptor> formats =
        source->formats();
    assert(formats.size() == 1);
    assert(formats.front().canonicalFormat ==
           modules::clipboard::FdclFileListFormat);
    assert(formats.front().canStream);

    modules::clipboard::TransferReadRequest listRequest;
    listRequest.bundleId = snapshot.bundle.bundleId;
    listRequest.offerId = snapshot.bundle.offerId;
    listRequest.ownerEpoch = snapshot.bundle.ownerEpoch;
    listRequest.sourceId = source->id();
    listRequest.formatId = formats.front().formatId;
    listRequest.canonicalFormat = modules::clipboard::FdclFileListFormat;
    listRequest.acceptedMaxBytes = 4096;
    listRequest.requestedEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;
    const modules::clipboard::TransferReadResult read =
        source->read(listRequest);
    assert(read.ok());

    const modules::clipboard::TransferFileListDecodeResult decoded =
        modules::clipboard::decodeTransferFileList(read.bytes);
    assert(decoded.ok);
    assert(decoded.fileList.files.size() == 5);
    const std::size_t alphaIndex =
        findFileIndex(decoded.fileList, "payload_dir/alpha.txt");
    const std::size_t nestedIndex =
        findFileIndex(decoded.fileList, "payload_dir/nested");
    const std::size_t betaIndex =
        findFileIndex(decoded.fileList, "payload_dir/nested/beta.txt");
    const std::size_t looseIndex =
        findFileIndex(decoded.fileList, "loose.txt");
    assert(alphaIndex != decoded.fileList.files.size());
    assert(nestedIndex != decoded.fileList.files.size());
    assert(betaIndex != decoded.fileList.files.size());
    assert(looseIndex != decoded.fileList.files.size());
    assert(decoded.fileList.files[nestedIndex].directory);
    assert(decoded.fileList.files[alphaIndex].sizeBytes == 13);
    assert(decoded.fileList.files[betaIndex].sizeBytes == 12);
    assert(decoded.fileList.files[looseIndex].sizeBytes == 13);

    const auto fileProvider =
        std::dynamic_pointer_cast<
            modules::clipboard::ITransferFileContentProvider>(source);
    assert(fileProvider != nullptr);

    modules::clipboard::TransferFileRangeRequest rangeRequest;
    rangeRequest.bundleId = snapshot.bundle.bundleId;
    rangeRequest.offerId = snapshot.bundle.offerId;
    rangeRequest.ownerEpoch = snapshot.bundle.ownerEpoch;
    rangeRequest.sourceId = source->id();
    rangeRequest.objectId = decoded.fileList.files[alphaIndex].objectId;
    rangeRequest.fileIndex = static_cast<std::uint32_t>(alphaIndex);
    rangeRequest.offset = 3;
    rangeRequest.requestedBytes = 5;
    const modules::clipboard::TransferFileRangeResult range =
        fileProvider->readFileRange(rangeRequest);
    assert(range.ok());
    assert(stringFromBytes(range.bytes) == "alpha");
    assert(!range.endOfFile);
}

void publishBundleWritesClipboardText()
{
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    endpoint.markClipboardChangeConsumed();

    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{textBundle("published qt")});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardText("published qt"));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 200);
    assert(endpoint.diagnostics().publishes == 1);
}

void publishBundleWritesClipboardHtml()
{
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    endpoint.markClipboardChangeConsumed();

    const std::string html = "<p><i>published qt html</i></p>";
    const std::string text = "published qt html";
    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{htmlBundle(html, text)});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardHtml(html, text));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 220);
    assert(endpoint.diagnostics().publishes == 1);
}

void publishBundleWritesClipboardRtf()
{
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    endpoint.markClipboardChangeConsumed();

    const std::string rtf = "{\\rtf1\\ansi published qt rtf}";
    const std::string text = "published qt rtf";
    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{rtfBundle(rtf, text)});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardRtf(rtf, text));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 221);
    assert(endpoint.diagnostics().publishes == 1);
}

void publishFallsBackToRtfWhenHtmlReadIsTooLarge()
{
    adapters::qt::clipboard::QtClipboardEndpointOptions options;
    options.maxInlineBytes = 16;
    adapters::qt::clipboard::QtClipboardEndpoint endpoint(options);
    endpoint.markClipboardChangeConsumed();

    const std::string rtf = "{\\rtf1 ok}";
    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{
            htmlRtfBundle("<p>too long html payload</p>", rtf, "fallback")});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardRtf(rtf, "fallback"));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 223);
    assert(endpoint.diagnostics().publishes == 1);
}

void publishFallsBackToTextWhenHtmlReadIsTooLarge()
{
    adapters::qt::clipboard::QtClipboardEndpointOptions options;
    options.maxInlineBytes = 8;
    adapters::qt::clipboard::QtClipboardEndpoint endpoint(options);
    endpoint.markClipboardChangeConsumed();

    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{
            htmlBundle("<p>too long html payload</p>", "fallback")});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardText("fallback"));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 220);
    assert(endpoint.diagnostics().publishes == 1);
}

void changeMonitorSuppressesOwnPublishAndTracksExternalChange()
{
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    endpoint.markClipboardChangeConsumed();
    assert(!endpoint.hasPendingClipboardChange());

    assert(setClipboardTextAndWait("external qt"));
    assert(endpoint.hasPendingClipboardChange());
    endpoint.markClipboardChangeConsumed();
    assert(!endpoint.hasPendingClipboardChange());

    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{textBundle("owned qt")});
    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardText("owned qt"));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 200);
}

void publishRemoteOfferUsesRemoteReader()
{
    auto reader = std::make_shared<FakeRemoteReader>();
    adapters::qt::clipboard::QtClipboardEndpoint endpoint({}, reader);

    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{remoteTextBundle()});

    assert(status == protocol::ResponseStatus::Ok);
    assert(reader->reads == 1);
    assert(reader->lastRequest.bundleId == 101);
    assert(reader->lastRequest.offerId == 201);
    assert(reader->lastRequest.sourceId == 17);
    assert(waitForClipboardText("remote qt text"));
}

void publishRemoteFileOfferMaterializesLocalUrls()
{
    auto reader = std::make_shared<FakeRemoteFileReader>();
    adapters::qt::clipboard::QtClipboardEndpoint endpoint({}, reader);

    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{remoteFileBundle()});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardUrls(2));
    assert(reader->formatReads == 1);
    assert(reader->rangeReads >= 2);
    assert(reader->locks == 2);
    assert(reader->unlocks == 2);

    const QMimeData* mimeData = QGuiApplication::clipboard()->mimeData();
    assert(mimeData != nullptr);
    const QList<QUrl> urls = mimeData->urls();
    assert(urls.size() == 2);

    bool sawPayloadDir = false;
    bool sawLoose = false;
    for (const QUrl& url : urls) {
        assert(url.isLocalFile());
        const std::filesystem::path path(url.toLocalFile().toStdString());
        if (path.filename() == "payload_dir") {
            sawPayloadDir = true;
            assert(readFile(path / "alpha.txt") == "qt alpha file");
        } else if (path.filename() == "loose.txt") {
            sawLoose = true;
            assert(readFile(path) == "qt loose file");
        }
    }
    assert(sawPayloadDir);
    assert(sawLoose);

    const adapters::qt::clipboard::QtClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.publishedOfferId == 222);
    assert(diagnostics.remoteFilePublishes == 1);
    assert(diagnostics.remoteFilesMaterialized == 2);
    assert(diagnostics.remoteDirectoriesMaterialized == 1);
    assert(diagnostics.remoteFileBytesMaterialized == 26);
    assert(!endpoint.hasPendingClipboardChange());
}

void clearPublishedBundleClearsClipboard()
{
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    assert(endpoint.publishBundle(
               modules::clipboard::ClipboardPublishRequest{textBundle("clear me")}) ==
           protocol::ResponseStatus::Ok);
    assert(endpoint.clearPublishedBundle(200) ==
           protocol::ResponseStatus::Ok);
    assert(waitForClipboardEmpty());
    assert(endpoint.diagnostics().clears == 1);
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    if (!setClipboardTextAndWait("fusiondesk qt clipboard availability")) {
        std::fputs("Skipping Qt clipboard endpoint smoke: native QClipboard "
                   "text roundtrip is unavailable in this Windows session.\n",
                   stderr);
        return 0;
    }

    snapshotBuildsTextBundle();
    snapshotBuildsHtmlBundle();
    snapshotBuildsRtfBundle();
    snapshotBuildsFileListBundleFromLocalUrls();
    publishBundleWritesClipboardText();
    publishBundleWritesClipboardHtml();
    publishBundleWritesClipboardRtf();
    publishFallsBackToRtfWhenHtmlReadIsTooLarge();
    publishFallsBackToTextWhenHtmlReadIsTooLarge();
    changeMonitorSuppressesOwnPublishAndTracksExternalChange();
    publishRemoteOfferUsesRemoteReader();
    publishRemoteFileOfferMaterializesLocalUrls();
    clearPublishedBundleClearsClipboard();
    return 0;
}
