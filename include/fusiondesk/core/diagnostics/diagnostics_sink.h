#ifndef FUSIONDESK_DIAGNOSTICS_DIAGNOSTICS_SINK_H
#define FUSIONDESK_DIAGNOSTICS_DIAGNOSTICS_SINK_H

#include <cstdint>
#include <string>
#include <vector>

#include "fusiondesk/core/network/channel.h"
#include "fusiondesk/core/protocol/types.h"

namespace fusiondesk {
namespace diagnostics {

enum class DiagnosticSeverity
{
    Debug,
    Info,
    Warning,
    Error
};

struct DiagnosticEvent
{
    protocol::SessionId sessionId = 0;
    protocol::TraceId traceId = 0;
    std::string moduleId;
    network::ChannelKey channel;
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string code;
    std::string message;
    std::uint64_t monotonicTimestampUsec = 0;
    std::string policyVersion;
};

class DiagnosticsSink
{
public:
    void publish(const DiagnosticEvent& event);
    const std::vector<DiagnosticEvent>& events() const;
    std::vector<DiagnosticEvent> eventsForSession(protocol::SessionId sessionId) const;
    void clear();

private:
    std::vector<DiagnosticEvent> events_;
};

} // namespace diagnostics
} // namespace fusiondesk

#endif // FUSIONDESK_DIAGNOSTICS_DIAGNOSTICS_SINK_H
