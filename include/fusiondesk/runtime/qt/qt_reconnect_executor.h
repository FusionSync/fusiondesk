#ifndef FUSIONDESK_RUNTIME_QT_QT_RECONNECT_EXECUTOR_H
#define FUSIONDESK_RUNTIME_QT_QT_RECONNECT_EXECUTOR_H

#include <memory>

#include "fusiondesk/runtime/connection/reconnect_coordinator.h"
#include "fusiondesk/runtime/qt/qt_reconnect_orchestrator.h"

namespace fusiondesk {
namespace runtime {
namespace qt {

class QtReconnectExecutor : public connection::IReconnectReplacementExecutor
{
public:
    explicit QtReconnectExecutor(QtRuntimeTransportManager& transportManager);
    // Non-owning injection hook; orchestrator must outlive this executor.
    explicit QtReconnectExecutor(QtReconnectOrchestrator& orchestrator);

    connection::ReconnectReplacementExecutionResult startAgentReplacements(
        const connection::ReconnectOrchestrationSidePlan& agent) override;
    connection::ReconnectReplacementExecutionResult reconnectClientReplacements(
        const connection::ReconnectOrchestrationSidePlan& client) override;

private:
    std::unique_ptr<QtReconnectOrchestrator> ownedOrchestrator_;
    QtReconnectOrchestrator* orchestrator_ = nullptr;
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_RECONNECT_EXECUTOR_H
