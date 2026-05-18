#include "fusiondesk/runtime/qt/qt_peer_profile_runtime_service.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace qt {

namespace {

void appendFailure(QtPeerProfileRuntimeServiceStartResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendFailure(QtPeerProfileRuntimeApplyResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

void appendMessages(std::vector<std::string>& target,
                    const std::vector<std::string>& messages)
{
    target.insert(target.end(), messages.begin(), messages.end());
}

} // namespace

QtPeerProfileRuntimeService::QtPeerProfileRuntimeService(
    QtRuntimeTransportManager& transportManager,
    network::INetworkRouter& router,
    protocol::MessageId firstMessageId)
    : transportManager_(transportManager),
      runtime_(router, firstMessageId),
      timer_([this](std::uint64_t nowUsec) {
          runtime_.expire(nowUsec);
      })
{
}

QtPeerProfileRuntimeService::~QtPeerProfileRuntimeService()
{
    stop();
}

QtPeerProfileRuntimeServiceStartResult QtPeerProfileRuntimeService::start(
    const QtPeerProfileRuntimeServiceStartOptions& options)
{
    QtPeerProfileRuntimeServiceStartResult result;
    if (active_) {
        appendFailure(result, "qt peer profile runtime service is already active");
        remember(result.messages);
        return result;
    }

    connection::PeerProfileRuntimeServiceStartOptions runtimeOptions = options.runtime;
    if (options.autoApplyAgentListenProfile && runtimeOptions.startResponder) {
        const auto userHandler = runtimeOptions.responder.exchangeHandler;
        runtimeOptions.responder.exchangeHandler =
            [this, userHandler](const connection::PeerProfileExchangeRequest& request,
                                const connection::PeerProfileExchangeResult& exchange) {
                connection::PeerProfileExchangeResult handled = userHandler
                    ? userHandler(request, exchange)
                    : exchange;
                if (!handled.ok)
                    return handled;
                return applyAgentListenProfile(request, handled);
            };
    }

    result.runtime = runtime_.start(runtimeOptions);
    if (!result.runtime.ok) {
        appendMessages(result.messages, result.runtime.messages);
        if (result.messages.empty())
            appendFailure(result, "peer profile runtime service start failed");
        remember(result.messages);
        return result;
    }

    if (options.startTimer) {
        result.timerStarted = timer_.start(options.timerIntervalMs);
        if (!result.timerStarted) {
            runtime_.stop();
            appendFailure(result, "qt peer profile runtime service timer start failed");
            remember(result.messages);
            return result;
        }
    }

    active_ = true;
    result.ok = true;
    return result;
}

void QtPeerProfileRuntimeService::stop()
{
    timer_.stop();
    runtime_.stop();
    active_ = false;
}

bool QtPeerProfileRuntimeService::active() const
{
    return active_ && runtime_.active();
}

connection::PeerProfileRuntimeDispatchResult
QtPeerProfileRuntimeService::requestPeerProfile(
    const connection::PeerProfileExchangeRequest& request,
    const connection::PeerProfileRuntimeExchangeOptions& options)
{
    connection::PeerProfileRuntimeDispatchResult result =
        runtime_.requestPeerProfile(request, options);
    remember(result.messages);
    return result;
}

QtPeerProfileRuntimeApplyResult
QtPeerProfileRuntimeService::applyCompletedClientProfiles()
{
    QtPeerProfileRuntimeApplyResult result;
    const connection::PeerProfileRuntimeServiceSnapshot runtimeSnapshot =
        runtime_.snapshot();
    const std::vector<connection::PeerProfileRuntimeCompletion>& completions =
        runtimeSnapshot.completions;

    while (appliedClientCompletions_ < completions.size()) {
        const connection::PeerProfileRuntimeCompletion& completion =
            completions[appliedClientCompletions_++];
        ++result.appliedCompletions;

        if (!completion.ok) {
            appendFailure(result, completion.messages.empty()
                                      ? "peer profile completion failed"
                                      : completion.messages.front());
            continue;
        }

        const connection::PeerProfileSide& client = completion.exchange.pair.client;
        if (client.sessionId == 0) {
            appendFailure(result, "peer profile client session id is required");
            continue;
        }

        std::vector<QtTcpChannelProfile> profiles;
        profiles.reserve(client.tcpChannels.size());
        for (const connection::PeerProfileConnectChannel& channel : client.tcpChannels)
            profiles.push_back(connectProfileFrom(channel));

        if (profiles.empty())
            continue;

        const QtTransportConnectResult connected =
            transportManager_.connectTcpChannels(client.sessionId, profiles);
        result.readyChannels.insert(result.readyChannels.end(),
                                    connected.readyChannels.begin(),
                                    connected.readyChannels.end());
        appendMessages(result.messages, connected.messages);
        if (!connected.ok)
            result.ok = false;
    }

    remember(result.messages);
    return result;
}

std::size_t QtPeerProfileRuntimeService::expire(std::uint64_t nowUsec)
{
    return runtime_.expire(nowUsec);
}

QtPeerProfileRuntimeServiceSnapshot QtPeerProfileRuntimeService::snapshot() const
{
    QtPeerProfileRuntimeServiceSnapshot result;
    result.active = active();
    result.timerRunning = timer_.running();
    result.timerIntervalMs = timer_.intervalMs();
    result.appliedClientCompletions = appliedClientCompletions_;
    result.runtime = runtime_.snapshot();
    result.messages = messages_;
    return result;
}

QtTcpChannelProfile QtPeerProfileRuntimeService::connectProfileFrom(
    const connection::PeerProfileConnectChannel& channel)
{
    QtTcpChannelProfile profile;
    profile.spec = channel.spec;
    profile.endpoint = channel.endpoint;
    profile.ready.endpoint = channel.readyEndpoint;
    return profile;
}

QtTcpListenChannelProfile QtPeerProfileRuntimeService::listenProfileFrom(
    const connection::PeerProfileListenChannel& channel)
{
    QtTcpListenChannelProfile profile;
    profile.spec = channel.spec;
    profile.endpoint = channel.endpoint;
    profile.ready.endpoint = channel.readyEndpoint;
    return profile;
}

connection::PeerProfileExchangeResult
QtPeerProfileRuntimeService::applyAgentListenProfile(
    const connection::PeerProfileExchangeRequest&,
    const connection::PeerProfileExchangeResult& exchange)
{
    connection::PeerProfileExchangeResult result = exchange;
    const connection::PeerProfileSide& agent = exchange.pair.agent;
    if (agent.sessionId == 0) {
        result.ok = false;
        result.messages.push_back("peer profile agent session id is required");
        return result;
    }

    std::vector<QtTcpListenChannelProfile> profiles;
    profiles.reserve(agent.tcpListenChannels.size());
    for (const connection::PeerProfileListenChannel& channel : agent.tcpListenChannels)
        profiles.push_back(listenProfileFrom(channel));

    if (profiles.empty())
        return result;

    const QtTransportConnectResult listened =
        transportManager_.listenTcpChannels(agent.sessionId, profiles);
    appendMessages(result.messages, listened.messages);
    if (!listened.ok)
        result.ok = false;
    return result;
}

void QtPeerProfileRuntimeService::remember(const std::vector<std::string>& messages)
{
    appendMessages(messages_, messages);
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
