#ifndef FUSIONDESK_MODULE_MODULE_H
#define FUSIONDESK_MODULE_MODULE_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "fusiondesk/core/diagnostics/diagnostics_sink.h"
#include "fusiondesk/core/module/module_manifest.h"
#include "fusiondesk/core/network/channel_registry.h"
#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/core/session/session_context.h"

namespace fusiondesk {
namespace network {
class NetworkManager;
} // namespace network

namespace module {

enum class ModuleState
{
    Created,
    Attached,
    Starting,
    Running,
    Stopping,
    Stopped,
    Detached,
    Failed
};

struct ModuleRuntime
{
    session::SessionContext session;
    network::INetworkRouter* network = nullptr;
    network::NetworkManager* networkManager = nullptr;
    network::ChannelRegistry* channels = nullptr;
    diagnostics::DiagnosticsSink* diagnostics = nullptr;
};

struct ModuleStartOptions
{
    bool allowPartialStart = false;
    std::vector<ModulePeerVersion> peerVersions;
};

struct ModuleStopOptions
{
    bool force = false;
    std::string reason;
};

struct ModuleReconnectOptions
{
    std::string reason;
    std::vector<network::ChannelKey> affectedChannels;
    std::uint32_t reconnectCount = 0;
    bool requestFreshState = false;
};

struct ModuleReconnectReport
{
    std::string moduleId;
    bool notified = false;
    std::string diagnostics;
};

struct ModuleIngressReplayReport
{
    std::string moduleId;
    bool replayed = false;
    std::size_t tokenCount = 0;
    std::string diagnostics;
    std::string message;
};

class IModule
{
public:
    virtual ~IModule() = default;

    virtual const ModuleManifest& manifest() const = 0;
    virtual ModuleState state() const = 0;
    virtual bool attach(const ModuleRuntime& runtime) = 0;
    virtual bool start(const ModuleStartOptions& options) = 0;
    virtual void stop(const ModuleStopOptions& options) = 0;
    virtual void detach() = 0;
    virtual void handlePacket(const protocol::PacketEnvelope& packet)
    {
        (void)packet;
    }
    virtual std::string diagnostics() const = 0;
};

class IReconnectAwareModule
{
public:
    virtual ~IReconnectAwareModule() = default;

    virtual void pauseForReconnect(const ModuleReconnectOptions& options) = 0;
    virtual void resumeAfterReconnect(const ModuleReconnectOptions& options) = 0;
};

} // namespace module
} // namespace fusiondesk

#endif // FUSIONDESK_MODULE_MODULE_H
