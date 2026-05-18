#include "fusiondesk/modules/test/test_echo_modules.h"

#include <algorithm>
#include <utility>

namespace fusiondesk {
namespace modules {
namespace test {

namespace {

const char kPingPrefix[] = "FDET/1 PING ";
const char kPongPrefix[] = "FDET/1 PONG ";
const char kErrorPrefix[] = "FDET/1 ERROR ";

network::ChannelKey echoChannelKey()
{
    return network::ChannelKey{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
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

protocol::ByteBuffer bytes(const std::string& value)
{
    return protocol::ByteBuffer(value.begin(), value.end());
}

std::string text(const protocol::ByteBuffer& value)
{
    return std::string(value.begin(), value.end());
}

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::string afterPrefix(const std::string& value, const std::string& prefix)
{
    if (!startsWith(value, prefix))
        return {};

    return value.substr(prefix.size());
}

protocol::PacketEnvelope makeEchoPacket(const module::ModuleRuntime& runtime,
                                        protocol::MessageKind kind)
{
    const network::ChannelKey key = echoChannelKey();
    protocol::PacketEnvelope packet;
    packet.sessionId = runtime.session.sessionId;
    packet.traceId = runtime.session.traceId;
    packet.channelId = key.channelId;
    packet.channelType = key.channelType;
    packet.packetType = protocol::PacketType::Exchange;
    packet.messageKind = kind;
    packet.priority = protocol::PacketPriority::Interactive;
    return packet;
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
    event.channel = echoChannelKey();
    event.severity = diagnostics::DiagnosticSeverity::Info;
    event.code = code;
    event.message = message;
    event.policyVersion = runtime.session.policyVersion;
    runtime.diagnostics->publish(event);
}

} // namespace

TestEchoModule::TestEchoModule(module::ModuleManifest manifest)
    : manifest_(std::move(manifest))
{
}

const module::ModuleManifest& TestEchoModule::manifest() const
{
    return manifest_;
}

module::ModuleState TestEchoModule::state() const
{
    return state_;
}

bool TestEchoModule::attach(const module::ModuleRuntime& runtime)
{
    runtime_ = runtime;
    state_ = module::ModuleState::Attached;
    return !manifest_.moduleId.empty();
}

bool TestEchoModule::start(const module::ModuleStartOptions& options)
{
    if (runtime_.network == nullptr) {
        state_ = module::ModuleState::Failed;
        return false;
    }

    for (const module::ModulePeerVersion& peer : options.peerVersions) {
        if (peer.compatible) {
            peerCompatibilityMode_ = peer.compatibilityMode;
            break;
        }
    }

    state_ = module::ModuleState::Running;
    publish(runtime_, manifest_.moduleId, "test.echo.started", "test echo module started");
    return true;
}

void TestEchoModule::stop(const module::ModuleStopOptions&)
{
    state_ = module::ModuleState::Stopped;
    publish(runtime_, manifest_.moduleId, "test.echo.stopped", "test echo module stopped");
}

void TestEchoModule::detach()
{
    state_ = module::ModuleState::Detached;
}

void TestEchoModule::handlePacket(const protocol::PacketEnvelope& packet)
{
    if (state_ != module::ModuleState::Running ||
        packet.packetType != protocol::PacketType::Exchange) {
        return;
    }

    const std::string payload = text(packet.payload);
    lastPayload_ = payload;

    if (packet.messageKind == protocol::MessageKind::Request) {
        if (!startsWith(payload, "FDET/1 "))
            return;

        if (!startsWith(payload, kPingPrefix)) {
            ++decodeFailures_;
            sendError(packet,
                      protocol::ResponseStatus::ProtocolError,
                      "malformed echo request");
            return;
        }

        ++requestsReceived_;
        sendResponse(packet, afterPrefix(payload, kPingPrefix));
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Response) {
        if (!startsWith(payload, "FDET/1 "))
            return;

        if (packet.responseStatus == protocol::ResponseStatus::Ok &&
            startsWith(payload, kPongPrefix)) {
            ++responsesReceived_;
            lastResponseTo_ = packet.responseTo;
            return;
        }

        ++decodeFailures_;
        return;
    }

    if (packet.messageKind == protocol::MessageKind::Error) {
        if (!startsWith(payload, kErrorPrefix))
            return;

        ++errorsReceived_;
        lastResponseTo_ = packet.responseTo;
    }
}

std::string TestEchoModule::diagnostics() const
{
    return "test.echo id=" + manifest_.moduleId +
           " state=" + stateName(state_) +
           " pingsSent=" + std::to_string(pingsSent_) +
           " requestsReceived=" + std::to_string(requestsReceived_) +
           " responsesSent=" + std::to_string(responsesSent_) +
           " responsesReceived=" + std::to_string(responsesReceived_) +
           " errorsSent=" + std::to_string(errorsSent_) +
           " errorsReceived=" + std::to_string(errorsReceived_) +
           " decodeFailures=" + std::to_string(decodeFailures_) +
           " sendFailures=" + std::to_string(sendFailures_);
}

protocol::MessageId TestEchoModule::sendPing(const std::string& text,
                                             std::uint32_t timeoutMs)
{
    if (state_ != module::ModuleState::Running || runtime_.network == nullptr)
        return 0;

    protocol::PacketEnvelope packet = makeEchoPacket(runtime_, protocol::MessageKind::Request);
    packet.messageId = nextMessageId_++;
    packet.correlationId = packet.messageId;
    packet.timeoutMs = timeoutMs;
    packet.flags = protocol::PacketFlagResponseRequired;
    packet.payload = bytes(std::string(kPingPrefix) + text);

    const network::SendResult result = runtime_.network->send(packet);
    if (result.status != network::SendStatus::Sent) {
        ++sendFailures_;
        return 0;
    }

    ++pingsSent_;
    lastMessageId_ = packet.messageId;
    lastPayload_ = text;
    publish(runtime_, manifest_.moduleId, "test.echo.ping_sent", "test echo ping sent");
    return packet.messageId;
}

TestEchoSnapshot TestEchoModule::snapshot() const
{
    TestEchoSnapshot snapshot;
    snapshot.moduleId = manifest_.moduleId;
    snapshot.state = state_;
    snapshot.pingsSent = pingsSent_;
    snapshot.requestsReceived = requestsReceived_;
    snapshot.responsesSent = responsesSent_;
    snapshot.responsesReceived = responsesReceived_;
    snapshot.errorsSent = errorsSent_;
    snapshot.errorsReceived = errorsReceived_;
    snapshot.decodeFailures = decodeFailures_;
    snapshot.sendFailures = sendFailures_;
    snapshot.lastMessageId = lastMessageId_;
    snapshot.lastResponseTo = lastResponseTo_;
    snapshot.lastPayload = lastPayload_;
    snapshot.peerCompatibilityMode = peerCompatibilityMode_;
    return snapshot;
}

bool TestEchoModule::sendResponse(const protocol::PacketEnvelope& request,
                                  const std::string& text)
{
    protocol::PacketEnvelope response = makeEchoPacket(runtime_, protocol::MessageKind::Response);
    response.messageId = nextMessageId_++;
    response.correlationId = request.correlationId != 0 ? request.correlationId : request.messageId;
    response.responseTo = request.messageId;
    response.timeoutMs = request.timeoutMs;
    response.responseStatus = protocol::ResponseStatus::Ok;
    response.payload = bytes(std::string(kPongPrefix) + text);

    const network::SendResult result = runtime_.network->send(response);
    if (result.status != network::SendStatus::Sent) {
        ++sendFailures_;
        return false;
    }

    ++responsesSent_;
    lastMessageId_ = response.messageId;
    lastResponseTo_ = request.messageId;
    publish(runtime_, manifest_.moduleId, "test.echo.response_sent", "test echo response sent");
    return true;
}

bool TestEchoModule::sendError(const protocol::PacketEnvelope& request,
                               protocol::ResponseStatus status,
                               const std::string& message)
{
    protocol::PacketEnvelope response = makeEchoPacket(runtime_, protocol::MessageKind::Error);
    response.messageId = nextMessageId_++;
    response.correlationId = request.correlationId != 0 ? request.correlationId : request.messageId;
    response.responseTo = request.messageId;
    response.timeoutMs = request.timeoutMs;
    response.responseStatus = status;
    response.payload = bytes(std::string(kErrorPrefix) + message);

    const network::SendResult result = runtime_.network != nullptr ?
                                      runtime_.network->send(response) :
                                      network::SendResult{network::SendStatus::Failed,
                                                          "network is missing"};
    if (result.status != network::SendStatus::Sent) {
        ++sendFailures_;
        return false;
    }

    ++errorsSent_;
    lastMessageId_ = response.messageId;
    lastResponseTo_ = request.messageId;
    return true;
}

} // namespace test
} // namespace modules
} // namespace fusiondesk
