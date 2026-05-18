#include <cassert>

#include "fusiondesk/platform/windows/clipboard/windows_clipboard_endpoint.h"

using namespace fusiondesk;
using namespace fusiondesk::platform::windows::clipboard;

namespace {

void watcherPendingStateCanBeConsumedAndNotified()
{
    WindowsClipboardEndpointOptions options;
    options.dryRun = false;
    options.enableNativeClipboardWatcher = false;
    WindowsClipboardEndpoint endpoint(options);

    assert(endpoint.hasPendingClipboardChange());
    endpoint.markClipboardChangeConsumed();
    assert(!endpoint.hasPendingClipboardChange());

    endpoint.notifyNativeClipboardChanged();
    assert(endpoint.hasPendingClipboardChange());

    const WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.nativeChangePending);
    assert(diagnostics.nativeChangeNotifications == 1);
}

void watcherRegistrationIsAttemptedForNativeEndpoint()
{
    WindowsClipboardEndpointOptions options;
    options.dryRun = false;
    WindowsClipboardEndpoint endpoint(options);

    const WindowsClipboardEndpointDiagnostics diagnostics =
        endpoint.diagnostics();
    assert(diagnostics.nativeChangePending);
    assert(endpoint.hasPendingClipboardChange());
}

void dryRunWatcherStateIsConsumableAndSettersNotify()
{
    WindowsClipboardEndpoint endpoint;

    assert(endpoint.hasPendingClipboardChange());
    endpoint.markClipboardChangeConsumed();
    assert(!endpoint.hasPendingClipboardChange());

    endpoint.setDryRunClipboardText("dry-run text");
    assert(endpoint.hasPendingClipboardChange());
    endpoint.markClipboardChangeConsumed();
    assert(!endpoint.hasPendingClipboardChange());
}

} // namespace

int main()
{
    watcherPendingStateCanBeConsumedAndNotified();
    watcherRegistrationIsAttemptedForNativeEndpoint();
    dryRunWatcherStateIsConsumableAndSettersNotify();
    return 0;
}
