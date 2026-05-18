#include "fusiondesk/runtime/qt/qt_timer_bridge.h"

#include <utility>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace fusiondesk {
namespace runtime {
namespace qt {

namespace {

QElapsedTimer& steadyTimer()
{
    static QElapsedTimer timer;
    static const bool started = []() {
        timer.start();
        return true;
    }();
    (void)started;
    return timer;
}

} // namespace

struct QtTimerBridge::Impl
{
    Impl()
    {
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, &timer, [this]() {
            if (handler)
                handler(QtTimerBridge::monotonicNowUsec());
        });
    }

    QTimer timer;
    TickHandler handler;
    std::uint32_t intervalMs = 0;
};

QtTimerBridge::QtTimerBridge(TickHandler handler)
    : impl_(std::make_unique<Impl>())
{
    impl_->handler = std::move(handler);
}

QtTimerBridge::~QtTimerBridge() = default;

void QtTimerBridge::setTickHandler(TickHandler handler)
{
    impl_->handler = std::move(handler);
}

bool QtTimerBridge::start(std::uint32_t intervalMs)
{
    if (intervalMs == 0)
        return false;

    impl_->intervalMs = intervalMs;
    impl_->timer.start(static_cast<int>(intervalMs));
    return impl_->timer.isActive();
}

void QtTimerBridge::stop()
{
    impl_->timer.stop();
}

bool QtTimerBridge::running() const
{
    return impl_->timer.isActive();
}

std::uint32_t QtTimerBridge::intervalMs() const
{
    return impl_->intervalMs;
}

std::uint64_t QtTimerBridge::monotonicNowUsec()
{
    return static_cast<std::uint64_t>(steadyTimer().nsecsElapsed() / 1000);
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
