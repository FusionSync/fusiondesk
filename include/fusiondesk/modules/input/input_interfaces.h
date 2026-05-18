#ifndef FUSIONDESK_MODULES_INPUT_INPUT_INTERFACES_H
#define FUSIONDESK_MODULES_INPUT_INPUT_INTERFACES_H

#include "fusiondesk/modules/input/input_types.h"

namespace fusiondesk {
namespace modules {
namespace input {

class IInputCapture
{
public:
    virtual ~IInputCapture() = default;
    virtual bool open()
    {
        return true;
    }

    virtual void close()
    {
    }

    virtual bool pollMouseEvent(MouseInputEvent& event)
    {
        (void)event;
        return false;
    }

    virtual bool pollKeyboardEvent(KeyboardInputEvent& event)
    {
        (void)event;
        return false;
    }
};

class IInputInjector
{
public:
    virtual ~IInputInjector() = default;
    virtual bool injectMouse(const MouseInputEvent& event)
    {
        (void)event;
        return false;
    }

    virtual bool injectKeyboard(const KeyboardInputEvent& event)
    {
        (void)event;
        return false;
    }
};

} // namespace input
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_INPUT_INPUT_INTERFACES_H
