#ifndef FUSIONDESK_PLATFORM_WINDOWS_INPUT_WINDOWS_INPUT_INJECTOR_H
#define FUSIONDESK_PLATFORM_WINDOWS_INPUT_WINDOWS_INPUT_INJECTOR_H

#include "fusiondesk/modules/input/input_interfaces.h"

namespace fusiondesk {
namespace platform {
namespace windows {
namespace input {

struct WindowsInputInjectorOptions
{
    bool dryRun = true;
};

class WindowsInputInjector final : public modules::input::IInputInjector
{
public:
    explicit WindowsInputInjector(WindowsInputInjectorOptions options = {});

    bool injectMouse(const modules::input::MouseInputEvent& event) override;
    bool injectKeyboard(const modules::input::KeyboardInputEvent& event) override;

    bool dryRun() const;
    int mouseEvents() const;
    int keyboardEvents() const;
    int nativeFailures() const;
    modules::input::MouseInputEvent lastMouse() const;
    modules::input::KeyboardInputEvent lastKeyboard() const;

private:
    WindowsInputInjectorOptions options_;
    modules::input::MouseInputEvent lastMouse_;
    modules::input::KeyboardInputEvent lastKeyboard_;
    int mouseEvents_ = 0;
    int keyboardEvents_ = 0;
    int nativeFailures_ = 0;
};

} // namespace input
} // namespace windows
} // namespace platform
} // namespace fusiondesk

#endif // FUSIONDESK_PLATFORM_WINDOWS_INPUT_WINDOWS_INPUT_INJECTOR_H
