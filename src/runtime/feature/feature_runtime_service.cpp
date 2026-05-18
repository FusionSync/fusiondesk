#include "fusiondesk/runtime/feature/feature_runtime_service.h"

#include <utility>

#include "fusiondesk/core/module/module_host.h"
#include "fusiondesk/core/session/session.h"
#include "fusiondesk/modules/input/input_modules.h"

namespace fusiondesk {
namespace runtime {
namespace feature {

FeatureRuntimeService::FeatureRuntimeService(FeatureRuntimeServiceOptions options)
    : options_(std::move(options))
{
    if (options_.policy == nullptr)
        options_.policy = std::make_shared<AllowAllFeatureRuntimePolicy>();

    snapshot_.inputCaptureAttached = options_.inputCapture != nullptr;
}

FeatureRuntimeService::~FeatureRuntimeService()
{
    stop();
}

FeatureRuntimeServiceStartResult FeatureRuntimeService::start()
{
    FeatureRuntimeServiceStartResult result;
    if (snapshot_.active) {
        result.ok = true;
        return result;
    }

    if (options_.session == nullptr) {
        result.messages.push_back("feature runtime service requires a session");
        return result;
    }
    if (options_.session->moduleHost() == nullptr) {
        result.messages.push_back("feature runtime service requires a module host");
        return result;
    }

    if (options_.manageInputCaptureLifecycle && options_.inputCapture != nullptr) {
        if (!options_.inputCapture->open()) {
            ++snapshot_.inputCaptureOpenFailures;
            result.messages.push_back("input capture open failed");
            return result;
        }
        snapshot_.inputCaptureOpenedByService = true;
    }

    snapshot_.active = true;
    result.ok = true;
    return result;
}

void FeatureRuntimeService::stop()
{
    if (snapshot_.inputCaptureOpenedByService && options_.inputCapture != nullptr) {
        options_.inputCapture->close();
        snapshot_.inputCaptureOpenedByService = false;
    }

    snapshot_.active = false;
}

FeatureRuntimePumpResult FeatureRuntimeService::pumpOnce()
{
    FeatureRuntimePumpResult result;
    result.active = snapshot_.active;
    if (!snapshot_.active || options_.session == nullptr)
        return result;

    ++snapshot_.pumpCount;
    if (options_.inputCapture != nullptr &&
        options_.session->role() == session::SessionRole::Client) {
        const std::uint32_t maxEvents =
            options_.maxInputEventsPerPump == 0 ? 64 : options_.maxInputEventsPerPump;
        std::uint32_t pumped = 0;
        while (pumped < maxEvents) {
            bool polled = false;

            modules::input::MouseInputEvent mouse;
            if (options_.inputCapture->pollMouseEvent(mouse)) {
                polled = true;
                ++pumped;
                modules::input::InputClientModule* module =
                    inputClientModule("input.mouse.client");
                FeatureRuntimePolicyContext context = makePolicyContext(
                    FeatureRuntimeOperation::InputMouseEvent,
                    "input.mouse.client");
                context.sequence = mouse.sequence;
                if (authorizeAndAudit(context, &result)) {
                    if (module == nullptr) {
                        ++snapshot_.missingInputModules;
                        ++snapshot_.inputSendFailures;
                        ++result.missingInputModules;
                        ++result.inputSendFailures;
                    } else if (module->sendMouseEvent(mouse)) {
                        ++snapshot_.mouseEventsSent;
                        ++result.mouseEventsSent;
                    } else {
                        ++snapshot_.inputSendFailures;
                        ++result.inputSendFailures;
                    }
                }
            }

            if (pumped >= maxEvents)
                break;

            modules::input::KeyboardInputEvent keyboard;
            if (options_.inputCapture->pollKeyboardEvent(keyboard)) {
                polled = true;
                ++pumped;
                modules::input::InputClientModule* module =
                    inputClientModule("input.keyboard.client");
                FeatureRuntimePolicyContext context = makePolicyContext(
                    FeatureRuntimeOperation::InputKeyboardEvent,
                    "input.keyboard.client");
                context.sequence = keyboard.sequence;
                if (!authorizeAndAudit(context, &result)) {
                    continue;
                }

                if (module == nullptr) {
                    ++snapshot_.missingInputModules;
                    ++snapshot_.inputSendFailures;
                    ++result.missingInputModules;
                    ++result.inputSendFailures;
                } else if (module->sendKeyboardEvent(keyboard)) {
                    ++snapshot_.keyboardEventsSent;
                    ++result.keyboardEventsSent;
                } else {
                    ++snapshot_.inputSendFailures;
                    ++result.inputSendFailures;
                }
            }

            if (!polled)
                break;
        }
    }

    return result;
}

FeatureRuntimeServiceSnapshot FeatureRuntimeService::snapshot() const
{
    return snapshot_;
}

modules::input::InputClientModule* FeatureRuntimeService::inputClientModule(
    const std::string& moduleId) const
{
    if (options_.session == nullptr || options_.session->moduleHost() == nullptr)
        return nullptr;

    return dynamic_cast<modules::input::InputClientModule*>(
        options_.session->moduleHost()->module(moduleId));
}

FeatureRuntimePolicyContext FeatureRuntimeService::makePolicyContext(
    FeatureRuntimeOperation operation,
    const std::string& moduleId) const
{
    FeatureRuntimePolicyContext context;
    context.operation = operation;
    context.moduleId = moduleId;

    if (options_.session != nullptr) {
        const session::SessionContext& sessionContext = options_.session->context();
        context.sessionId = sessionContext.sessionId;
        context.traceId = sessionContext.traceId;
        context.role = sessionContext.role;
        context.policyVersion = sessionContext.policyVersion;
    }

    return context;
}

bool FeatureRuntimeService::authorizeAndAudit(
    const FeatureRuntimePolicyContext& context,
    const FeatureRuntimePolicyDecision& decision,
    FeatureRuntimePumpResult* pumpResult)
{
    const bool shouldAudit = !decision.allowed || decision.auditRequired;
    if (shouldAudit && options_.policy != nullptr) {
        FeatureRuntimeAuditEvent event;
        event.context = context;
        event.allowed = decision.allowed;
        event.responseStatus = decision.responseStatus;
        event.reason = decision.reason;
        options_.policy->audit(event);
        ++snapshot_.auditEvents;
    }

    if (!decision.allowed) {
        ++snapshot_.policyDenials;
        if (pumpResult != nullptr)
            ++pumpResult->policyDenials;
        return false;
    }

    return true;
}

bool FeatureRuntimeService::authorizeAndAudit(
    const FeatureRuntimePolicyContext& context,
    FeatureRuntimePumpResult* pumpResult)
{
    const FeatureRuntimePolicyDecision decision =
        options_.policy == nullptr ?
        FeatureRuntimePolicyDecision::allow(false) :
        options_.policy->authorize(context);
    return authorizeAndAudit(context, decision, pumpResult);
}

} // namespace feature
} // namespace runtime
} // namespace fusiondesk
