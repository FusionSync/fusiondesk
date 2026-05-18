#ifndef FUSIONDESK_RUNTIME_CONNECTION_MODULE_INVENTORY_EXCHANGE_H
#define FUSIONDESK_RUNTIME_CONNECTION_MODULE_INVENTORY_EXCHANGE_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/module/module_manifest.h"
#include "fusiondesk/core/network/network_router.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

struct ModuleInventory
{
    protocol::SessionId sessionId = 0;
    std::vector<module::ModuleManifest> manifests;
};

struct ModuleInventoryDecodeResult
{
    bool ok = false;
    ModuleInventory inventory;
    std::string message;
};

struct ModuleInventoryWireOptions
{
    protocol::MessageId messageId = 1;
    protocol::MessageId correlationId = 0;
    std::uint32_t timeoutMs = 1000;
    protocol::SessionId sessionId = 0;
    protocol::TraceId traceId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::PacketPriority priority = protocol::PacketPriority::Critical;
};

struct ModuleInventoryWireResponseOptions
{
    protocol::MessageId messageId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicTimestampUsec = 0;
    protocol::ResponseStatus status = protocol::ResponseStatus::Ok;
};

struct ModuleInventoryServiceStartOptions
{
    network::ChannelKey controlChannel{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::MessageId firstResponseMessageId = 1;
    ModuleInventory localInventory;
};

struct ModuleInventoryServiceStartResult
{
    bool ok = false;
    network::SubscriptionToken requestToken = 0;
    std::vector<std::string> messages;
};

struct ModuleInventoryServiceSnapshot
{
    bool active = false;
    std::size_t handledRequests = 0;
    std::size_t ignoredPackets = 0;
    std::size_t failedRequests = 0;
    std::size_t sentResponses = 0;
    bool hasLastRemoteInventory = false;
    protocol::SessionId lastRemoteSessionId = 0;
    std::size_t lastRemoteModuleCount = 0;
    std::vector<std::string> messages;
};

ModuleInventory makeModuleInventory(protocol::SessionId sessionId,
                                    const std::vector<module::ModuleManifest>& manifests);
std::vector<module::ModulePeerVersion> peerVersionsFromModuleInventory(
    const ModuleInventory& inventory);

protocol::ByteBuffer encodeModuleInventoryRequestPayload(const ModuleInventory& inventory);
protocol::ByteBuffer encodeModuleInventoryResponsePayload(const ModuleInventory& inventory);

ModuleInventoryDecodeResult decodeModuleInventoryRequestPacket(
    const protocol::PacketEnvelope& packet);
ModuleInventoryDecodeResult decodeModuleInventoryResponsePacket(
    const protocol::PacketEnvelope& packet);

protocol::PacketEnvelope makeModuleInventoryRequestPacket(
    const ModuleInventory& inventory,
    const ModuleInventoryWireOptions& options = {});
protocol::PacketEnvelope makeModuleInventoryResponsePacket(
    const protocol::PacketEnvelope& request,
    const ModuleInventory& inventory,
    const ModuleInventoryWireResponseOptions& options = {});

class ModuleInventoryService
{
public:
    explicit ModuleInventoryService(network::INetworkRouter& router);
    ~ModuleInventoryService();

    ModuleInventoryService(const ModuleInventoryService&) = delete;
    ModuleInventoryService& operator=(const ModuleInventoryService&) = delete;

    ModuleInventoryServiceStartResult start(
        const ModuleInventoryServiceStartOptions& options = {});
    void stop();
    bool active() const;

    bool handleRequest(const protocol::PacketEnvelope& packet);
    ModuleInventoryServiceSnapshot snapshot() const;
    const ModuleInventory& lastRemoteInventory() const;

private:
    static bool looksLikeModuleInventoryPayload(const protocol::ByteBuffer& payload);
    static network::RouteMatch requestRoute(network::ChannelKey controlChannel);
    protocol::MessageId nextResponseMessageId();
    void remember(std::string message);

private:
    network::INetworkRouter& router_;
    network::SubscriptionToken requestToken_ = 0;
    network::ChannelKey controlChannel_{
        static_cast<protocol::ChannelId>(protocol::ChannelIdValue::UserAuthMain),
        protocol::ChannelType::Control};
    protocol::MessageId nextResponseMessageId_ = 1;
    ModuleInventory localInventory_;
    ModuleInventory lastRemoteInventory_;
    bool hasLastRemoteInventory_ = false;
    bool active_ = false;
    std::size_t handledRequests_ = 0;
    std::size_t ignoredPackets_ = 0;
    std::size_t failedRequests_ = 0;
    std::size_t sentResponses_ = 0;
    std::vector<std::string> messages_;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_MODULE_INVENTORY_EXCHANGE_H
