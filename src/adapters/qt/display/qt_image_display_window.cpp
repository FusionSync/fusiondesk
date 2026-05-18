#include "fusiondesk/adapters/qt/display/qt_image_display_window.h"

#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"

#include <QLabel>
#include <QMetaObject>
#include <QPalette>
#include <QPixmap>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace display {

QtImageDisplayWindow::QtImageDisplayWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("FusionDesk Display"));
    resize(960, 600);

    imageLabel_ = new QLabel(this);
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setMinimumSize(320, 200);
    imageLabel_->setAutoFillBackground(true);
    QPalette palette = imageLabel_->palette();
    palette.setColor(QPalette::Window, Qt::black);
    imageLabel_->setPalette(palette);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidget(imageLabel_);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);

    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusLabel_->setText(QStringLiteral("display.initializing"));
    statusLabel_->setContentsMargins(8, 4, 8, 4);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(scrollArea_);
    layout->addWidget(statusLabel_);
    setLayout(layout);
}

QtImageDisplayWindow::~QtImageDisplayWindow()
{
    detachRenderer();
}

void QtImageDisplayWindow::attachRenderer(QtImageDisplayRenderer& renderer)
{
    detachRenderer();
    renderer_ = &renderer;
    renderer_->setFrameRenderedHandler([this](const QImage& image) {
        const QImage owned = image.copy();
        if (thread() == QThread::currentThread()) {
            showFrame(owned);
            return;
        }

        QMetaObject::invokeMethod(this,
                                  [this, owned]() {
                                      showFrame(owned);
                                  },
                                  Qt::QueuedConnection);
    });
}

void QtImageDisplayWindow::detachRenderer()
{
    if (renderer_ != nullptr)
        renderer_->setFrameRenderedHandler({});
    renderer_ = nullptr;
}

void QtImageDisplayWindow::showFrame(const QImage& image)
{
    if (image.isNull())
        return;

    lastFrame_ = image.copy();
    imageLabel_->setPixmap(QPixmap::fromImage(lastFrame_));
    imageLabel_->setMinimumSize(lastFrame_.size());
    ++renderedFrames_;
}

void QtImageDisplayWindow::setStatusText(const QString& text)
{
    statusLabel_->setText(text);
}

bool QtImageDisplayWindow::hasFrame() const
{
    return !lastFrame_.isNull();
}

int QtImageDisplayWindow::renderedFrames() const
{
    return renderedFrames_;
}

QSize QtImageDisplayWindow::lastFrameSize() const
{
    return lastFrame_.size();
}

QRect QtImageDisplayWindow::imageContentRectInWindow() const
{
    if (lastFrame_.isNull() || imageLabel_ == nullptr)
        return {};

    const QSize imageSize = lastFrame_.size();
    QRect content(QPoint(0, 0), imageSize);
    const QRect labelRect = imageLabel_->rect();
    if (labelRect.width() > imageSize.width())
        content.moveLeft((labelRect.width() - imageSize.width()) / 2);
    if (labelRect.height() > imageSize.height())
        content.moveTop((labelRect.height() - imageSize.height()) / 2);

    return QRect(imageLabel_->mapTo(this, content.topLeft()),
                 content.size());
}

QString QtImageDisplayWindow::statusText() const
{
    return statusLabel_->text();
}

} // namespace display
} // namespace qt
} // namespace adapters
} // namespace fusiondesk
