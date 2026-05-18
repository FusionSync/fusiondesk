#ifndef FUSIONDESK_MODULES_INPUT_INPUT_MODULES_H
#define FUSIONDESK_MODULES_INPUT_INPUT_MODULES_H

#include <memory>
#include <string>

#include "fusiondesk/core/module/module.h"
#include "fusiondesk/modules/input/input_interfaces.h"

namespace fusiondesk {
namespace modules {
namespace input {

struct InputClientSnapshot
{
    std::string moduleId;
    module::ModuleState state = module::ModuleState::Created;
    bool captureAttached = false;
    int eventsSent = 0;
    int sendFailures = 0;
    int captureOpenFailures = 0;
};

struct InputAgentSnapshot
{
    std::string moduleId;
    module::ModuleState state = module::ModuleState::Created;
    int eventsReceived = 0;
    int eventsInjected = 0;
    int decodeFailures = 0;
    int injectionFailures = 0;
};

class InputClientModule : public module::IModule
{
public:
    InputClientModule(module::ModuleManifest manifest,
                      InputModuleKind kind,
                      std::shared_ptr<IInputCapture> capture = nullptr);

    const module::ModuleManifest& manifest() const override;
    module::ModuleState state() const override;
    bool attach(const module::ModuleRuntime& runtime) override;
    bool start(const module::ModuleStartOptions& options) override;
    void stop(const module::ModuleStopOptions& options) override;
    void detach() override;
    std::string diagnostics() const override;

    bool sendMouseEvent(const MouseInputEvent& event);
    bool sendKeyboardEvent(const KeyboardInputEvent& event);
    InputClientSnapshot snapshot() const;

private:
    bool sendPayload(protocol::PacketType packetType,
                     std::uint64_t sequence,
                     std::uint64_t timestampUsec,
                     protocol::ByteBuffer payload);

private:
    module::ModuleManifest manifest_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    InputModuleKind kind_;
    std::shared_ptr<IInputCapture> capture_;
    int eventsSent_ = 0;
    int sendFailures_ = 0;
    int captureOpenFailures_ = 0;
};

class InputAgentModule : public module::IModule
{
public:
    InputAgentModule(module::ModuleManifest manifest,
                     InputModuleKind kind,
                     std::shared_ptr<IInputInjector> injector);

    const module::ModuleManifest& manifest() const override;
    module::ModuleState state() const override;
    bool attach(const module::ModuleRuntime& runtime) override;
    bool start(const module::ModuleStartOptions& options) override;
    void stop(const module::ModuleStopOptions& options) override;
    void detach() override;
    void handlePacket(const protocol::PacketEnvelope& packet) override;
    std::string diagnostics() const override;

    InputAgentSnapshot snapshot() const;

private:
    bool injectMouse(const protocol::PacketEnvelope& packet);
    bool injectKeyboard(const protocol::PacketEnvelope& packet);

private:
    module::ModuleManifest manifest_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    InputModuleKind kind_;
    std::shared_ptr<IInputInjector> injector_;
    int eventsReceived_ = 0;
    int eventsInjected_ = 0;
    int decodeFailures_ = 0;
    int injectionFailures_ = 0;
};

} // namespace input
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_INPUT_INPUT_MODULES_H
