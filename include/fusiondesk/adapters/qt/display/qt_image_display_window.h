#ifndef FUSIONDESK_ADAPTERS_QT_DISPLAY_QT_IMAGE_DISPLAY_WINDOW_H
#define FUSIONDESK_ADAPTERS_QT_DISPLAY_QT_IMAGE_DISPLAY_WINDOW_H

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace display {

class QtImageDisplayRenderer;

class QtImageDisplayWindow final : public QWidget
{
public:
    explicit QtImageDisplayWindow(QWidget* parent = nullptr);
    ~QtImageDisplayWindow() override;

    void attachRenderer(QtImageDisplayRenderer& renderer);
    void detachRenderer();
    void showFrame(const QImage& image);
    void setStatusText(const QString& text);

    bool hasFrame() const;
    int renderedFrames() const;
    QSize lastFrameSize() const;
    QRect imageContentRectInWindow() const;
    QString statusText() const;

private:
    QLabel* imageLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QtImageDisplayRenderer* renderer_ = nullptr;
    QImage lastFrame_;
    int renderedFrames_ = 0;
};

} // namespace display
} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_DISPLAY_QT_IMAGE_DISPLAY_WINDOW_H
