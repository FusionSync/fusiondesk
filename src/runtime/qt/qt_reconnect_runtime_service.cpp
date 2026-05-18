#include "fusiondesk/runtime/qt/qt_reconnect_runtime_service.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace qt {

namespace {

void appendFailure(QtReconnectRuntimeServiceStartResult& result, std::string message)
{
    result.ok = false;
    result.messages.push_back(std::move(message));
}

} // namespace

QtReconnectRuntimeService::QtReconnectRuntimeService(
    QtRuntimeTransportManager& transportManager,
    network::INetworkRouter& router,
    protocol::MessageId firstMessageId)
    : executor_(transportManager),
      runtime_(executor_, router, transportManager, firstMessageId),
      timer_([this](std::uint64_t nowUsec) {
          runtime_.expire(nowUsec);
      })
{
}

QtReconnectRuntimeService::~QtReconnectRuntimeService()
{
    stop();
}

QtReconnectRuntimeServiceStartResult QtReconnectRuntimeService::start(
    const QtReconnectRuntimeServiceStartOptions& options)
{
    QtReconnectRuntimeServiceStartResult result;
    if (active_) {
        appendFailure(result, "qt reconnect runtime service is already active");
        remember(result.messages);
        return result;
    }

    result.runtime = runtime_.start(options.runtime);
    if (!result.runtime.ok) {
        result.messages.insert(result.messages.end(),
                               result.runtime.messages.begin(),
                               result.runtime.messages.end());
        if (result.messages.empty())
            appendFailure(result, "reconnect runtime service start failed");
        remember(result.messages);
        return result;
    }

    if (options.startTimer) {
        result.timerStarted = timer_.start(options.timerIntervalMs);
        if (!result.timerStarted) {
            runtime_.stop();
            appendFailure(result, "qt reconnect runtime service timer start failed");
            remember(result.messages);
            return result;
        }
    }

    active_ = true;
    result.ok = true;
    return result;
}

void QtReconnectRuntimeService::stop()
{
    timer_.stop();
    runtime_.stop();
    active_ = false;
}

bool QtReconnectRuntimeService::active() const
{
    return active_ && runtime_.active();
}

connection::ReconnectCoordinatorRunResult QtReconnectRuntimeService::run(
    const connection::ReconnectOrchestrationRequest& request,
    const connection::ReconnectCoordinatorOptions& options)
{
    connection::ReconnectCoordinatorRunResult result = runtime_.run(request, options);
    remember(result.messages);
    return result;
}

std::size_t QtReconnectRuntimeService::expire(std::uint64_t nowUsec)
{
    return runtime_.expire(nowUsec);
}

QtReconnectRuntimeServiceSnapshot QtReconnectRuntimeService::snapshot() const
{
    QtReconnectRuntimeServiceSnapshot result;
    result.active = active();
    result.timerRunning = timer_.running();
    result.timerIntervalMs = timer_.intervalMs();
    result.runtime = runtime_.snapshot();
    result.messages = messages_;
    return result;
}

void QtReconnectRuntimeService::remember(const std::vector<std::string>& messages)
{
    messages_.insert(messages_.end(), messages.begin(), messages.end());
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
