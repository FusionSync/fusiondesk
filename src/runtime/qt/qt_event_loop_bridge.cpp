#include "fusiondesk/runtime/qt/qt_event_loop_bridge.h"

#include <utility>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

namespace fusiondesk {
namespace runtime {
namespace qt {

bool QtEventLoopBridge::hasApplication()
{
    return QCoreApplication::instance() != nullptr;
}

bool QtEventLoopBridge::post(Task task)
{
    if (!task || !hasApplication())
        return false;

    QTimer::singleShot(0, [task = std::move(task)]() mutable {
        task();
    });
    return true;
}

void QtEventLoopBridge::processOnce(std::uint32_t maxTimeMs)
{
    if (!hasApplication())
        return;

    if (maxTimeMs == 0) {
        QCoreApplication::processEvents();
        return;
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, static_cast<int>(maxTimeMs));
}

bool QtEventLoopBridge::runUntil(Predicate done,
                                 std::uint32_t timeoutMs,
                                 std::uint32_t processSliceMs)
{
    if (!done || !hasApplication())
        return false;

    if (done())
        return true;

    QElapsedTimer timer;
    timer.start();
    const std::uint32_t slice = processSliceMs == 0 ? 1 : processSliceMs;
    while (!done() && static_cast<std::uint64_t>(timer.elapsed()) < timeoutMs)
        processOnce(slice);

    return done();
}

} // namespace qt
} // namespace runtime
} // namespace fusiondesk
