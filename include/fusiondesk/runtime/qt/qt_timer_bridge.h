#ifndef FUSIONDESK_RUNTIME_QT_QT_TIMER_BRIDGE_H
#define FUSIONDESK_RUNTIME_QT_QT_TIMER_BRIDGE_H

#include <cstdint>
#include <functional>
#include <memory>

namespace fusiondesk {
namespace runtime {
namespace qt {

class QtTimerBridge
{
public:
    using TickHandler = std::function<void(std::uint64_t nowUsec)>;

    explicit QtTimerBridge(TickHandler handler = {});
    ~QtTimerBridge();

    QtTimerBridge(const QtTimerBridge&) = delete;
    QtTimerBridge& operator=(const QtTimerBridge&) = delete;

    void setTickHandler(TickHandler handler);
    bool start(std::uint32_t intervalMs);
    void stop();
    bool running() const;
    std::uint32_t intervalMs() const;

    static std::uint64_t monotonicNowUsec();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace qt
} // namespace runtime
} // namespace fusiondesk

#endif // FUSIONDESK_RUNTIME_QT_QT_TIMER_BRIDGE_H
