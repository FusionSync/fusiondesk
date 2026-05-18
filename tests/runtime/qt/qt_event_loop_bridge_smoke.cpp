#include <cassert>

#include <QCoreApplication>

#include "fusiondesk/runtime/qt/qt_event_loop_bridge.h"

using namespace fusiondesk;

namespace {

void postsAndRunsUntilTaskCompletes()
{
    assert(runtime::qt::QtEventLoopBridge::hasApplication());

    int calls = 0;
    assert(runtime::qt::QtEventLoopBridge::post([&calls]() {
        ++calls;
    }));

    assert(runtime::qt::QtEventLoopBridge::runUntil([&calls]() {
        return calls == 1;
    }, 1000));
}

void rejectsMissingWork()
{
    assert(!runtime::qt::QtEventLoopBridge::post({}));
    assert(!runtime::qt::QtEventLoopBridge::runUntil({}, 10));
}

void immediatePredicateDoesNotNeedPostedWork()
{
    assert(runtime::qt::QtEventLoopBridge::runUntil([]() {
        return true;
    }, 1));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    postsAndRunsUntilTaskCompletes();
    rejectsMissingWork();
    immediatePredicateDoesNotNeedPostedWork();
    return 0;
}
