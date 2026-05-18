#include <cassert>

#include "fusiondesk/platform/windows/input/windows_input_injector.h"

using namespace fusiondesk;

namespace {

void windowsInputInjectorDryRunRecordsEvents()
{
    platform::windows::input::WindowsInputInjector injector;
    assert(injector.dryRun());

    modules::input::MouseInputEvent mouse;
    mouse.sequence = 1;
    mouse.x = 320;
    mouse.y = 240;
    mouse.action = modules::input::MouseAction::Move;
    assert(injector.injectMouse(mouse));

    modules::input::KeyboardInputEvent keyboard;
    keyboard.sequence = 2;
    keyboard.action = modules::input::KeyboardAction::KeyDown;
    keyboard.virtualKey = 0x41;
    assert(injector.injectKeyboard(keyboard));

    assert(injector.mouseEvents() == 1);
    assert(injector.keyboardEvents() == 1);
    assert(injector.nativeFailures() == 0);
    assert(injector.lastMouse().x == 320);
    assert(injector.lastKeyboard().virtualKey == 0x41);
}

} // namespace

int main()
{
    windowsInputInjectorDryRunRecordsEvents();
    return 0;
}
