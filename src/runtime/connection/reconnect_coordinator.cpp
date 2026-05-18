#include "fusiondesk/runtime/connection/reconnect_coordinator.h"

#include <utility>

namespace fusiondesk {
namespace runtime {
namespace connection {

namespace {

void appendFailure(ReconnectCoordinatorStepReport& step, std::string message)
{
    step.ok = false;
    step.messages.push_back(std::move(message));
}

void copySessionReport(ReconnectCoordinatorRunResult& result,
                       const ReconnectReplacementExecutionResult& execution)
{
    if (!execution.hasSessionReport)
        return;

    result.hasSessionReport = true;
    result.sessionReport = execution.sessionReport;
}

} // namespace

ReconnectTeardownServiceExecutor::ReconnectTeardownServiceExecutor(
    ReconnectTeardownService& service)
    : service_(service)
{
}

ReconnectTeardownDispatchResult ReconnectTeardownServiceExecutor::dispatchTeardown(
    const ReconnectOrchestrationSidePlan& sidePlan,
    std::uint32_t timeoutMs)
{
    return service_.dispatch(sidePlan, timeoutMs);
}

ReconnectCoordinator::ReconnectCoordinator(
    IReconnectReplacementExecutor& replacementExecutor,
    IReconnectTeardownExecutor* teardownExecutor)
    : replacementExecutor_(replacementExecutor),
      teardownExecutor_(teardownExecutor)
{
}

ReconnectOrchestrationResult ReconnectCoordinator::plan(
    const ReconnectOrchestrationRequest& request) const
{
    return resolveReconnectOrchestrationPlan(request);
}

ReconnectCoordinatorRunResult ReconnectCoordinator::run(
    const ReconnectOrchestrationRequest& request,
    const ReconnectCoordinatorOptions& options)
{
    ReconnectCoordinatorRunResult result;

    const ReconnectOrchestrationResult planned = plan(request);
    result.steps.push_back(resolveStep(planned));
    appendMessages(result, planned.messages);
    if (!planned.ok) {
        result.ok = false;
        return result;
    }
    result.plan = planned.plan;

    if (options.startAgentReplacements) {
        const ReconnectReplacementExecutionResult execution =
            replacementExecutor_.startAgentReplacements(result.plan.agent);
        ReconnectCoordinatorStepReport step = replacementStep(
            ReconnectCoordinatorPhase::StartAgentReplacements,
            execution);
        copySessionReport(result, execution);
        appendMessages(result, step.messages);
        result.steps.push_back(step);
        if (!stepOk(step))
            return result;
    }

    if (options.reconnectClientReplacements) {
        const ReconnectReplacementExecutionResult execution =
            replacementExecutor_.reconnectClientReplacements(result.plan.client);
        ReconnectCoordinatorStepReport step = replacementStep(
            ReconnectCoordinatorPhase::ReconnectClientReplacements,
            execution);
        copySessionReport(result, execution);
        appendMessages(result, step.messages);
        result.steps.push_back(step);
        if (!stepOk(step))
            return result;
    }

    if (options.dispatchClientTeardown) {
        ReconnectCoordinatorStepReport step;
        step.phase = ReconnectCoordinatorPhase::DispatchClientTeardown;
        step.attempted = true;
        if (teardownExecutor_ == nullptr) {
            appendFailure(step, "reconnect coordinator requires a teardown executor");
        } else {
            result.clientTeardown =
                teardownExecutor_->dispatchTeardown(result.plan.client,
                                                    options.teardownTimeoutMs);
            step = teardownStep(ReconnectCoordinatorPhase::DispatchClientTeardown,
                                result.clientTeardown);
        }
        appendMessages(result, step.messages);
        result.steps.push_back(step);
        if (!stepOk(step))
            return result;
    }

    if (options.dispatchAgentTeardown) {
        ReconnectCoordinatorStepReport step;
        step.phase = ReconnectCoordinatorPhase::DispatchAgentTeardown;
        step.attempted = true;
        if (teardownExecutor_ == nullptr) {
            appendFailure(step, "reconnect coordinator requires a teardown executor");
        } else {
            result.agentTeardown =
                teardownExecutor_->dispatchTeardown(result.plan.agent,
                                                    options.teardownTimeoutMs);
            step = teardownStep(ReconnectCoordinatorPhase::DispatchAgentTeardown,
                                result.agentTeardown);
        }
        appendMessages(result, step.messages);
        result.steps.push_back(step);
        if (!stepOk(step))
            return result;
    }

    result.ok = true;
    return result;
}

ReconnectCoordinatorStepReport ReconnectCoordinator::resolveStep(
    const ReconnectOrchestrationResult& planResult)
{
    ReconnectCoordinatorStepReport step;
    step.phase = ReconnectCoordinatorPhase::ResolvePlan;
    step.attempted = true;
    step.ok = planResult.ok;
    step.messages = planResult.messages;
    if (planResult.ok)
        step.channels = planResult.plan.client.degradedChannels;
    return step;
}

ReconnectCoordinatorStepReport ReconnectCoordinator::replacementStep(
    ReconnectCoordinatorPhase phase,
    const ReconnectReplacementExecutionResult& execution)
{
    ReconnectCoordinatorStepReport step;
    step.phase = phase;
    step.attempted = true;
    step.ok = execution.ok;
    step.channels = execution.channels;
    step.messages = execution.messages;
    return step;
}

ReconnectCoordinatorStepReport ReconnectCoordinator::teardownStep(
    ReconnectCoordinatorPhase phase,
    const ReconnectTeardownDispatchResult& teardown)
{
    ReconnectCoordinatorStepReport step;
    step.phase = phase;
    step.attempted = true;
    step.ok = teardown.ok;
    step.messages = teardown.messages;
    step.channels = teardown.summary.completedTargetChannels;
    return step;
}

void ReconnectCoordinator::appendMessages(
    ReconnectCoordinatorRunResult& result,
    const std::vector<std::string>& messages)
{
    result.messages.insert(result.messages.end(), messages.begin(), messages.end());
}

bool ReconnectCoordinator::stepOk(const ReconnectCoordinatorStepReport& step)
{
    return step.attempted && step.ok;
}

} // namespace connection
} // namespace runtime
} // namespace fusiondesk
