#ifndef FUSIONDESK_RUNTIME_QT_QT_PEER_PROFILE_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_QT_QT_PEER_PROFILE_RUNTIME_SERVICE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/runtime/connection/peer_profile_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_timer_bridge.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"

namespace fusiondesk {
namespace runtime {
namespace qt {

struct QtPeerProfileRuntimeServiceStartOptions
{
    connection::PeerProfileRuntimeServiceStartOptions runtime;
    bool autoApplyAgentListenProfile = true;
    bool startTimer = true;
    std::uint32_t timerIntervalMs = 10;
};

struct QtPeerProfileRuntimeServiceStartResult
{
    bool ok = false;
    connection::PeerProfileRuntimeServiceStartResult runtime;
    bool timerStarted = false;
    std::vector<std::string> messages;
};

struct QtPeerProfileRuntimeApplyResult
{
    bool ok = true;
    std::size_t appliedCompletions = 0;
    std::vector<network::ChannelKey> readyChannels;
    std::vector<network::ChannelKey> listeningChannels;
    std::vector<std::string> messages;
};

struct QtPeerProfileRuntimeServiceSnapshot
{
    bool active = false;
    bool timerRunning = false;
    std::uint32_t timerIntervalMs = 0;
    std::size_t appliedClientCompletions = 0;
    connection::PeerProfileRuntimeServiceSnapshot runtime;
    std::vector<std::string> messages;
};

class QtPeerProfileRuntimeService
{
public:
    QtPeerProfileRuntimeService(QtRuntimeTransportManager& transportManager,
                                network::INetworkRouter& router,
                                protocol::MessageId firstMessageId = 1);
    ~QtPeerProfileRuntimeService();

    QtPeerProfileRuntimeService(const QtPeerProfileRuntimeService&) = delete;
    QtPeerProfileRuntimeService& operator=(const QtPeerProfileRuntimeService&) = delete;

    QtPeerProfileRuntimeServiceStartResult start(
        const QtPeerProfileRuntimeServiceStartOptions& options = {});
    void stop();
    bool active() const;

    connection::PeerProfileRuntimeDispatchResult requestPeerProfile(
        const connection::PeerProfileExchangeRequest& request,
        const connection::PeerProfileRuntimeExchangeOptions& options = {});
    QtPeerProfileRuntimeApplyResult applyCompletedClientProfiles();
    std::size_t expire(std::uint64_t nowUsec);

    QtPeerProfileRuntimeServiceSnapshot snapshot() const;

private:
    static QtTcpChannelProfile connectProfileFrom(
        const connection::PeerProfileConnectChannel& channel);
    static QtTcpListenChannelProfile listenProfileFrom(
        const connection::PeerProfileListenChannel& channel);

    connection::PeerProfileExchangeResult applyAgentListenProfile(
        const connection::PeerProfileExchangeRequest& request,
        const connection::PeerProfileExchangeResult& exchange);
    void remember(const std::vector<std::string>& messages);

private:
    QtRuntimeTransportManager& transportManager_;
    connection::PeerProfileRuntimeService runtime_;
    QtTimerBridge timer_;
    bool active_ = false;
    std::size_t appliedClientCompletions_ = 0;
    std::vector<std::string> messages_;
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_PEER_PROFILE_RUNTIME_SERVICE_H
