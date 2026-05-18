#include "fusiondesk/core/diagnostics/diagnostics_sink.h"

namespace fusiondesk {
namespace diagnostics {

void DiagnosticsSink::publish(const DiagnosticEvent& event)
{
    events_.push_back(event);
}

const std::vector<DiagnosticEvent>& DiagnosticsSink::events() const
{
    return events_;
}

std::vector<DiagnosticEvent> DiagnosticsSink::eventsForSession(protocol::SessionId sessionId) const
{
    std::vector<DiagnosticEvent> result;
    for (const DiagnosticEvent& event : events_) {
        if (event.sessionId == sessionId)
            result.push_back(event);
    }
    return result;
}

void DiagnosticsSink::clear()
{
    events_.clear();
}

} // namespace diagnostics
} // namespace fusiondesk
