#ifndef FUSIONDESK_RUNTIME_QT_QT_RECONNECT_ORCHESTRATOR_H
#define FUSIONDESK_RUNTIME_QT_QT_RECONNECT_ORCHESTRATOR_H

#include <string>
#include <vector>

#include "fusiondesk/runtime/connection/reconnect_orchestration_plan.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"

namespace fusiondesk {
namespace runtime {
namespace qt {

struct QtReconnectOrchestrationStartResult
{
    bool ok = false;
    QtTransportConnectResult agentListeners;
    QtReconnectResult clientReconnect;
    std::vector<std::string> messages;
};

class QtReconnectOrchestrator
{
public:
    explicit QtReconnectOrchestrator(QtRuntimeTransportManager& transportManager);

    QtTransportConnectResult startAgentReplacementListeners(
        const connection::ReconnectOrchestrationSidePlan& sidePlan);
    QtReconnectResult reconnectClientReplacements(
        const connection::ReconnectOrchestrationSidePlan& sidePlan);
    QtReconnectOrchestrationStartResult startLocalTcpPlan(
        const connection::ReconnectOrchestrationPlan& plan);

private:
    QtRuntimeTransportManager& transportManager_;
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_RECONNECT_ORCHESTRATOR_H
