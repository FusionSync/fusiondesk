#include "fusiondesk/modules/input/input_modules.h"

#include <utility>

#include "fusiondesk/modules/input/input_payload_codec.h"

namespace fusiondesk {
namespace modules {
namespace input {

namespace {

network::ChannelKey inputChannelKey()
{
    return network::ChannelKey{static_cast<protocol::ChannelId>(protocol::ChannelIdValue::SmallData),
                               protocol::ChannelType::Standard};
}

const char* stateName(module::ModuleState state)
{
    switch (state) {
    case module::ModuleState::Created:
        return "created";
    case module::ModuleState::Attached:
        return "attached";
    case module::ModuleState::Starting:
        return "starting";
    case module::ModuleState::Running:
        return "running";
    case module::ModuleState::Stopping:
        return "stopping";
    case module::ModuleState::Stopped:
        return "stopped";
    case module::ModuleState::Detached:
        return "detached";
    case module::ModuleState::Failed:
        return "failed";
    }
    return "unknown";
}

void publish(const module::ModuleRuntime& runtime,
             const std::string& moduleId,
             const std::string& code,
             const std::string& message)
{
    if (runtime.diagnostics == nullptr)
        return;

    diagnostics::DiagnosticEvent event;
    event.sessionId = runtime.session.sessionId;
    event.traceId = runtime.session.traceId;
    event.moduleId = moduleId;
    event.channel = inputChannelKey();
    event.severity = diagnostics::DiagnosticSeverity::Info;
    event.code = code;
    event.message = message;
    event.policyVersion = runtime.session.policyVersion;
    runtime.diagnostics->publish(event);
}

protocol::PacketEnvelope makeInputPacket(const module::ModuleRuntime& runtime,
                                         protocol::PacketType packetType)
{
    protocol::PacketEnvelope packet;
    packet.sessionId = runtime.session.sessionId;
    packet.traceId = runtime.session.traceId;
    packet.channelId = inputChannelKey().channelId;
    packet.channelType = inputChannelKey().channelType;
    packet.packetType = packetType;
    packet.messageKind = protocol::MessageKind::Event;
    packet.priority = protocol::PacketPriority::Interactive;
    packet.flags = protocol::PacketFlagNoResponseRequired |
                   protocol::PacketFlagCoalescable;
    return packet;
}

} // namespace

InputClientModule::InputClientModule(module::ModuleManifest manifest,
                                     InputModuleKind kind,
                                     std::shared_ptr<IInputCapture> capture)
    : manifest_(std::move(manifest)),
      kind_(kind),
      capture_(std::move(capture))
{
}

const module::ModuleManifest& InputClientModule::manifest() const
{
    return manifest_;
}

module::ModuleState InputClientModule::state() const
{
    return state_;
}

bool InputClientModule::attach(const module::ModuleRuntime& runtime)
{
    runtime_ = runtime;
    state_ = module::ModuleState::Attached;
    return !manifest_.moduleId.empty();
}

bool InputClientModule::start(const module::ModuleStartOptions&)
{
    if (runtime_.network == nullptr) {
        state_ = module::ModuleState::Failed;
        return false;
    }

    if (capture_ != nullptr && !capture_->open()) {
        ++captureOpenFailures_;
        state_ = module::ModuleState::Failed;
        return false;
    }

    state_ = module::ModuleState::Running;
    publish(runtime_, manifest_.moduleId, "input.client_started", "input sender started");
    return true;
}

void InputClientModule::stop(const module::ModuleStopOptions&)
{
    if (capture_ != nullptr)
        capture_->close();

    state_ = module::ModuleState::Stopped;
    publish(runtime_, manifest_.moduleId, "input.client_stopped", "input sender stopped");
}

void InputClientModule::detach()
{
    state_ = module::ModuleState::Detached;
}

std::string InputClientModule::diagnostics() const
{
    return "input.client id=" + manifest_.moduleId +
           " state=" + stateName(state_) +
           " sent=" + std::to_string(eventsSent_) +
           " sendFailures=" + std::to_string(sendFailures_);
}

bool InputClientModule::sendMouseEvent(const MouseInputEvent& event)
{
    if (kind_ != InputModuleKind::Mouse)
        return false;

    return sendPayload(protocol::PacketType::Mouse,
                       event.sequence,
                       event.monotonicTimestampUsec,
                       encodeMouseInputPayload(event));
}

bool InputClientModule::sendKeyboardEvent(const KeyboardInputEvent& event)
{
    if (kind_ != InputModuleKind::Keyboard)
        return false;

    return sendPayload(protocol::PacketType::Keyboard,
                       event.sequence,
                       event.monotonicTimestampUsec,
                       encodeKeyboardInputPayload(event));
}

InputClientSnapshot InputClientModule::snapshot() const
{
    InputClientSnapshot snapshot;
    snapshot.moduleId = manifest_.moduleId;
    snapshot.state = state_;
    snapshot.captureAttached = capture_ != nullptr;
    snapshot.eventsSent = eventsSent_;
    snapshot.sendFailures = sendFailures_;
    snapshot.captureOpenFailures = captureOpenFailures_;
    return snapshot;
}

bool InputClientModule::sendPayload(protocol::PacketType packetType,
                                    std::uint64_t sequence,
                                    std::uint64_t timestampUsec,
                                    protocol::ByteBuffer payload)
{
    if (state_ != module::ModuleState::Running ||
        runtime_.network == nullptr ||
        payload.empty()) {
        ++sendFailures_;
        return false;
    }

    protocol::PacketEnvelope packet = makeInputPacket(runtime_, packetType);
    packet.sequence = sequence;
    packet.monotonicTimestampUsec = timestampUsec;
    packet.payload = std::move(payload);
    const network::SendResult result = runtime_.network->send(packet);
    if (result.status != network::SendStatus::Sent) {
        ++sendFailures_;
        return false;
    }

    ++eventsSent_;
    publish(runtime_, manifest_.moduleId, "input.event_sent", "input event sent");
    return true;
}

InputAgentModule::InputAgentModule(module::ModuleManifest manifest,
                                   InputModuleKind kind,
                                   std::shared_ptr<IInputInjector> injector)
    : manifest_(std::move(manifest)),
      kind_(kind),
      injector_(std::move(injector))
{
}

const module::ModuleManifest& InputAgentModule::manifest() const
{
    return manifest_;
}

module::ModuleState InputAgentModule::state() const
{
    return state_;
}

bool InputAgentModule::attach(const module::ModuleRuntime& runtime)
{
    runtime_ = runtime;
    state_ = module::ModuleState::Attached;
    return !manifest_.moduleId.empty();
}

bool InputAgentModule::start(const module::ModuleStartOptions&)
{
    state_ = module::ModuleState::Running;
    publish(runtime_, manifest_.moduleId, "input.agent_started", "input injector contract started");
    return true;
}

void InputAgentModule::stop(const module::ModuleStopOptions&)
{
    state_ = module::ModuleState::Stopped;
    publish(runtime_, manifest_.moduleId, "input.agent_stopped", "input injector contract stopped");
}

void InputAgentModule::detach()
{
    state_ = module::ModuleState::Detached;
}

void InputAgentModule::handlePacket(const protocol::PacketEnvelope& packet)
{
    if (state_ != module::ModuleState::Running)
        return;

    if (kind_ == InputModuleKind::Mouse &&
        packet.packetType == protocol::PacketType::Mouse) {
        injectMouse(packet);
        return;
    }

    if (kind_ == InputModuleKind::Keyboard &&
        packet.packetType == protocol::PacketType::Keyboard) {
        injectKeyboard(packet);
    }
}

std::string InputAgentModule::diagnostics() const
{
    return "input.agent id=" + manifest_.moduleId +
           " state=" + stateName(state_) +
           " received=" + std::to_string(eventsReceived_) +
           " injected=" + std::to_string(eventsInjected_) +
           " decodeFailures=" + std::to_string(decodeFailures_) +
           " injectionFailures=" + std::to_string(injectionFailures_);
}

InputAgentSnapshot InputAgentModule::snapshot() const
{
    InputAgentSnapshot snapshot;
    snapshot.moduleId = manifest_.moduleId;
    snapshot.state = state_;
    snapshot.eventsReceived = eventsReceived_;
    snapshot.eventsInjected = eventsInjected_;
    snapshot.decodeFailures = decodeFailures_;
    snapshot.injectionFailures = injectionFailures_;
    return snapshot;
}

bool InputAgentModule::injectMouse(const protocol::PacketEnvelope& packet)
{
    ++eventsReceived_;
    const InputDecodeResult decoded = decodeInputPayload(packet.payload);
    if (!decoded.ok || decoded.kind != InputModuleKind::Mouse) {
        ++decodeFailures_;
        return false;
    }

    if (injector_ == nullptr || !injector_->injectMouse(decoded.mouse)) {
        ++injectionFailures_;
        publish(runtime_, manifest_.moduleId, "input.inject_failed", "mouse injection failed or missing");
        return false;
    }

    ++eventsInjected_;
    publish(runtime_, manifest_.moduleId, "input.event_injected", "mouse event injected");
    return true;
}

bool InputAgentModule::injectKeyboard(const protocol::PacketEnvelope& packet)
{
    ++eventsReceived_;
    const InputDecodeResult decoded = decodeInputPayload(packet.payload);
    if (!decoded.ok || decoded.kind != InputModuleKind::Keyboard) {
        ++decodeFailures_;
        return false;
    }

    if (injector_ == nullptr || !injector_->injectKeyboard(decoded.keyboard)) {
        ++injectionFailures_;
        publish(runtime_, manifest_.moduleId, "input.inject_failed", "keyboard injection failed or missing");
        return false;
    }

    ++eventsInjected_;
    publish(runtime_, manifest_.moduleId, "input.event_injected", "keyboard event injected");
    return true;
}

} // namespace input
} // namespace modules
} // namespace fusiondesk
