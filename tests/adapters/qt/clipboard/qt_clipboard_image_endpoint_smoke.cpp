#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QIODevice>
#include <QMimeData>
#include <QString>
#include <QThread>

#include "fusiondesk/adapters/qt/clipboard/qt_clipboard_endpoint.h"

using namespace fusiondesk;

namespace {

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

QByteArray byteArrayFromBytes(const protocol::ByteBuffer& value)
{
    return QByteArray(reinterpret_cast<const char*>(value.data()),
                      static_cast<int>(value.size()));
}

protocol::ByteBuffer bytesFromByteArray(const QByteArray& value)
{
    return protocol::ByteBuffer(
        reinterpret_cast<const std::uint8_t*>(value.constData()),
        reinterpret_cast<const std::uint8_t*>(value.constData()) +
            value.size());
}

QString qStringFromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QImage makeImage()
{
    QImage image(4, 3, QImage::Format_ARGB32);
    image.fill(QColor(20, 30, 40, 255));
    image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    image.setPixelColor(1, 0, QColor(0, 255, 0, 255));
    image.setPixelColor(2, 1, QColor(0, 0, 255, 255));
    image.setPixelColor(3, 2, QColor(255, 255, 0, 255));
    return image;
}

protocol::ByteBuffer pngBytesFromImage(const QImage& image)
{
    QByteArray encoded;
    QBuffer buffer(&encoded);
    assert(buffer.open(QIODevice::WriteOnly));
    assert(image.save(&buffer, "PNG"));
    return bytesFromByteArray(encoded);
}

bool sameImagePixels(const QImage& left, const QImage& right)
{
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return false;

    const QImage normalizedLeft =
        left.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage normalizedRight =
        right.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < normalizedLeft.height(); ++y) {
        for (int x = 0; x < normalizedLeft.width(); ++x) {
            if (normalizedLeft.pixel(x, y) != normalizedRight.pixel(x, y))
                return false;
        }
    }
    return true;
}

bool waitForClipboardImage(const QImage& expected, int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (sameImagePixels(clipboard->image(), expected))
            return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return sameImagePixels(clipboard->image(), expected);
}

bool setClipboardImageAndWait(const QImage& image, int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        clipboard->setImage(image);
        if (waitForClipboardImage(image, 100))
            return true;
        QThread::msleep(20);
    }
    return false;
}

bool waitForClipboardHtml(const std::string& html,
                          const std::string& text,
                          int timeoutMs = 2000)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;

    const QString expectedHtml = qStringFromUtf8(html);
    const QString expectedText = qStringFromUtf8(text);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        const QMimeData* mimeData = clipboard->mimeData();
        if (mimeData != nullptr &&
            mimeData->hasHtml() &&
            mimeData->html() == expectedHtml &&
            mimeData->text() == expectedText) {
            return true;
        }
        QThread::msleep(10);
    }
    return false;
}

modules::clipboard::TransferFormatDescriptor imageDescriptor()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::ImagePngFormat;
    descriptor.nativeFormatName = "image/png";
    descriptor.localFormatToken = 5;
    descriptor.formatId = 31;
    descriptor.estimatedBytes = 1024;
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
    descriptor.formatId = 32;
    descriptor.estimatedBytes = 64;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;
    return descriptor;
}

modules::clipboard::TransferFormatDescriptor textDescriptor()
{
    modules::clipboard::TransferFormatDescriptor descriptor;
    descriptor.canonicalFormat = modules::clipboard::TextPlainUtf8Format;
    descriptor.nativeFormatName = "text/plain;charset=utf-8";
    descriptor.localFormatToken = 1;
    descriptor.formatId = 33;
    descriptor.estimatedBytes = 64;
    descriptor.canInline = true;
    descriptor.canStream = false;
    descriptor.preferredEncoding =
        modules::clipboard::TransferEncodingMode::CanonicalBytes;
    return descriptor;
}

modules::clipboard::TransferSourceBundle imageBundle(const QImage& image)
{
    modules::clipboard::MaterializedTransferEntry entry;
    entry.descriptor = imageDescriptor();
    entry.bytes = pngBytesFromImage(image);

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 130;
    bundle.offerId = 230;
    bundle.ownerEpoch = 330;
    bundle.sequence = 430;
    bundle.origin = modules::clipboard::TransferOrigin::RemoteOffer;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::MaterializedTransferSource>(
            11,
            std::vector<modules::clipboard::MaterializedTransferEntry>{
                std::move(entry)}));
    return bundle;
}

modules::clipboard::TransferSourceBundle invalidImageWithHtmlBundle()
{
    modules::clipboard::MaterializedTransferEntry imageEntry;
    imageEntry.descriptor = imageDescriptor();
    imageEntry.bytes = bytes("not a png");

    modules::clipboard::MaterializedTransferEntry htmlEntry;
    htmlEntry.descriptor = htmlDescriptor();
    htmlEntry.bytes = bytes("<b>fallback html</b>");

    modules::clipboard::MaterializedTransferEntry textEntry;
    textEntry.descriptor = textDescriptor();
    textEntry.bytes = bytes("fallback html");

    modules::clipboard::TransferSourceBundle bundle;
    bundle.bundleId = 131;
    bundle.offerId = 231;
    bundle.ownerEpoch = 331;
    bundle.sequence = 431;
    bundle.origin = modules::clipboard::TransferOrigin::RemoteOffer;
    bundle.side = modules::clipboard::TransferSide::Remote;
    bundle.sources.push_back(
        std::make_shared<modules::clipboard::MaterializedTransferSource>(
            12,
            std::vector<modules::clipboard::MaterializedTransferEntry>{
                std::move(imageEntry),
                std::move(htmlEntry),
                std::move(textEntry)}));
    return bundle;
}

void snapshotBuildsImageBundle()
{
    const QImage image = makeImage();
    assert(setClipboardImageAndWait(image));
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;

    const modules::clipboard::ClipboardSnapshot snapshot =
        endpoint.snapshot();
    assert(snapshot.bundle.offerId != 0);
    assert(snapshot.bundle.presentation.has_value());
    assert(snapshot.bundle.presentation->sourceKind ==
           modules::clipboard::TransferSourceKind::Image);
    assert(snapshot.bundle.presentation->icons.size() == 1);
    assert(snapshot.bundle.presentation->icons.front().format ==
           modules::clipboard::ImagePngFormat);
    assert(snapshot.bundle.presentation->icons.front().width == 4);
    assert(snapshot.bundle.presentation->icons.front().height == 3);
    assert(snapshot.bundle.sources.size() == 1);

    const std::shared_ptr<modules::clipboard::TransferSource> source =
        snapshot.bundle.sources.front();
    modules::clipboard::TransferReadRequest request;
    request.sourceId = source->id();
    request.canonicalFormat = modules::clipboard::ImagePngFormat;
    request.acceptedMaxBytes = 4096;
    const modules::clipboard::TransferReadResult read = source->read(request);
    assert(read.ok());
    assert(read.canonicalFormat == modules::clipboard::ImagePngFormat);

    QImage decoded;
    assert(decoded.loadFromData(byteArrayFromBytes(read.bytes), "PNG"));
    assert(sameImagePixels(decoded, image));
}

void publishBundleWritesClipboardImage()
{
    const QImage image = makeImage();
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    endpoint.markClipboardChangeConsumed();

    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{imageBundle(image)});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardImage(image));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 230);
    assert(endpoint.diagnostics().publishes == 1);
}

void publishFallsBackToHtmlWhenImageDecodeFails()
{
    adapters::qt::clipboard::QtClipboardEndpoint endpoint;
    endpoint.markClipboardChangeConsumed();

    const protocol::ResponseStatus status = endpoint.publishBundle(
        modules::clipboard::ClipboardPublishRequest{
            invalidImageWithHtmlBundle()});

    assert(status == protocol::ResponseStatus::Ok);
    assert(waitForClipboardHtml("<b>fallback html</b>", "fallback html"));
    assert(!endpoint.hasPendingClipboardChange());
    assert(endpoint.diagnostics().publishedOfferId == 231);
    assert(endpoint.diagnostics().publishes == 1);
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    if (!setClipboardImageAndWait(makeImage())) {
        std::fputs("Skipping Qt clipboard image endpoint smoke: native "
                   "QClipboard image roundtrip is unavailable in this "
                   "session.\n",
                   stderr);
        return 0;
    }

    snapshotBuildsImageBundle();
    publishBundleWritesClipboardImage();
    publishFallsBackToHtmlWhenImageDecodeFails();
    return 0;
}
