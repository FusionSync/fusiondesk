#include "fusiondesk/platform/windows/input/windows_input_injector.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace input {

WindowsInputInjector::WindowsInputInjector(WindowsInputInjectorOptions options)
    : options_(options)
{
}

bool WindowsInputInjector::injectMouse(const modules::input::MouseInputEvent& event)
{
    lastMouse_ = event;
    ++mouseEvents_;
    if (options_.dryRun)
        return true;

    ++nativeFailures_;
    return false;
}

bool WindowsInputInjector::injectKeyboard(const modules::input::KeyboardInputEvent& event)
{
    lastKeyboard_ = event;
    ++keyboardEvents_;
    if (options_.dryRun)
        return true;

    ++nativeFailures_;
    return false;
}

bool WindowsInputInjector::dryRun() const
{
    return options_.dryRun;
}

int WindowsInputInjector::mouseEvents() const
{
    return mouseEvents_;
}

int WindowsInputInjector::keyboardEvents() const
{
    return keyboardEvents_;
}

int WindowsInputInjector::nativeFailures() const
{
    return nativeFailures_;
}

modules::input::MouseInputEvent WindowsInputInjector::lastMouse() const
{
    return lastMouse_;
}

modules::input::KeyboardInputEvent WindowsInputInjector::lastKeyboard() const
{
    return lastKeyboard_;
}

} // namespace input
} // namespace windows
} // namespace platform
} // namespace fusiondesk
