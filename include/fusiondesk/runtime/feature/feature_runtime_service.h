#ifndef FUSIONDESK_RUNTIME_FEATURE_FEATURE_RUNTIME_SERVICE_H
#define FUSIONDESK_RUNTIME_FEATURE_FEATURE_RUNTIME_SERVICE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fusiondesk/core/protocol/types.h"
#include "fusiondesk/modules/input/input_interfaces.h"
#include "fusiondesk/runtime/feature/feature_runtime_policy.h"

namespace fusiondesk {
namespace session {
class Session;
} // namespace session

namespace modules {
namespace input {
class InputClientModule;
} // namespace input
} // namespace modules

namespace runtime {
namespace feature {

struct FeatureRuntimeServiceOptions
{
    session::Session* session = nullptr;
    std::shared_ptr<modules::input::IInputCapture> inputCapture;
    std::shared_ptr<IFeatureRuntimePolicy> policy;
    std::uint32_t maxInputEventsPerPump = 64;
    bool manageInputCaptureLifecycle = false;
};

struct FeatureRuntimeServiceStartResult
{
    bool ok = false;
    std::vector<std::string> messages;
};

struct FeatureRuntimePumpResult
{
    bool active = false;
    int mouseEventsSent = 0;
    int keyboardEventsSent = 0;
    int inputSendFailures = 0;
    int missingInputModules = 0;
    int policyDenials = 0;
};

struct FeatureRuntimeServiceSnapshot
{
    bool active = false;
    bool inputCaptureAttached = false;
    bool inputCaptureOpenedByService = false;
    std::uint64_t pumpCount = 0;
    int mouseEventsSent = 0;
    int keyboardEventsSent = 0;
    int inputSendFailures = 0;
    int missingInputModules = 0;
    int inputCaptureOpenFailures = 0;
    int policyDenials = 0;
    int auditEvents = 0;
};

class FeatureRuntimeService
{
public:
    explicit FeatureRuntimeService(FeatureRuntimeServiceOptions options);
    ~FeatureRuntimeService();

    FeatureRuntimeServiceStartResult start();
    void stop();

    FeatureRuntimePumpResult pumpOnce();

    FeatureRuntimeServiceSnapshot snapshot() const;

private:
    modules::input::InputClientModule* inputClientModule(
        const std::string& moduleId) const;
    FeatureRuntimePolicyContext makePolicyContext(
        FeatureRuntimeOperation operation,
        const std::string& moduleId) const;
    bool authorizeAndAudit(const FeatureRuntimePolicyContext& context,
                           const FeatureRuntimePolicyDecision& decision,
                           FeatureRuntimePumpResult* pumpResult);
    bool authorizeAndAudit(const FeatureRuntimePolicyContext& context,
                           FeatureRuntimePumpResult* pumpResult = nullptr);

private:
    FeatureRuntimeServiceOptions options_;
    FeatureRuntimeServiceSnapshot snapshot_;
};

} // namespace feature
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_FEATURE_FEATURE_RUNTIME_SERVICE_H
