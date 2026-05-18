#ifndef FUSIONDESK_MODULES_TEST_TEST_ECHO_MODULES_H
#define FUSIONDESK_MODULES_TEST_TEST_ECHO_MODULES_H

#include <string>

#include "fusiondesk/core/module/module.h"

namespace fusiondesk {
namespace modules {
namespace test {

struct TestEchoSnapshot
{
    std::string moduleId;
    module::ModuleState state = module::ModuleState::Created;
    int pingsSent = 0;
    int requestsReceived = 0;
    int responsesSent = 0;
    int responsesReceived = 0;
    int errorsSent = 0;
    int errorsReceived = 0;
    int decodeFailures = 0;
    int sendFailures = 0;
    protocol::MessageId lastMessageId = 0;
    protocol::MessageId lastResponseTo = 0;
    std::string lastPayload;
    std::string peerCompatibilityMode;
};

class TestEchoModule : public module::IModule
{
public:
    explicit TestEchoModule(module::ModuleManifest manifest);

    const module::ModuleManifest& manifest() const override;
    module::ModuleState state() const override;
    bool attach(const module::ModuleRuntime& runtime) override;
    bool start(const module::ModuleStartOptions& options) override;
    void stop(const module::ModuleStopOptions& options) override;
    void detach() override;
    void handlePacket(const protocol::PacketEnvelope& packet) override;
    std::string diagnostics() const override;

    protocol::MessageId sendPing(const std::string& text,
                                 std::uint32_t timeoutMs = 1000);
    TestEchoSnapshot snapshot() const;

private:
    bool sendResponse(const protocol::PacketEnvelope& request,
                      const std::string& text);
    bool sendError(const protocol::PacketEnvelope& request,
                   protocol::ResponseStatus status,
                   const std::string& message);

private:
    module::ModuleManifest manifest_;
    module::ModuleRuntime runtime_;
    module::ModuleState state_ = module::ModuleState::Created;
    protocol::MessageId nextMessageId_ = 1;
    int pingsSent_ = 0;
    int requestsReceived_ = 0;
    int responsesSent_ = 0;
    int responsesReceived_ = 0;
    int errorsSent_ = 0;
    int errorsReceived_ = 0;
    int decodeFailures_ = 0;
    int sendFailures_ = 0;
    protocol::MessageId lastMessageId_ = 0;
    protocol::MessageId lastResponseTo_ = 0;
    std::string lastPayload_;
    std::string peerCompatibilityMode_;
};

} // namespace test
} // namespace modules
} // namespace fusiondesk

#endif // FUSIONDESK_MODULES_TEST_TEST_ECHO_MODULES_H
