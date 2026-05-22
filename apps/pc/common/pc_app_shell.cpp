#include "pc_app_shell.h"
#include "pc_clipboard_drag_mapper.h"
#include "pc_clipboard_file_save.h"
#include "pc_clipboard_policy_file.h"
#include "pc_clipboard_shell.h"
#include "pc_display_diagnostics.h"
#include "pc_option_registry.h"
#include "pc_profile_dependencies.h"
#include "pc_profile_options.h"
#include "pc_runtime_exchange_shell.h"
#include "pc_session_diagnostics.h"
#include "pc_shell_options.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#if defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
#include <QGuiApplication>
#endif
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
#include <QApplication>
#endif

#include "fusiondesk/core/module/module_host.h"
#if defined(FUSIONDESK_PC_HAS_QT_IMAGE_DISPLAY)
#include "fusiondesk/adapters/qt/display/qt_image_display_renderer.h"
#endif
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
#include "fusiondesk/adapters/qt/display/qt_image_display_window.h"
#endif
#include "fusiondesk/modules/display/display_modules.h"
#include "fusiondesk/modules/test/test_echo_modules.h"
#include "fusiondesk/runtime/qt/qt_event_loop_bridge.h"
#include "fusiondesk/runtime/qt/qt_peer_profile_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_reconnect_runtime_service.h"
#include "fusiondesk/runtime/qt/qt_timer_bridge.h"
#include "fusiondesk/runtime/qt/qt_transport_profile.h"
#include "fusiondesk/runtime/connection/module_inventory_runtime_service.h"
#include "fusiondesk/runtime/display/display_runtime_service.h"
#include "fusiondesk/runtime/feature/clipboard_runtime_service.h"
#include "fusiondesk/runtime/feature/feature_runtime_service.h"
#include "fusiondesk/runtime/runtime_host.h"
#include "fusiondesk/runtime/session/display_product_health_presenter.h"
#include "fusiondesk/runtime/session/session_mainline.h"
#include "fusiondesk/runtime/session/session_runtime_diagnostics.h"

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

void appendUniqueChannels(std::vector<network::ChannelKey>& channels,
                          const std::vector<network::ChannelKey>& values)
{
    for (const network::ChannelKey& key : values) {
        if (std::find(channels.begin(), channels.end(), key) == channels.end())
            channels.push_back(key);
    }
}

modules::display::DisplayAgentModule* displayAgentModule(session::Session& session)
{
    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr)
        return nullptr;

    return dynamic_cast<modules::display::DisplayAgentModule*>(
        host->module("display.screen.agent"));
}

modules::display::DisplayClientModule* displayClientModule(session::Session& session)
{
    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr)
        return nullptr;

    return dynamic_cast<modules::display::DisplayClientModule*>(
        host->module("display.screen.client"));
}

bool displayFrameObserved(session::Session& session, PcShellRole role)
{
    if (role == PcShellRole::Agent) {
        modules::display::DisplayAgentModule* agent = displayAgentModule(session);
        return agent != nullptr && agent->snapshot().sentFrames > 0;
    }

    modules::display::DisplayClientModule* client = displayClientModule(session);
    if (client == nullptr)
        return false;

    const modules::display::DisplayClientSnapshot snapshot = client->snapshot();
    return snapshot.renderedFrames > 0 && snapshot.renderErrors == 0;
}

std::string roleScopedProfileModuleId(const std::string& moduleId, PcShellRole role)
{
    const char* suffix = role == PcShellRole::Client ? ".client" : ".agent";
    if (moduleId == "display.screen" ||
        moduleId == "input.mouse" ||
        moduleId == "input.keyboard" ||
        moduleId == "clipboard.redirect" ||
        moduleId == "test.echo") {
        return moduleId + suffix;
    }
    return moduleId;
}

bool requiredProfileModulesMounted(int argc,
                                   char** argv,
                                   session::Session& session,
                                   PcShellRole role)
{
    const std::vector<std::string> required =
        optionValues(argc, argv, "--require-profile-module");
    if (required.empty())
        return true;

    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr)
        return false;

    for (const std::string& moduleId : required) {
        const std::string resolved = roleScopedProfileModuleId(moduleId, role);
        if (host->module(resolved) == nullptr) {
            writeShellError("required profile module was not mounted: " + resolved);
            return false;
        }
    }

    return true;
}

int displayFrameCount(session::Session& session, PcShellRole role)
{
    if (role == PcShellRole::Agent) {
        modules::display::DisplayAgentModule* agent = displayAgentModule(session);
        return agent == nullptr ? 0 : agent->snapshot().sentFrames;
    }

    modules::display::DisplayClientModule* client = displayClientModule(session);
    return client == nullptr ? 0 : client->snapshot().renderedFrames;
}

std::string displayModuleDiagnostics(session::Session& session, PcShellRole role)
{
    if (role == PcShellRole::Agent) {
        modules::display::DisplayAgentModule* agent = displayAgentModule(session);
        return agent == nullptr ? "display.agent missing" : agent->diagnostics();
    }

    modules::display::DisplayClientModule* client = displayClientModule(session);
    return client == nullptr ? "display.client missing" : client->diagnostics();
}

modules::test::TestEchoModule* testEchoModule(session::Session& session,
                                              PcShellRole role)
{
    module::ModuleHost* host = session.moduleHost();
    if (host == nullptr)
        return nullptr;

    const std::string moduleId =
        role == PcShellRole::Client ? "test.echo.client" : "test.echo.agent";
    return dynamic_cast<modules::test::TestEchoModule*>(host->module(moduleId));
}

bool waitForTestEchoResponse(modules::test::TestEchoModule& echo,
                             runtime::qt::QtRuntimeTransportManager& transportManager,
                             protocol::SessionId sessionId,
                             int waitMs,
                             int previousResponses,
                             int previousErrors)
{
    if (waitMs <= 0)
        return echo.snapshot().responsesReceived > previousResponses;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < waitMs) {
        pollSessionTransports(transportManager, sessionId);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        const modules::test::TestEchoSnapshot snapshot = echo.snapshot();
        if (snapshot.responsesReceived > previousResponses)
            return true;
        if (snapshot.errorsReceived > previousErrors)
            return false;
    }

    return echo.snapshot().responsesReceived > previousResponses;
}

bool sendTestEchoIfRequested(int argc,
                             char** argv,
                             session::Session& session,
                             PcShellRole role,
                             runtime::qt::QtRuntimeTransportManager& transportManager,
                             protocol::SessionId sessionId)
{
    const std::vector<std::string> payloads =
        optionValues(argc, argv, "--send-test-echo");
    if (payloads.empty())
        return true;

    modules::test::TestEchoModule* echo = testEchoModule(session, role);
    if (echo == nullptr) {
        writeShellError("test echo module is not mounted for this role");
        return false;
    }

    const bool requireResponse = hasArg(argc, argv, "--require-test-echo-response");
    const std::uint32_t timeoutMs = static_cast<std::uint32_t>(
        intOptionValue(argc, argv, "--test-echo-timeout-ms", 1000));
    const int waitMs = intOptionValue(argc, argv, "--test-echo-wait-ms", 3000);

    for (const std::string& payload : payloads) {
        const modules::test::TestEchoSnapshot before = echo->snapshot();
        const protocol::MessageId messageId = echo->sendPing(payload, timeoutMs);
        if (messageId == 0) {
            writeShellError("test echo send failed");
            return false;
        }

        std::cout << "test.echo.sent"
                  << " module=" << echo->manifest().moduleId
                  << " messageId=" << messageId
                  << " payload=" << payload
                  << std::endl;

        if (!requireResponse)
            continue;

        if (!waitForTestEchoResponse(*echo,
                                     transportManager,
                                     sessionId,
                                     waitMs,
                                     before.responsesReceived,
                                     before.errorsReceived)) {
            const modules::test::TestEchoSnapshot snapshot = echo->snapshot();
            writeShellError("test echo response wait failed: " + echo->diagnostics());
            std::cout << "test.echo.response"
                      << " ok=false"
                      << " module=" << snapshot.moduleId
                      << " responses=" << snapshot.responsesReceived
                      << " errors=" << snapshot.errorsReceived
                      << std::endl;
            return false;
        }

        const modules::test::TestEchoSnapshot snapshot = echo->snapshot();
        std::cout << "test.echo.response"
                  << " ok=true"
                  << " module=" << snapshot.moduleId
                  << " responseTo=" << snapshot.lastResponseTo
                  << " responses=" << snapshot.responsesReceived
                  << std::endl;
    }

    return true;
}

bool waitForClipboardTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (!clipboardTextRequirementRequested(argc, argv))
        return true;

    const int waitMs =
        intOptionValue(argc, argv, "--clipboard-require-wait-ms", 0);
    if (waitMs <= 0)
        return verifyClipboardTextRequirement(argc, argv, endpoint);

    QElapsedTimer timer;
    timer.start();
    std::string lastError;
    while (timer.elapsed() < waitMs) {
        if (clipboardTextRequirementSatisfied(
                argc,
                argv,
                endpoint,
                &lastError)) {
            return true;
        }
        runBoundedEventLoop(transportManager,
                            sessionId,
                            50,
                            featureService,
                            clipboardService);
    }

    if (clipboardTextRequirementSatisfied(argc, argv, endpoint, &lastError))
        return true;

    writeShellError(lastError.empty()
                        ? "clipboard text requirement wait timed out"
                        : lastError);
    return false;
}

bool waitForClipboardImagePngRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (!clipboardImagePngRequirementRequested(argc, argv))
        return true;

    const int waitMs =
        intOptionValue(argc, argv, "--clipboard-require-wait-ms", 0);
    if (waitMs <= 0)
        return verifyClipboardImagePngRequirement(argc, argv, endpoint);

    QElapsedTimer timer;
    timer.start();
    std::string lastError;
    while (timer.elapsed() < waitMs) {
        if (clipboardImagePngRequirementSatisfied(argc,
                                                  argv,
                                                  endpoint,
                                                  &lastError)) {
            return true;
        }
        runBoundedEventLoop(transportManager,
                            sessionId,
                            50,
                            featureService,
                            clipboardService);
    }

    if (clipboardImagePngRequirementSatisfied(argc,
                                              argv,
                                              endpoint,
                                              &lastError)) {
        return true;
    }

    writeShellError(lastError.empty()
                        ? "clipboard image/png requirement wait timed out"
                        : lastError);
    return false;
}

bool waitForClipboardFormattedTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (!clipboardFormattedTextRequirementRequested(argc, argv))
        return true;

    const int waitMs =
        intOptionValue(argc, argv, "--clipboard-require-wait-ms", 0);
    if (waitMs <= 0)
        return verifyClipboardFormattedTextRequirement(argc, argv, endpoint);

    QElapsedTimer timer;
    timer.start();
    std::string lastError;
    while (timer.elapsed() < waitMs) {
        if (clipboardFormattedTextRequirementSatisfied(argc,
                                                       argv,
                                                       endpoint,
                                                       &lastError)) {
            return true;
        }
        runBoundedEventLoop(transportManager,
                            sessionId,
                            50,
                            featureService,
                            clipboardService);
    }

    if (clipboardFormattedTextRequirementSatisfied(argc,
                                                   argv,
                                                   endpoint,
                                                   &lastError)) {
        return true;
    }

    writeShellError(lastError.empty()
                        ? "clipboard formatted text requirement wait timed out"
                        : lastError);
    return false;
}

bool waitForClipboardFileTextRequirement(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (!clipboardFileTextRequirementRequested(argc, argv))
        return true;

    const int waitMs =
        intOptionValue(argc, argv, "--clipboard-require-wait-ms", 0);
    if (waitMs <= 0) {
        return verifyClipboardFileTextRequirement(argc,
                                                  argv,
                                                  session,
                                                  remoteReader);
    }

    QElapsedTimer timer;
    timer.start();
    std::string lastError;
    while (timer.elapsed() < waitMs) {
        if (clipboardFileTextRequirementSatisfied(argc,
                                                  argv,
                                                  session,
                                                  remoteReader,
                                                  &lastError)) {
            return true;
        }
        runBoundedEventLoop(transportManager,
                            sessionId,
                            50,
                            featureService,
                            clipboardService);
    }

    if (clipboardFileTextRequirementSatisfied(argc,
                                              argv,
                                              session,
                                              remoteReader,
                                              &lastError)) {
        return true;
    }

    writeShellError(lastError.empty()
                        ? "clipboard file text requirement wait timed out"
                        : lastError);
    return false;
}

bool waitForClipboardEndpointFileTextRequirement(
    int argc,
    char** argv,
    const std::shared_ptr<modules::clipboard::IClipboardEndpoint>& endpoint,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (!clipboardEndpointFileTextRequirementRequested(argc, argv))
        return true;

    const int waitMs =
        intOptionValue(argc, argv, "--clipboard-require-wait-ms", 0);
    if (waitMs <= 0) {
        return verifyClipboardEndpointFileTextRequirement(argc,
                                                          argv,
                                                          endpoint);
    }

    QElapsedTimer timer;
    timer.start();
    std::string lastError;
    while (timer.elapsed() < waitMs) {
        if (clipboardEndpointFileTextRequirementSatisfied(argc,
                                                          argv,
                                                          endpoint,
                                                          &lastError)) {
            return true;
        }
        runBoundedEventLoop(transportManager,
                            sessionId,
                            50,
                            featureService,
                            clipboardService);
    }

    if (clipboardEndpointFileTextRequirementSatisfied(argc,
                                                      argv,
                                                      endpoint,
                                                      &lastError)) {
        return true;
    }

    writeShellError(lastError.empty()
                        ? "clipboard endpoint file text requirement wait timed out"
                        : lastError);
    return false;
}

bool waitForClipboardFileSave(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (!clipboardSaveFilesRequested(argc, argv))
        return true;

    const int waitMs =
        intOptionValue(argc, argv, "--clipboard-require-wait-ms", 0);
    if (waitMs <= 0) {
        std::string errorMessage;
        if (saveClipboardRemoteFiles(argc,
                                     argv,
                                     session,
                                     remoteReader,
                                     &errorMessage)) {
            return true;
        }
        writeShellError(errorMessage);
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    std::string lastError;
    while (timer.elapsed() < waitMs) {
        if (saveClipboardRemoteFiles(argc,
                                     argv,
                                     session,
                                     remoteReader,
                                     &lastError)) {
            return true;
        }
        runBoundedEventLoop(transportManager,
                            sessionId,
                            50,
                            featureService,
                            clipboardService);
    }

    if (saveClipboardRemoteFiles(argc,
                                 argv,
                                 session,
                                 remoteReader,
                                 &lastError)) {
        return true;
    }

    writeShellError(lastError.empty()
                        ? "clipboard file save wait timed out"
                        : lastError);
    return false;
}

modules::clipboard::ClipboardModuleBase* clipboardModuleForCurrentRole(
    session::Session& session)
{
    if (session.moduleHost() == nullptr)
        return nullptr;

    const std::string moduleId =
        session.role() == session::SessionRole::Client
            ? "clipboard.redirect.client"
            : "clipboard.redirect.agent";
    return dynamic_cast<modules::clipboard::ClipboardModuleBase*>(
        session.moduleHost()->module(moduleId));
}

modules::clipboard::DragSurfaceCoordinate dragPointFromOptions(
    int argc,
    char** argv,
    const std::string& prefix,
    int fallbackX,
    int fallbackY)
{
    modules::clipboard::DragSurfaceCoordinate point;
    point.coordinateSpace =
        modules::clipboard::DragCoordinateSpace::RemoteLogical;
    point.x = intOptionValue(argc, argv, prefix + "-x", fallbackX);
    point.y = intOptionValue(argc, argv, prefix + "-y", fallbackY);
    point.surfaceWidth = intOptionValue(argc,
                                        argv,
                                        "--clipboard-drag-surface-width",
                                        1920);
    point.surfaceHeight = intOptionValue(argc,
                                         argv,
                                         "--clipboard-drag-surface-height",
                                         1080);
    point.scale = 1.0;
    return point;
}

bool sendClipboardDragDropIfRequested(
    int argc,
    char** argv,
    session::Session& session,
    runtime::qt::QtRuntimeTransportManager& transportManager,
    protocol::SessionId sessionId,
    runtime::feature::FeatureRuntimeService* featureService,
    runtime::feature::ClipboardRuntimeService* clipboardService)
{
    if (!hasArg(argc, argv, "--clipboard-send-drag-drop"))
        return true;

    modules::clipboard::ClipboardModuleBase* module =
        clipboardModuleForCurrentRole(session);
    if (module == nullptr) {
        writeShellError("clipboard drag requires a running clipboard module");
        return false;
    }

    const int waitMs =
        intOptionValue(argc, argv, "--clipboard-drag-offer-wait-ms", 1500);
    QElapsedTimer timer;
    timer.start();
    while (module->snapshot().localBundle.offerId == 0 &&
           timer.elapsed() < waitMs) {
        runBoundedEventLoop(transportManager,
                            sessionId,
                            50,
                            featureService,
                            clipboardService);
    }

    const modules::clipboard::ClipboardModuleSnapshot snapshot =
        module->snapshot();
    if (snapshot.localBundle.offerId == 0) {
        writeShellError("clipboard drag needs a local clipboard offer");
        return false;
    }

    const modules::clipboard::DragSessionId dragSessionId =
        static_cast<modules::clipboard::DragSessionId>(
            uint64OptionValue(argc, argv, "--clipboard-drag-session-id", 7001));

    modules::clipboard::FdclDragStart start;
    start.start.dragSessionId = dragSessionId;
    start.start.bundleId = snapshot.localBundle.bundleId;
    start.start.offerId = snapshot.localBundle.offerId;
    start.start.ownerEpoch = snapshot.localBundle.ownerEpoch;
    start.start.allowedActions = modules::clipboard::transfer_action::Copy;
    start.start.preferredAction = modules::clipboard::TransferAction::Copy;
    start.start.start = dragPointFromOptions(argc,
                                             argv,
                                             "--clipboard-drag-start",
                                             10,
                                             20);
    if (!module->sendRemoteDragStart(start)) {
        writeShellError("clipboard drag start send failed");
        return false;
    }

    if (hasArg(argc, argv, "--clipboard-send-drag-start-only")) {
        std::cout << "clipboard.drag.sent"
                  << " session=" << dragSessionId
                  << " offer=" << snapshot.localBundle.offerId
                  << " bundle=" << snapshot.localBundle.bundleId
                  << " mode=start_only"
                  << std::endl;
        return true;
    }

    modules::clipboard::FdclDragMove move;
    move.dragSessionId = dragSessionId;
    move.point = dragPointFromOptions(argc,
                                      argv,
                                      "--clipboard-drag-move",
                                      30,
                                      40);
    move.proposedAction = modules::clipboard::TransferAction::Copy;
    if (!module->sendRemoteDragMove(move)) {
        writeShellError("clipboard drag move send failed");
        return false;
    }

    modules::clipboard::FdclDragDrop drop;
    drop.dragSessionId = dragSessionId;
    drop.point = dragPointFromOptions(argc,
                                      argv,
                                      "--clipboard-drag-drop",
                                      50,
                                      60);
    drop.proposedAction = modules::clipboard::TransferAction::Copy;
    if (!module->sendRemoteDragDrop(drop)) {
        writeShellError("clipboard drag drop send failed");
        return false;
    }

    std::cout << "clipboard.drag.sent"
              << " session=" << dragSessionId
              << " offer=" << snapshot.localBundle.offerId
              << " bundle=" << snapshot.localBundle.bundleId
              << std::endl;
    return true;
}

} // namespace

int runPcShell(int argc, char** argv, PcShellRole role)
{
    if (pcShellHelpRequested(argc, argv)) {
        writePcShellHelp(std::cout,
                         applicationName(role),
                         hasArg(argc, argv, "--help-all"));
        return 0;
    }

    if (pcShellGuiConfigModelRequested(argc, argv)) {
        writePcShellGuiConfigModelJson(std::cout);
        return 0;
    }

    const PcOptionValidationResult validation =
        validatePcShellOptions(argc, argv);
    if (!validation.ok) {
        writeShellMessages(validation.messages);
        writeShellError(std::string("Run ") + applicationName(role) +
                        " --help for supported options.");
        return 2;
    }

#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY) || \
    defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
    std::unique_ptr<QCoreApplication> applicationOwner;
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    if (hasArg(argc, argv, "--show-display-window"))
        applicationOwner = std::make_unique<QApplication>(argc, argv);
#endif
#if defined(FUSIONDESK_PC_HAS_QT_FEATURE_ADAPTERS)
    if (applicationOwner == nullptr && qtClipboardEndpointRequested(argc, argv))
        applicationOwner = std::make_unique<QGuiApplication>(argc, argv);
#endif
    if (applicationOwner == nullptr)
        applicationOwner = std::make_unique<QCoreApplication>(argc, argv);
    QCoreApplication& application = *applicationOwner;
#else
    QCoreApplication application(argc, argv);
#endif
    QCoreApplication::setApplicationName(applicationName(role));

    const RuntimeOptionsBuildResult runtimeOptions =
        makeRuntimeOptionsResult(argc, argv);
    if (!runtimeOptions.ok) {
        writeShellMessages(runtimeOptions.messages);
        return 2;
    }

    const std::string clipboardPolicyExportPath =
        optionValue(argc, argv, "--clipboard-policy-export-file");
    if (!clipboardPolicyExportPath.empty()) {
        const PcClipboardPolicyFileSaveResult saved =
            saveClipboardProductPolicyToJsonFile(
                clipboardPolicyExportPath,
                runtimeOptions.options.profile.clipboardPolicy);
        if (!saved.ok) {
            writeShellMessages(saved.messages);
            return 2;
        }
        std::cout << "clipboard.policy.export"
                  << " path=" << clipboardPolicyExportPath
                  << std::endl;
    }

    runtime::RuntimeHost host;
    if (!host.initialize(runtimeOptions.options))
        return 2;
    writeClipboardProductPolicyDiagnosticsIfRequested(
        argc,
        argv,
        host.profile().clipboardPolicy,
        "profile_initialized");

    const session::SessionCreateOptions options =
        makeSessionOptions(host, role, argc, argv);
    runtime::SessionMainlineOptions sessionStartOptions;
    sessionStartOptions.host = &host;
    sessionStartOptions.role = sessionRoleFor(role);
    sessionStartOptions.sessionOptions = options;
    sessionStartOptions.mountProfileModules = false;
    sessionStartOptions.startModules = false;
    const runtime::SessionMainlineReport sessionStarted =
        runtime::SessionMainline::start(sessionStartOptions);
    if (!sessionStarted.ok) {
        writeShellMessages(sessionStarted.messages);
        writeLinkChannelBindingReport(sessionStarted.linkChannels);
        return sessionStarted.session == nullptr ? 3 : 4;
    }

    const protocol::SessionId sessionId = sessionStarted.sessionId;
    session::Session* session = sessionStarted.session;
    if (session == nullptr)
        return 3;

    runtime::qt::QtRuntimeTransportManager transportManager(host);
    if (session->network() == nullptr) {
        writeShellError("session network is missing");
        return 12;
    }

    runtime::qt::QtReconnectRuntimeService reconnectService(
        transportManager,
        session->network()->router(),
        15000);
    const runtime::qt::QtReconnectRuntimeServiceStartResult reconnectStarted =
        reconnectService.start();
    if (!reconnectStarted.ok) {
        writeShellMessages(reconnectStarted.messages);
        writeReconnectDiagnosticsIfRequested(argc,
                                             argv,
                                             reconnectService,
                                             "service_start_failed");
        return 12;
    }

    const std::string transportProfile = optionValue(argc, argv, "--transport-profile");
    std::vector<network::ChannelKey> listeningChannels;
    writeSessionDiagnosticsIfRequested(argc,
                                       argv,
                                       host,
                                       sessionId,
                                       listeningChannels,
                                       "session_created");
    if (!transportProfile.empty()) {
        const runtime::qt::QtTransportConnectResult applied =
            transportManager.applyProfilesFromJsonFileForSession(sessionId,
                                                                 transportProfile,
                                                                 host.profile().minimumChannels);
        if (!applied.ok) {
            writeShellMessages(applied.messages);
            return 5;
        }
        writeSessionDiagnosticsIfRequested(argc,
                                           argv,
                                           host,
                                           sessionId,
                                           listeningChannels,
                                           "transport_profile_applied");
    }

    const std::string listenProfile = optionValue(argc, argv, "--listen-profile");
    if (!listenProfile.empty()) {
        const runtime::qt::QtTransportConnectResult applied =
            transportManager.applyListenProfilesFromJsonFileForSession(sessionId,
                                                                       listenProfile,
                                                                       host.profile().minimumChannels);
        if (!applied.ok) {
            writeShellMessages(applied.messages);
            return 9;
        }
        appendUniqueChannels(listeningChannels, applied.listeningChannels);
        writeSessionDiagnosticsIfRequested(argc,
                                           argv,
                                           host,
                                           sessionId,
                                           listeningChannels,
                                           "listen_profile_applied");
    }

    const bool startPeerProfileResponder = hasArg(argc, argv, "--peer-profile-service");
    const bool requestPeerProfile =
        !optionValues(argc, argv, "--peer-profile-channel").empty();
    const bool negotiateDisplayCodecOverFdpp =
        displayCodecFdppNegotiationRequested(argc, argv);
    DisplayCodecPeerNegotiationState peerCodecNegotiation;
    if (negotiateDisplayCodecOverFdpp &&
        displayCodecLocalNegotiationRequested(argc, argv)) {
        writeShellError("--display-codec-negotiate-fdpp and --display-codec-negotiate-local are mutually exclusive");
        return 13;
    }
    if (negotiateDisplayCodecOverFdpp &&
        ((role == PcShellRole::Agent && !startPeerProfileResponder) ||
         (role == PcShellRole::Client && !requestPeerProfile))) {
        writeShellError("--display-codec-negotiate-fdpp requires the peer-profile exchange path");
        return 13;
    }
    std::unique_ptr<runtime::qt::QtPeerProfileRuntimeService> peerProfileService;
    if (startPeerProfileResponder || requestPeerProfile) {
        peerProfileService = std::make_unique<runtime::qt::QtPeerProfileRuntimeService>(
            transportManager,
            session->network()->router(),
            21000);
        runtime::qt::QtPeerProfileRuntimeServiceStartOptions peerStart;
        peerStart.runtime.startResponder = startPeerProfileResponder;
        peerStart.runtime.subscribeResponses = requestPeerProfile;
        peerStart.runtime.responder.firstResponseMessageId = 22000;
        if (negotiateDisplayCodecOverFdpp && startPeerProfileResponder) {
            peerStart.runtime.responder.exchangeHandler =
                [argc, argv, &peerCodecNegotiation, &host](
                    const runtime::connection::PeerProfileExchangeRequest& request,
                    const runtime::connection::PeerProfileExchangeResult& exchange) {
                    return handleAgentDisplayCodecPeerProfileExchange(argc,
                                                                      argv,
                                                                      host.profile().displayCodecPolicy,
                                                                      request,
                                                                      exchange,
                                                                      peerCodecNegotiation);
                };
        }
        const runtime::qt::QtPeerProfileRuntimeServiceStartResult peerStarted =
            peerProfileService->start(peerStart);
        if (!peerStarted.ok) {
            writeShellMessages(peerStarted.messages);
            return 13;
        }
    }

    if (requestPeerProfile) {
        if (role != PcShellRole::Client) {
            writeShellError("--peer-profile-channel is only supported by the client shell");
            return 13;
        }
        if (!runClientPeerProfileExchange(argc,
                                          argv,
                                          sessionId,
                                          host.profile().minimumChannels,
                                          host.profile().displayCodecPolicy,
                                          transportManager,
                                          *peerProfileService,
                                          &peerCodecNegotiation)) {
            return 13;
        }
    }

    if (negotiateDisplayCodecOverFdpp && role == PcShellRole::Agent) {
        if (!waitForAgentDisplayCodecPeerProfileNegotiation(argc,
                                                            argv,
                                                            sessionId,
                                                            transportManager,
                                                            peerCodecNegotiation)) {
            return 13;
        }
    }

    const std::string reconnectProfile = optionValue(argc, argv, "--reconnect-profile");
    const bool startDisplay = hasArg(argc, argv, "--start-display");
    const bool startModuleInventoryResponder =
        hasArg(argc, argv, "--module-inventory-service");
    const bool requestModuleInventory =
        hasArg(argc, argv, "--module-inventory-request");
    const bool moduleInventoryRequested =
        startModuleInventoryResponder || requestModuleInventory;
    const bool featureRuntimeRequested =
        hasArg(argc, argv, "--pump-profile-modules");
    const bool testEchoRequested = hasArg(argc, argv, "--send-test-echo");
    const bool pumpFeatureInput = hasArg(argc, argv, "--pump-profile-modules");
    const bool pumpClipboard = clipboardRuntimeRequested(argc, argv);
    const int displayFps = intOptionValue(argc, argv, "--display-fps", 15);
    const int displayFirstFrameTimeoutMs =
        intOptionValue(argc, argv, "--display-first-frame-timeout-ms", 3000);
    const bool pumpDisplay = role == PcShellRole::Agent &&
                             (startDisplay ||
                              hasArg(argc, argv, "--pump-display") ||
                              hasArg(argc, argv, "--display-fps"));
    const bool displayRuntimeRequested =
        pumpDisplay || (role == PcShellRole::Client && startDisplay);
    const bool startProfileModules =
        startDisplay ||
        displayRuntimeRequested ||
        hasArg(argc, argv, "--start-profile-modules") ||
        hasArg(argc, argv, "--start-clipboard") ||
        pumpClipboard ||
        featureRuntimeRequested ||
        testEchoRequested;
    const bool mountProfileModules = startProfileModules ||
                                     hasArg(argc, argv, "--mount-profile-modules") ||
                                     hasArg(argc, argv, "--mount-display") ||
                                     hasArg(argc, argv, "--show-display-window") ||
                                     pumpDisplay ||
                                     hasArg(argc, argv, "--mount-input") ||
                                     clipboardProfileRequested(argc, argv) ||
                                     moduleInventoryRequested ||
                                     !optionValues(argc, argv, "--profile-module").empty();
    const bool requireDisplayFrame = hasArg(argc, argv, "--require-display-frame");
    runtime::DisplayMvpDependencies profileDependencies;
    DisplayCaptureRuntimeContext displayCaptureRuntime;
    module::ModuleStartOptions profileModuleStartOptions;
    std::unique_ptr<runtime::connection::ModuleInventoryRuntimeService> moduleInventoryService;
    std::unique_ptr<runtime::display::DisplayRuntimeService> displayService;
    std::shared_ptr<modules::input::IInputCapture> featureInputCapture;
    std::unique_ptr<runtime::feature::FeatureRuntimeService> featureService;
    PcClipboardRuntimeContext clipboardRuntime;
    std::shared_ptr<runtime::feature::IClipboardRuntimeReadPump> clipboardReadPump;
    std::shared_ptr<runtime::feature::IClipboardRuntimePolicy>
        clipboardRuntimePolicy;
    std::shared_ptr<modules::clipboard::IClipboardRemoteReader> clipboardRemoteReader;
    std::shared_ptr<modules::clipboard::IRemoteDisplayCoordinateMapper>
        clipboardDragCoordinateMapper;
    std::unique_ptr<QTimer> displayPumpTimer;
    std::unique_ptr<QTimer> featurePumpTimer;
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
    std::unique_ptr<adapters::qt::display::QtImageDisplayWindow> displayWindow;
#endif
    if (clipboardProfileRequested(argc, argv)) {
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
        if (role == PcShellRole::Client &&
            hasArg(argc, argv, "--show-display-window")) {
            clipboardDragCoordinateMapper =
                makeClipboardDisplayWindowDragCoordinateMapper(
                    argc,
                    argv,
                    [&displayWindow]() {
                        return displayWindow.get();
                    });
        } else
#endif
        {
            clipboardDragCoordinateMapper =
                makeClipboardDragCoordinateMapper(argc, argv);
        }

        clipboardReadPump =
            makeClipboardRuntimeReadPump(transportManager, sessionId);
        clipboardRuntimePolicy =
            makeClipboardRuntimePolicy(host.profile().clipboardPolicy);
        runtime::feature::ClipboardRuntimeRemoteReaderOptions readerOptions;
        readerOptions.session = session;
        readerOptions.pump = clipboardReadPump.get();
        readerOptions.policy = clipboardRuntimePolicy;
        readerOptions.defaultTimeoutMs = static_cast<std::uint32_t>(
            intOptionValue(argc, argv, "--clipboard-read-timeout-ms", 1000));
        clipboardRemoteReader =
            std::make_shared<runtime::feature::ClipboardRuntimeRemoteReader>(
                readerOptions);
    }
    if (mountProfileModules) {
        profileDependencies =
            makeProfileDependencies(role,
                                    argc,
                                    argv,
                                    host.profile(),
                                    &displayCaptureRuntime,
                                    &peerCodecNegotiation,
                                    clipboardRemoteReader,
                                    clipboardDragCoordinateMapper);
        if (!seedClipboardTextIfRequested(
                argc,
                argv,
                profileDependencies.clipboardEndpoint)) {
            return 22;
        }
        if (!seedClipboardFormattedTextIfRequested(
                argc,
                argv,
                profileDependencies.clipboardEndpoint)) {
            return 22;
        }
        if (!seedClipboardImagePngIfRequested(
                argc,
                argv,
                profileDependencies.clipboardEndpoint)) {
            return 22;
        }
        if (!seedClipboardFilesIfRequested(
                argc,
                argv,
                profileDependencies.clipboardEndpoint)) {
            return 22;
        }
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
        if (role == PcShellRole::Client &&
            hasArg(argc, argv, "--show-display-window")) {
            if (displayCaptureRuntime.imageRenderer == nullptr) {
                writeShellError("display window requires Qt image display renderer");
                return 18;
            }
            displayWindow =
                std::make_unique<adapters::qt::display::QtImageDisplayWindow>();
            displayWindow->attachRenderer(*displayCaptureRuntime.imageRenderer);
            displayWindow->show();
        }
#else
        if (hasArg(argc, argv, "--show-display-window")) {
            writeShellError("display window support is not available in this build");
            return 18;
        }
#endif
        if (pumpFeatureInput) {
            featureInputCapture = profileDependencies.inputCapture;
            profileDependencies.inputCapture.reset();
        }
        runtime::SessionMainlineModuleOptions mountOptions;
        mountOptions.host = &host;
        mountOptions.session = session;
        mountOptions.moduleDependencies = profileDependencies;
        mountOptions.linkReportOptions = makeLinkReportOptions(listeningChannels);
        mountOptions.mountProfileModules = true;
        mountOptions.startModules = false;
        const runtime::SessionMainlineReport mounted =
            runtime::SessionMainline::mountAndStart(mountOptions);
        if (!mounted.mount.ok()) {
            writeShellMessages(mounted.messages);
            writeShellError("profile module mount failed");
            writeSessionDiagnosticsIfRequested(argc,
                                               argv,
                                               host,
                                               sessionId,
                                               listeningChannels,
                                               "profile_mount_failed");
            return 7;
        }
        writeSessionDiagnosticsIfRequested(argc,
                                           argv,
                                           host,
                                           sessionId,
                                           listeningChannels,
                                           "profile_mounted");
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
        updateDisplayWindowHealth(displayWindow.get(),
                                  host,
                                  sessionId,
                                  listeningChannels);
#endif

        if (!requiredProfileModulesMounted(argc, argv, *session, role))
            return 7;

        if (moduleInventoryRequested) {
            if (session->moduleHost() == nullptr) {
                writeShellError("module inventory requires a profile module host");
                return 15;
            }

            moduleInventoryService =
                std::make_unique<runtime::connection::ModuleInventoryRuntimeService>(
                    session->network()->router(),
                    23000);
            const runtime::connection::ModuleInventory localInventory =
                makeLocalModuleInventory(sessionId, *session);

            runtime::connection::ModuleInventoryRuntimeServiceStartOptions inventoryStart;
            inventoryStart.startResponder = startModuleInventoryResponder;
            inventoryStart.subscribeResponses = requestModuleInventory;
            inventoryStart.responder.localInventory = localInventory;
            inventoryStart.responder.firstResponseMessageId = 24000;
            const runtime::connection::ModuleInventoryRuntimeServiceStartResult inventoryStarted =
                moduleInventoryService->start(inventoryStart);
            if (!inventoryStarted.ok) {
                writeShellMessages(inventoryStarted.messages);
                writeModuleInventoryDiagnosticsIfRequested(argc,
                                                           argv,
                                                           moduleInventoryService.get(),
                                                           "service_start_failed");
                return 15;
            }
            writeModuleInventoryDiagnosticsIfRequested(argc,
                                                       argv,
                                                       moduleInventoryService.get(),
                                                       "service_started");

            runtime::connection::ModuleInventory remoteInventory;
            if (requestModuleInventory) {
                runtime::connection::ModuleInventoryRuntimeExchangeOptions exchangeOptions;
                exchangeOptions.wire.messageId = 0;
                exchangeOptions.wire.sessionId = sessionId;
                exchangeOptions.wire.traceId = session->context().traceId;
                exchangeOptions.wire.timeoutMs = static_cast<std::uint32_t>(
                    intOptionValue(argc, argv, "--module-inventory-timeout-ms", 3000));
                exchangeOptions.wire.monotonicTimestampUsec =
                    runtime::qt::QtTimerBridge::monotonicNowUsec();
                const runtime::connection::ModuleInventoryRuntimeDispatchResult dispatched =
                    moduleInventoryService->requestModuleInventory(localInventory,
                                                                   exchangeOptions);
                if (!dispatched.ok) {
                    writeShellMessages(dispatched.messages);
                    writeModuleInventoryDiagnosticsIfRequested(argc,
                                                               argv,
                                                               moduleInventoryService.get(),
                                                               "request_failed");
                    return 15;
                }
                writeModuleInventoryDiagnosticsIfRequested(argc,
                                                           argv,
                                                           moduleInventoryService.get(),
                                                           "request_sent");

                const int waitInventoryMs = intOptionValue(argc,
                                                           argv,
                                                           "--module-inventory-wait-ms",
                                                           5000);
                const ModuleInventoryWaitResult inventoryWait =
                    waitForModuleInventory(*moduleInventoryService,
                                           transportManager,
                                           sessionId,
                                           true,
                                           false,
                                           waitInventoryMs,
                                           remoteInventory);
                if (!inventoryWait.ok) {
                    writeShellMessages(inventoryWait.messages);
                    if (!inventoryWait.terminalFailure)
                        writeShellError("module inventory exchange timed out");
                    writeModuleInventoryDiagnosticsIfRequested(argc,
                                                               argv,
                                                               moduleInventoryService.get(),
                                                               inventoryWait.terminalFailure
                                                                   ? "request_failed"
                                                                   : "request_timeout");
                    return 15;
                }
                writeModuleInventoryDiagnosticsIfRequested(argc,
                                                           argv,
                                                           moduleInventoryService.get(),
                                                           "inventory_complete");
                session->updateRemoteModuleInventory(remoteInventory.sessionId,
                                                     remoteInventory.manifests);
                profileModuleStartOptions.peerVersions =
                    runtime::connection::peerVersionsFromModuleInventory(remoteInventory);
            } else if (startModuleInventoryResponder) {
                const int waitInventoryMs = intOptionValue(argc,
                                                           argv,
                                                           "--module-inventory-wait-ms",
                                                           0);
                if (waitInventoryMs > 0) {
                    const ModuleInventoryWaitResult inventoryWait =
                        waitForModuleInventory(*moduleInventoryService,
                                               transportManager,
                                               sessionId,
                                               false,
                                               true,
                                               waitInventoryMs,
                                               remoteInventory);
                    if (!inventoryWait.ok) {
                        writeShellMessages(inventoryWait.messages);
                        if (!inventoryWait.terminalFailure)
                            writeShellError("module inventory responder did not receive remote inventory");
                        writeModuleInventoryDiagnosticsIfRequested(argc,
                                                                   argv,
                                                                   moduleInventoryService.get(),
                                                                   inventoryWait.terminalFailure
                                                                       ? "responder_failed"
                                                                       : "responder_timeout");
                        return 15;
                    }
                    writeModuleInventoryDiagnosticsIfRequested(argc,
                                                               argv,
                                                               moduleInventoryService.get(),
                                                               "inventory_complete");
                    session->updateRemoteModuleInventory(remoteInventory.sessionId,
                                                         remoteInventory.manifests);
                    profileModuleStartOptions.peerVersions =
                        runtime::connection::peerVersionsFromModuleInventory(remoteInventory);
                }
            }
        }

        if (startProfileModules) {
            if (session->moduleHost() == nullptr) {
                writeShellError("profile module host is missing");
                return 8;
            }

            const int waitChannelsMs = intOptionValue(argc, argv, "--wait-channels-ms", 3000);
            if (!waitForRequiredModuleChannels(*session,
                                               transportManager,
                                               sessionId,
                                               waitChannelsMs,
                                               listeningChannels)) {
                writeShellError("profile module required channels are not ready");
                writeSessionDiagnosticsIfRequested(argc,
                                                   argv,
                                                   host,
                                                   sessionId,
                                                   listeningChannels,
                                                   "profile_start_blocked");
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
                updateDisplayWindowHealth(displayWindow.get(),
                                          host,
                                          sessionId,
                                          listeningChannels);
#endif
                return 8;
            }

            runtime::SessionMainlineModuleOptions startOptions;
            startOptions.host = &host;
            startOptions.session = session;
            startOptions.linkReportOptions = makeLinkReportOptions(listeningChannels);
            startOptions.mountProfileModules = false;
            startOptions.startModules = true;
            startOptions.moduleStartOptions = profileModuleStartOptions;
            const runtime::SessionMainlineReport started =
                runtime::SessionMainline::mountAndStart(startOptions);
            if (!started.linkChannels.ok)
                writeLinkChannelBindingReport(started.linkChannels);
            writeShellMessages(started.messages);
            for (const module::ModuleStartReport& report : started.moduleStarts) {
                if (!report.started) {
                    writeShellError("profile module start failed: " + report.moduleId +
                                    " reason=" + denyReasonName(report.decision.reason) +
                                    " message=" + report.decision.message +
                                    " diagnostics=" + report.diagnostics);
                    writeSessionDiagnosticsIfRequested(argc,
                                                       argv,
                                                       host,
                                                       sessionId,
                                                       listeningChannels,
                                                       "profile_start_failed");
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
                    updateDisplayWindowHealth(displayWindow.get(),
                                              host,
                                              sessionId,
                                              listeningChannels);
#endif
                    return 8;
                }
            }
            if (!started.ok) {
                writeSessionDiagnosticsIfRequested(argc,
                                                   argv,
                                                   host,
                                                   sessionId,
                                                   listeningChannels,
                                                   "profile_start_failed");
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
                updateDisplayWindowHealth(displayWindow.get(),
                                          host,
                                          sessionId,
                                          listeningChannels);
#endif
                return 8;
            }
            writeSessionDiagnosticsIfRequested(argc,
                                               argv,
                                               host,
                                               sessionId,
                                               listeningChannels,
                                     "profile_started");
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
            updateDisplayWindowHealth(displayWindow.get(),
                                      host,
                                      sessionId,
                                      listeningChannels);
#endif

            if (displayRuntimeRequested) {
                runtime::display::DisplayRuntimeServiceOptions displayOptions;
                displayOptions.session = session;
                displayOptions.targetFps = static_cast<std::uint32_t>(
                    displayFps <= 0 ? 15 : displayFps);
                displayOptions.pumpAgentFrames = role == PcShellRole::Agent;
                displayOptions.monitorClientFirstFrame =
                    role == PcShellRole::Client;
                displayOptions.firstFrameTimeoutUsec =
                    static_cast<std::uint64_t>(
                        displayFirstFrameTimeoutMs <= 0
                            ? 3000
                            : displayFirstFrameTimeoutMs) *
                    1000ULL;
                displayOptions.captureBackendFactory =
                    displayCaptureRuntime.backendFactory.get();
                displayOptions.captureBackendSelectionRequest =
                    displayCaptureRuntime.selectionRequest;
                displayService = std::make_unique<runtime::display::DisplayRuntimeService>(
                    displayOptions);
                const runtime::display::DisplayRuntimeServiceStartResult displayStarted =
                    displayService->start();
                if (!displayStarted.ok) {
                    writeShellMessages(displayStarted.messages);
                    return 17;
                }
                writeDisplayRuntimeDiagnosticsIfRequested(
                    argc,
                    argv,
                    displayService.get(),
                    "display_runtime_started");
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
                updateDisplayWindowHealth(displayWindow.get(),
                                          host,
                                          sessionId,
                                          listeningChannels);
#endif

                displayPumpTimer = std::make_unique<QTimer>();
                int intervalMs = displayOptions.pumpAgentFrames
                                     ? 1000 / (displayFps <= 0 ? 15
                                                               : displayFps)
                                     : displayFirstFrameTimeoutMs / 4;
                if (intervalMs <= 0)
                    intervalMs = 1;
                if (!displayOptions.pumpAgentFrames && intervalMs > 100)
                    intervalMs = 100;
                displayPumpTimer->setInterval(intervalMs);
                QObject::connect(displayPumpTimer.get(),
                                 &QTimer::timeout,
                                 [&displayService]() {
                                     if (displayService)
                                         displayService->pumpOnce();
                                 });
                displayPumpTimer->start();
            }

            if (pumpClipboard) {
                const PcClipboardRuntimeStartResult clipboardStarted =
                    startClipboardRuntime(argc,
                                          argv,
                                          *session,
                                          profileDependencies.clipboardEndpoint,
                                          clipboardRuntimePolicy,
                                          clipboardRuntime);
                if (!clipboardStarted.ok) {
                    writeShellMessages(clipboardStarted.messages);
                    return 20;
                }
                writeClipboardDiagnosticsIfRequested(
                    argc,
                    argv,
                    session,
                    clipboardRuntime.service.get(),
                    clipboardRuntimePolicy,
                    profileDependencies.clipboardEndpoint,
                    "clipboard_runtime_started");
            }

            if (testEchoRequested &&
                !sendTestEchoIfRequested(argc,
                                         argv,
                                         *session,
                                         role,
                                         transportManager,
                                         sessionId)) {
                return 16;
            }

            if (!sendClipboardDragDropIfRequested(argc,
                                                  argv,
                                                  *session,
                                                  transportManager,
                                                  sessionId,
                                                  featureService.get(),
                                                  clipboardRuntime.service.get())) {
                return 23;
            }
        }

        if (featureRuntimeRequested) {
            runtime::feature::FeatureRuntimeServiceOptions featureOptions;
            featureOptions.session = session;
            featureOptions.inputCapture = pumpFeatureInput ?
                                          featureInputCapture :
                                          profileDependencies.inputCapture;
            featureOptions.maxInputEventsPerPump = static_cast<std::uint32_t>(
                intOptionValue(argc, argv, "--max-input-events-per-pump", 64));
            featureOptions.manageInputCaptureLifecycle = pumpFeatureInput;
            featureService = std::make_unique<runtime::feature::FeatureRuntimeService>(
                featureOptions);

            const runtime::feature::FeatureRuntimeServiceStartResult featureStarted =
                featureService->start();
            if (!featureStarted.ok) {
                writeShellMessages(featureStarted.messages);
                return 14;
            }

            if (hasArg(argc, argv, "--pump-profile-modules")) {
                featurePumpTimer = std::make_unique<QTimer>();
                const int pumpIntervalMs =
                    intOptionValue(argc, argv, "--feature-pump-interval-ms", 10);
                featurePumpTimer->setInterval(pumpIntervalMs > 0 ? pumpIntervalMs : 10);
                QObject::connect(featurePumpTimer.get(),
                                 &QTimer::timeout,
                                 [&featureService]() {
                                     if (featureService)
                                         featureService->pumpOnce();
                                 });
                featurePumpTimer->start();
            }
        }
    }

    int minimumDisplayFramesAfterReconnect = 0;
    if (!reconnectProfile.empty()) {
        const int reconnectAfterMs = intOptionValue(argc, argv, "--reconnect-after-ms", 100);
        runBoundedEventLoop(transportManager,
                            sessionId,
                            reconnectAfterMs,
                            featureService.get(),
                            clipboardRuntime.service.get());

        if (requireDisplayFrame)
            minimumDisplayFramesAfterReconnect = displayFrameCount(*session, role) + 1;

        std::string reconnectReason = optionValue(argc, argv, "--reconnect-reason");
        if (reconnectReason.empty())
            reconnectReason = "pc shell reconnect profile";

        const runtime::qt::QtReconnectOrchestrationRequestLoadResult loaded =
            runtime::qt::loadReconnectOrchestrationRequestFromJsonFileForSession(
                sessionId,
                0,
                reconnectProfile,
                host.profile().minimumChannels,
                reconnectReason,
                !hasArg(argc, argv, "--reconnect-no-display-keyframe"));
        if (!loaded.ok) {
            writeShellMessages(loaded.messages);
            return 11;
        }

        runtime::connection::ReconnectCoordinatorOptions reconnectOptions;
        reconnectOptions.startAgentReplacements = false;
        reconnectOptions.dispatchClientTeardown = false;
        const runtime::connection::ReconnectCoordinatorRunResult reconnected =
            reconnectService.run(loaded.request, reconnectOptions);
        if (!reconnected.ok) {
            writeShellMessages(reconnected.messages);
            writeReconnectDiagnosticsIfRequested(argc,
                                                 argv,
                                                 reconnectService,
                                                 "reconnect_failed");
            return 11;
        }
        writeReconnectDiagnosticsIfRequested(argc,
                                             argv,
                                             reconnectService,
                                             "reconnect_complete");
        writeSessionDiagnosticsIfRequested(argc,
                                           argv,
                                           host,
                                           sessionId,
                                           listeningChannels,
                                           "reconnect_complete");
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
        updateDisplayWindowHealth(displayWindow.get(),
                                  host,
                                  sessionId,
                                  listeningChannels);
#endif

    }

    const int runMs = intOptionValue(argc, argv, "--run-ms", 0);
    if (runMs > 0) {
        runBoundedEventLoop(transportManager,
                            sessionId,
                            runMs,
                            featureService.get(),
                            clipboardRuntime.service.get());
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
        updateDisplayWindowHealth(displayWindow.get(),
                                  host,
                                  sessionId,
                                  listeningChannels);
#endif
        writeClipboardDiagnosticsIfRequested(
            argc,
            argv,
            session,
            clipboardRuntime.service.get(),
            clipboardRuntimePolicy,
            profileDependencies.clipboardEndpoint,
            "exit_precheck");
        if (requireDisplayFrame && !displayFrameObserved(*session, role)) {
            writeShellError("display frame was not observed");
            return 10;
        }
        if (minimumDisplayFramesAfterReconnect > 0 &&
            displayFrameCount(*session, role) < minimumDisplayFramesAfterReconnect) {
            writeShellError(
                "display frame did not advance after reconnect current=" +
                std::to_string(displayFrameCount(*session, role)) +
                " required=" +
                std::to_string(minimumDisplayFramesAfterReconnect) +
                " diagnostics=" + displayModuleDiagnostics(*session, role));
            return 10;
        }
        if (!waitForClipboardTextRequirement(argc,
                                             argv,
                                             profileDependencies.clipboardEndpoint,
                                             transportManager,
                                             sessionId,
                                             featureService.get(),
                                             clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardFormattedTextRequirement(
                argc,
                argv,
                profileDependencies.clipboardEndpoint,
                transportManager,
                sessionId,
                featureService.get(),
                clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardImagePngRequirement(
                argc,
                argv,
                profileDependencies.clipboardEndpoint,
                transportManager,
                sessionId,
                featureService.get(),
                clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardFileTextRequirement(argc,
                                                 argv,
                                                 *session,
                                                 clipboardRemoteReader,
                                                 transportManager,
                                                 sessionId,
                                                 featureService.get(),
                                                 clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardEndpointFileTextRequirement(
                argc,
                argv,
                profileDependencies.clipboardEndpoint,
                transportManager,
                sessionId,
                featureService.get(),
                clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardFileSave(argc,
                                      argv,
                                      *session,
                                      clipboardRemoteReader,
                                      transportManager,
                                      sessionId,
                                      featureService.get(),
                                      clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        writeClipboardDiagnosticsIfRequested(
            argc,
            argv,
            session,
            clipboardRuntime.service.get(),
            clipboardRuntimePolicy,
            profileDependencies.clipboardEndpoint,
            "exit_postcheck");
        writeDisplayRuntimeDiagnosticsIfRequested(argc,
                                                  argv,
                                                  displayService.get(),
                                                  "exit");
        writeReconnectDiagnosticsIfRequested(argc, argv, reconnectService, "exit");
        writeModuleInventoryDiagnosticsIfRequested(argc,
                                                   argv,
                                                   moduleInventoryService.get(),
                                                   "exit");
        writeSessionDiagnosticsIfRequested(argc,
                                           argv,
                                           host,
                                           sessionId,
                                           listeningChannels,
                                           "exit");
        return 0;
    }

    if (hasArg(argc, argv, "--smoke")) {
#if defined(FUSIONDESK_PC_HAS_QT_WIDGET_DISPLAY)
        updateDisplayWindowHealth(displayWindow.get(),
                                  host,
                                  sessionId,
                                  listeningChannels);
#endif
        writeClipboardDiagnosticsIfRequested(
            argc,
            argv,
            session,
            clipboardRuntime.service.get(),
            clipboardRuntimePolicy,
            profileDependencies.clipboardEndpoint,
            "smoke_precheck");
        if (!waitForClipboardTextRequirement(argc,
                                             argv,
                                             profileDependencies.clipboardEndpoint,
                                             transportManager,
                                             sessionId,
                                             featureService.get(),
                                             clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardFormattedTextRequirement(
                argc,
                argv,
                profileDependencies.clipboardEndpoint,
                transportManager,
                sessionId,
                featureService.get(),
                clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardImagePngRequirement(
                argc,
                argv,
                profileDependencies.clipboardEndpoint,
                transportManager,
                sessionId,
                featureService.get(),
                clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardFileTextRequirement(argc,
                                                 argv,
                                                 *session,
                                                 clipboardRemoteReader,
                                                 transportManager,
                                                 sessionId,
                                                 featureService.get(),
                                                 clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardEndpointFileTextRequirement(
                argc,
                argv,
                profileDependencies.clipboardEndpoint,
                transportManager,
                sessionId,
                featureService.get(),
                clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        if (!waitForClipboardFileSave(argc,
                                      argv,
                                      *session,
                                      clipboardRemoteReader,
                                      transportManager,
                                      sessionId,
                                      featureService.get(),
                                      clipboardRuntime.service.get())) {
            writeClipboardDiagnosticsIfRequested(
                argc,
                argv,
                session,
                clipboardRuntime.service.get(),
                clipboardRuntimePolicy,
                profileDependencies.clipboardEndpoint,
                "clipboard_requirement_failed");
            return 21;
        }
        writeClipboardDiagnosticsIfRequested(
            argc,
            argv,
            session,
            clipboardRuntime.service.get(),
            clipboardRuntimePolicy,
            profileDependencies.clipboardEndpoint,
            "smoke_postcheck");
        writeDisplayRuntimeDiagnosticsIfRequested(argc,
                                                  argv,
                                                  displayService.get(),
                                                  "smoke_exit");
        writeReconnectDiagnosticsIfRequested(argc, argv, reconnectService, "smoke_exit");
        writeModuleInventoryDiagnosticsIfRequested(argc,
                                                   argv,
                                                   moduleInventoryService.get(),
                                                   "smoke_exit");
        writeSessionDiagnosticsIfRequested(argc,
                                           argv,
                                           host,
                                           sessionId,
                                           listeningChannels,
                                           "smoke_exit");
        return 0;
    }

    return runtime::qt::QtEventLoopBridge::hasApplication() ? application.exec() : 6;
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
