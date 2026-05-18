#ifndef FUSIONDESK_RUNTIME_CONNECTION_MODULE_INVENTORY_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_CONNECTION_MODULE_INVENTORY_RUNTIME_SERVICE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/network/request_tracker.h"
#include "fusiondesk/runtime/connection/module_inventory_exchange.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ModuleInventoryRuntimeServiceStartOptions
{
    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    bool startResponder = true;
    bool subscribeResponses = true;
    ModuleInventoryServiceStartOptions responder;
};

struct ModuleInventoryRuntimeServiceStartResult
{
    bool ok = false;
    ModuleInventoryServiceStartResult responder;
    std::vector<network::SubscriptionToken> responseTokens;
    std::vector<std::string> messages;
};

struct ModuleInventoryRuntimeExchangeOptions
{
    ModuleInventoryWireOptions wire;
    bool assignMissingMessageId = true;
};

struct ModuleInventoryRuntimeDispatchResult
{
    bool ok = false;
    protocol::PacketEnvelope request;
    std::vector<std::string> messages;
};

struct ModuleInventoryRuntimeCompletion
{
    bool terminal = false;
    bool ok = false;
    protocol::PacketEnvelope response;
    ModuleInventory inventory;
    std::vector<std::string> messages;
};

struct ModuleInventoryRuntimeServiceSnapshot
{
    bool active = false;
    ModuleInventoryServiceSnapshot responder;
    std::size_t pendingRequests = 0;
    std::size_t completedResponses = 0;
    std::size_t expiredRequests = 0;
    std::vector<network::PendingRequestSnapshot> pending;
    std::vector<ModuleInventoryRuntimeCompletion> completions;
    std::vector<std::string> messages;
};

class ModuleInventoryRuntimeService
{
public:
    explicit ModuleInventoryRuntimeService(network::INetworkRouter& router,
                                           protocol::MessageId firstMessageId = 1);
    ~ModuleInventoryRuntimeService();

    ModuleInventoryRuntimeService(const ModuleInventoryRuntimeService&) = delete;
    ModuleInventoryRuntimeService& operator=(const ModuleInventoryRuntimeService&) = delete;

    ModuleInventoryRuntimeServiceStartResult start(
        const ModuleInventoryRuntimeServiceStartOptions& options = {});
    void stop();
    bool active() const;

    ModuleInventoryRuntimeDispatchResult requestModuleInventory(
        const ModuleInventory& inventory,
        const ModuleInventoryRuntimeExchangeOptions& options = {});
    bool complete(const protocol::PacketEnvelope& response);
    std::size_t expire(std::uint64_t nowUsec);

    ModuleInventoryRuntimeServiceSnapshot snapshot() const;
    const ModuleInventory& lastRemoteInventoryFromResponder() const;

private:
    static network::RouteMatch responseRoute(network::ChannelKey controlChannel,
                                             protocol::MessageKind messageKind);
    static bool responseMessageKind(protocol::MessageKind messageKind);
    static bool terminalMessageKind(protocol::MessageKind messageKind);
    static protocol::ResponseStatus sendStatusToResponseStatus(network::SendStatus status);

    bool subscribeResponse(protocol::MessageKind messageKind,
                           ModuleInventoryRuntimeServiceStartResult& result);
    void clearResponseSubscriptions();
    void recordResponse(const protocol::PacketEnvelope& response);
    void failTrackedRequest(const protocol::PacketEnvelope& request,
                            protocol::ResponseStatus status,
                            const std::string& message);
    void remember(const std::vector<std::string>& messages);
    void remember(std::string message);

private:
    network::INetworkRouter& router_;
    network::RequestTracker requestTracker_;
    ModuleInventoryService responder_;
    network::ChannelKey controlChannel_{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    bool active_ = false;
    std::size_t expiredRequests_ = 0;
    std::vector<network::SubscriptionToken> responseTokens_;
    std::vector<ModuleInventoryRuntimeCompletion> completions_;
    std::vector<std::string> messages_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_MODULE_INVENTORY_RUNTIME_SERVICE_H
