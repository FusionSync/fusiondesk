#include "fusiondesk/adapters/qt/input/qt_input_capture.h"

#include <chrono>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QtGlobal>

namespace fusiondesk {
namespace adapters {
namespace qt {
namespace input {

namespace {

std::uint64_t monotonicNowUsec()
{
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
}

modules::input::InputModifierState mapModifiers(Qt::KeyboardModifiers modifiers)
{
    modules::input::InputModifierState state;
    state.shift = modifiers.testFlag(Qt::ShiftModifier);
    state.control = modifiers.testFlag(Qt::ControlModifier);
    state.alt = modifiers.testFlag(Qt::AltModifier);
    state.meta = modifiers.testFlag(Qt::MetaModifier);
    return state;
}

modules::input::MouseButton mapButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return modules::input::MouseButton::Left;
    case Qt::RightButton:
        return modules::input::MouseButton::Right;
    case Qt::MiddleButton:
        return modules::input::MouseButton::Middle;
    case Qt::XButton1:
        return modules::input::MouseButton::X1;
    case Qt::XButton2:
        return modules::input::MouseButton::X2;
    default:
        return modules::input::MouseButton::None;
    }
}

modules::input::MouseAction mapMouseAction(QEvent::Type type)
{
    if (type == QEvent::MouseButtonPress)
        return modules::input::MouseAction::ButtonDown;
    if (type == QEvent::MouseButtonRelease)
        return modules::input::MouseAction::ButtonUp;
    return modules::input::MouseAction::Move;
}

modules::input::KeyboardAction mapKeyboardAction(QEvent::Type type)
{
    if (type == QEvent::KeyRelease)
        return modules::input::KeyboardAction::KeyUp;
    return modules::input::KeyboardAction::KeyDown;
}

std::uint32_t firstCodepoint(const QString& text)
{
    if (text.isEmpty())
        return 0;
    return static_cast<std::uint32_t>(text.at(0).unicode());
}

} // namespace

bool QtInputCapture::open()
{
    opened_ = true;
    return true;
}

void QtInputCapture::close()
{
    opened_ = false;
}

bool QtInputCapture::pollMouseEvent(modules::input::MouseInputEvent& event)
{
    if (mouseEvents_.empty())
        return false;

    event = mouseEvents_.front();
    mouseEvents_.pop();
    return true;
}

bool QtInputCapture::pollKeyboardEvent(modules::input::KeyboardInputEvent& event)
{
    if (keyboardEvents_.empty())
        return false;

    event = keyboardEvents_.front();
    keyboardEvents_.pop();
    return true;
}

bool QtInputCapture::captureMouseEvent(const QMouseEvent& event)
{
    if (!opened_)
        open();

    modules::input::MouseInputEvent captured;
    captured.sequence = static_cast<std::uint64_t>(capturedMouseEvents_ + 1);
    captured.monotonicTimestampUsec = monotonicNowUsec();
    captured.action = mapMouseAction(event.type());
    captured.button = mapButton(event.button());
    captured.coordinateSpace = modules::input::InputCoordinateSpace::Pixel;
    captured.modifiers = mapModifiers(event.modifiers());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    captured.x = static_cast<std::int32_t>(event.position().x());
    captured.y = static_cast<std::int32_t>(event.position().y());
#else
    captured.x = static_cast<std::int32_t>(event.localPos().x());
    captured.y = static_cast<std::int32_t>(event.localPos().y());
#endif
    enqueueMouseEvent(captured);
    return true;
}

bool QtInputCapture::captureKeyEvent(const QKeyEvent& event)
{
    if (!opened_)
        open();

    modules::input::KeyboardInputEvent captured;
    captured.sequence = static_cast<std::uint64_t>(capturedKeyboardEvents_ + 1);
    captured.monotonicTimestampUsec = monotonicNowUsec();
    captured.action = mapKeyboardAction(event.type());
    captured.modifiers = mapModifiers(event.modifiers());
    captured.nativeScanCode = static_cast<std::uint32_t>(event.nativeScanCode());
    captured.virtualKey = static_cast<std::uint32_t>(event.key());
    captured.unicodeCodepoint = firstCodepoint(event.text());
    enqueueKeyboardEvent(captured);
    return true;
}

void QtInputCapture::enqueueMouseEvent(const modules::input::MouseInputEvent& event)
{
    mouseEvents_.push(event);
    ++capturedMouseEvents_;
}

void QtInputCapture::enqueueKeyboardEvent(const modules::input::KeyboardInputEvent& event)
{
    keyboardEvents_.push(event);
    ++capturedKeyboardEvents_;
}

bool QtInputCapture::isOpen() const
{
    return opened_;
}

int QtInputCapture::capturedMouseEvents() const
{
    return capturedMouseEvents_;
}

int QtInputCapture::capturedKeyboardEvents() const
{
    return capturedKeyboardEvents_;
}

} // namespace input
} // namespace qt
} // namespace adapters
} // namespace fusiondesk
