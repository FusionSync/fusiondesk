#ifndef FUSIONDESK_RUNTIME_QT_QT_RECONNECT_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_QT_QT_RECONNECT_RUNTIME_SERVICE_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/network_router.h"
#include "fusiondesk/runtime/connection/reconnect_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_reconnect_executor.h"
#include "fusiondesk/runtime/qt/qt_timer_bridge.h"

namespace fusiondesk {
namespace runtime {
namespace qt {

struct QtReconnectRuntimeServiceStartOptions
{
    connection::ReconnectRuntimeServiceStartOptions runtime;
    bool startTimer = true;
    std::uint32_t timerIntervalMs = 10;
};

struct QtReconnectRuntimeServiceStartResult
{
    bool ok = false;
    connection::ReconnectRuntimeServiceStartResult runtime;
    bool timerStarted = false;
    std::vector<std::string> messages;
};

struct QtReconnectRuntimeServiceSnapshot
{
    bool active = false;
    bool timerRunning = false;
    std::uint32_t timerIntervalMs = 0;
    connection::ReconnectRuntimeServiceSnapshot runtime;
    std::vector<std::string> messages;
};

class QtReconnectRuntimeService
{
public:
    QtReconnectRuntimeService(QtRuntimeTransportManager& transportManager,
                              network::INetworkRouter& router,
                              protocol::MessageId firstMessageId = 1);
    ~QtReconnectRuntimeService();

    QtReconnectRuntimeService(const QtReconnectRuntimeService&) = delete;
    QtReconnectRuntimeService& operator=(const QtReconnectRuntimeService&) = delete;

    QtReconnectRuntimeServiceStartResult start(
        const QtReconnectRuntimeServiceStartOptions& options = {});
    void stop();
    bool active() const;

    connection::ReconnectCoordinatorRunResult run(
        const connection::ReconnectOrchestrationRequest& request,
        const connection::ReconnectCoordinatorOptions& options = {});
    std::size_t expire(std::uint64_t nowUsec);

    QtReconnectRuntimeServiceSnapshot snapshot() const;

private:
    void remember(const std::vector<std::string>& messages);

private:
    QtReconnectExecutor executor_;
    connection::ReconnectRuntimeService runtime_;
    QtTimerBridge timer_;
    bool active_ = false;
    std::vector<std::string> messages_;
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_RECONNECT_RUNTIME_SERVICE_H
