#ifndef FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_COORDINATOR_H
#define FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_COORDINATOR_H

#include <string>
#include <vector>

#include "fusiondesk/core/session/session.h"
#include "fusiondesk/runtime/connection/reconnect_orchestration_plan.h"
#include "fusiondesk/runtime/connection/reconnect_teardown_service.h"

namespace fusiondesk {
namespace runtime {
namespace connection {

enum class ReconnectCoordinatorPhase
{
    ResolvePlan,
    StartAgentReplacements,
    ReconnectClientReplacements,
    DispatchClientTeardown,
    DispatchAgentTeardown
};

struct ReconnectReplacementExecutionResult
{
    bool ok = false;
    std::vector<network::ChannelKey> channels;
    std::vector<std::string> messages;
    bool hasSessionReport = false;
    session::ReconnectReport sessionReport;
};

class IReconnectReplacementExecutor
{
public:
    virtual ~IReconnectReplacementExecutor() = default;

    virtual ReconnectReplacementExecutionResult startAgentReplacements(
        const ReconnectOrchestrationSidePlan& agent) = 0;
    virtual ReconnectReplacementExecutionResult reconnectClientReplacements(
        const ReconnectOrchestrationSidePlan& client) = 0;
};

class IReconnectTeardownExecutor
{
public:
    virtual ~IReconnectTeardownExecutor() = default;

    virtual ReconnectTeardownDispatchResult dispatchTeardown(
        const ReconnectOrchestrationSidePlan& sidePlan,
        std::uint32_t timeoutMs) = 0;
};

class ReconnectTeardownServiceExecutor : public IReconnectTeardownExecutor
{
public:
    explicit ReconnectTeardownServiceExecutor(ReconnectTeardownService& service);

    ReconnectTeardownDispatchResult dispatchTeardown(
        const ReconnectOrchestrationSidePlan& sidePlan,
        std::uint32_t timeoutMs) override;

private:
    ReconnectTeardownService& service_;
};

struct ReconnectCoordinatorOptions
{
    bool startAgentReplacements = true;
    bool reconnectClientReplacements = true;
    bool dispatchClientTeardown = true;
    bool dispatchAgentTeardown = false;
    std::uint32_t teardownTimeoutMs = 1000;
};

struct ReconnectCoordinatorStepReport
{
    ReconnectCoordinatorPhase phase = ReconnectCoordinatorPhase::ResolvePlan;
    bool attempted = false;
    bool ok = false;
    std::vector<network::ChannelKey> channels;
    std::vector<std::string> messages;
};

struct ReconnectCoordinatorRunResult
{
    bool ok = false;
    ReconnectOrchestrationPlan plan;
    std::vector<ReconnectCoordinatorStepReport> steps;
    ReconnectTeardownDispatchResult clientTeardown;
    ReconnectTeardownDispatchResult agentTeardown;
    bool hasSessionReport = false;
    session::ReconnectReport sessionReport;
    std::vector<std::string> messages;
};

class ReconnectCoordinator
{
public:
    explicit ReconnectCoordinator(IReconnectReplacementExecutor& replacementExecutor,
                                  IReconnectTeardownExecutor* teardownExecutor = nullptr);

    ReconnectOrchestrationResult plan(const ReconnectOrchestrationRequest& request) const;

    ReconnectCoordinatorRunResult run(
        const ReconnectOrchestrationRequest& request,
        const ReconnectCoordinatorOptions& options = {});

private:
    static ReconnectCoordinatorStepReport resolveStep(
        const ReconnectOrchestrationResult& planResult);
    static ReconnectCoordinatorStepReport replacementStep(
        ReconnectCoordinatorPhase phase,
        const ReconnectReplacementExecutionResult& execution);
    static ReconnectCoordinatorStepReport teardownStep(
        ReconnectCoordinatorPhase phase,
        const ReconnectTeardownDispatchResult& teardown);
    static void appendMessages(ReconnectCoordinatorRunResult& result,
                               const std::vector<std::string>& messages);
    static bool stepOk(const ReconnectCoordinatorStepReport& step);

private:
    IReconnectReplacementExecutor& replacementExecutor_;
    IReconnectTeardownExecutor* teardownExecutor_ = nullptr;
};

} // namespace connection
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_CONNECTION_RECONNECT_COORDINATOR_H
