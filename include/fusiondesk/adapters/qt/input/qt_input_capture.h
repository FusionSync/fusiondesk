#ifndef FUSIONDESK_ADAPTERS_QT_INPUT_QT_INPUT_CAPTURE_H
#define FUSIONDESK_ADAPTERS_QT_INPUT_QT_INPUT_CAPTURE_H

#include <queue>

#include "fusiondesk/modules/input/input_interfaces.h"

class QKeyEvent;
class QMouseEvent;

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace input {

class QtInputCapture final : public modules::input::IInputCapture
{
public:
    bool open() override;
    void close() override;
    bool pollMouseEvent(modules::input::MouseInputEvent& event) override;
    bool pollKeyboardEvent(modules::input::KeyboardInputEvent& event) override;

    bool captureMouseEvent(const QMouseEvent& event);
    bool captureKeyEvent(const QKeyEvent& event);
    void enqueueMouseEvent(const modules::input::MouseInputEvent& event);
    void enqueueKeyboardEvent(const modules::input::KeyboardInputEvent& event);

    bool isOpen() const;
    int capturedMouseEvents() const;
    int capturedKeyboardEvents() const;

private:
    std::queue<modules::input::MouseInputEvent> mouseEvents_;
    std::queue<modules::input::KeyboardInputEvent> keyboardEvents_;
    bool opened_ = false;
    int capturedMouseEvents_ = 0;
    int capturedKeyboardEvents_ = 0;
};

} // namespace input
} // namespace qt
} // namespace adapters
} // namespace fusiondesk

#endif // FUSIONDESK_ADAPTERS_QT_INPUT_QT_INPUT_CAPTURE_H
