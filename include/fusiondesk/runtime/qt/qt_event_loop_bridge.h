#ifndef FUSIONDESK_RUNTIME_QT_QT_EVENT_LOOP_BRIDGE_H
#define FUSIONDESK_RUNTIME_QT_QT_EVENT_LOOP_BRIDGE_H

#include <cstdint>
#include <functional>

namespace fusiondesk {
namespace runtime {
namespace qt {

class QtEventLoopBridge
{
public:
    using Task = std::function<void()>;
    using Predicate = std::function<bool()>;

    static bool hasApplication();
    static bool post(Task task);
    static void processOnce(std::uint32_t maxTimeMs = 0);
    static bool runUntil(Predicate done,
                         std::uint32_t timeoutMs,
                         std::uint32_t processSliceMs = 10);
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_EVENT_LOOP_BRIDGE_H
