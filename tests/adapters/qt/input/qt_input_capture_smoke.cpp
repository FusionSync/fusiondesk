#include <cassert>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QtGlobal>

#include "fusiondesk/adapters/qt/input/qt_input_capture.h"

using namespace fusiondesk;

namespace {

QMouseEvent makeMouseEvent()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QMouseEvent(QEvent::MouseButtonPress,
                       QPointF(12.0, 34.0),
                       QPointF(12.0, 34.0),
                       QPointF(12.0, 34.0),
                       Qt::LeftButton,
                       Qt::LeftButton,
                       Qt::ShiftModifier);
#else
    return QMouseEvent(QEvent::MouseButtonPress,
                       QPointF(12.0, 34.0),
                       Qt::LeftButton,
                       Qt::LeftButton,
                       Qt::ShiftModifier);
#endif
}

void capturesQtMouseAndKeyboardEvents()
{
    adapters::qt::input::QtInputCapture capture;
    assert(capture.open());
    assert(capture.isOpen());

    QMouseEvent mouse = makeMouseEvent();
    assert(capture.captureMouseEvent(mouse));

    QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, "a");
    assert(capture.captureKeyEvent(key));

    modules::input::MouseInputEvent mouseEvent;
    assert(capture.pollMouseEvent(mouseEvent));
    assert(mouseEvent.action == modules::input::MouseAction::ButtonDown);
    assert(mouseEvent.button == modules::input::MouseButton::Left);
    assert(mouseEvent.coordinateSpace == modules::input::InputCoordinateSpace::Pixel);
    assert(mouseEvent.modifiers.shift);
    assert(mouseEvent.x == 12);
    assert(mouseEvent.y == 34);

    modules::input::KeyboardInputEvent keyboardEvent;
    assert(capture.pollKeyboardEvent(keyboardEvent));
    assert(keyboardEvent.action == modules::input::KeyboardAction::KeyDown);
    assert(keyboardEvent.virtualKey == static_cast<std::uint32_t>(Qt::Key_A));
    assert(keyboardEvent.modifiers.control);
    assert(keyboardEvent.unicodeCodepoint == 'a');

    assert(capture.capturedMouseEvents() == 1);
    assert(capture.capturedKeyboardEvents() == 1);
    assert(!capture.pollMouseEvent(mouseEvent));
    assert(!capture.pollKeyboardEvent(keyboardEvent));

    capture.close();
    assert(!capture.isOpen());
}

} // namespace

int main()
{
    capturesQtMouseAndKeyboardEvents();
    return 0;
}
