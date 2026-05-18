#include <cassert>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>

#ifndef FUSIONDESK_PC_AGENT_EXE
#error FUSIONDESK_PC_AGENT_EXE is required
#endif

#ifndef FUSIONDESK_PC_CLIENT_EXE
#error FUSIONDESK_PC_CLIENT_EXE is required
#endif

#ifndef FUSIONDESK_PC_PROFILE_PLAN_EXE
#error FUSIONDESK_PC_PROFILE_PLAN_EXE is required
#endif

namespace {

std::array<quint16, 4> nextAvailablePorts()
{
    QTcpServer controlProbe;
    QTcpServer smallDataProbe;
    QTcpServer screenProbe;
    QTcpServer largeDataProbe;
    assert(controlProbe.listen(QHostAddress::LocalHost, 0));
    assert(smallDataProbe.listen(QHostAddress::LocalHost, 0));
    assert(screenProbe.listen(QHostAddress::LocalHost, 0));
    assert(largeDataProbe.listen(QHostAddress::LocalHost, 0));
    const std::array<quint16, 4> ports = {
        controlProbe.serverPort(),
        smallDataProbe.serverPort(),
        screenProbe.serverPort(),
        largeDataProbe.serverPort(),
    };
    controlProbe.close();
    smallDataProbe.close();
    screenProbe.close();
    largeDataProbe.close();
    assert(ports[0] != ports[1]);
    assert(ports[0] != ports[2]);
    assert(ports[0] != ports[3]);
    assert(ports[1] != ports[2]);
    assert(ports[1] != ports[3]);
    assert(ports[2] != ports[3]);
    return ports;
}

void waitBriefly(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

std::filesystem::path profilePath(const std::string& name)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fusiondesk_" + name + "_" + std::to_string(now) + ".json");
}

void dumpProcessOutput(const char* label, QProcess& process)
{
    const QByteArray stdoutBytes = process.readAllStandardOutput();
    const QByteArray stderrBytes = process.readAllStandardError();
    if (!stdoutBytes.isEmpty()) {
        std::fputs(label, stderr);
        std::fputs(" stdout:\n", stderr);
        std::fwrite(stdoutBytes.constData(),
                    1,
                    static_cast<std::size_t>(stdoutBytes.size()),
                    stderr);
        std::fputs("\n", stderr);
    }
    if (!stderrBytes.isEmpty()) {
        std::fputs(label, stderr);
        std::fputs(" stderr:\n", stderr);
        std::fwrite(stderrBytes.constData(),
                    1,
                    static_cast<std::size_t>(stderrBytes.size()),
                    stderr);
        std::fputs("\n", stderr);
    }
}

void generateProfiles(const std::filesystem::path& clientProfile,
                      const std::filesystem::path& agentProfile,
                      const std::string& controlEndpoint,
                      const std::string& smallDataEndpoint,
                      const std::string& screenEndpoint,
                      const std::string& largeDataEndpoint)
{
    QProcess profilePlan;
    profilePlan.start(QString::fromUtf8(FUSIONDESK_PC_PROFILE_PLAN_EXE),
                      {"--client-profile",
                       QString::fromStdString(clientProfile.string()),
                       "--agent-profile",
                       QString::fromStdString(agentProfile.string()),
                       "--client-ready-prefix",
                       "pc-clipboard-reconnect-client",
                       "--agent-ready-prefix",
                       "pc-clipboard-reconnect-agent",
                       "--channel",
                       QString::fromStdString("control=" + controlEndpoint),
                       "--channel",
                       QString::fromStdString("small_data=" + smallDataEndpoint),
                       "--channel",
                       QString::fromStdString("main_screen=" + screenEndpoint),
                       "--channel",
                       QString::fromStdString("large_data=" + largeDataEndpoint)});
    assert(profilePlan.waitForStarted(3000));
    assert(profilePlan.waitForFinished(5000));
    assert(profilePlan.exitStatus() == QProcess::NormalExit);
    assert(profilePlan.exitCode() == 0);
    assert(std::filesystem::exists(clientProfile));
    assert(std::filesystem::exists(agentProfile));
}

void generateLargeDataReconnectProfile(
    const std::filesystem::path& clientProfile,
    const std::filesystem::path& agentProfile,
    const std::string& largeDataEndpoint)
{
    QProcess profilePlan;
    profilePlan.start(QString::fromUtf8(FUSIONDESK_PC_PROFILE_PLAN_EXE),
                      {"--client-profile",
                       QString::fromStdString(clientProfile.string()),
                       "--agent-profile",
                       QString::fromStdString(agentProfile.string()),
                       "--client-ready-prefix",
                       "pc-clipboard-large-reconnect-client",
                       "--agent-ready-prefix",
                       "pc-clipboard-large-reconnect-agent",
                       "--channel",
                       QString::fromStdString("large_data=" + largeDataEndpoint)});
    assert(profilePlan.waitForStarted(3000));
    assert(profilePlan.waitForFinished(5000));
    assert(profilePlan.exitStatus() == QProcess::NormalExit);
    assert(profilePlan.exitCode() == 0);
    assert(std::filesystem::exists(clientProfile));
    assert(std::filesystem::exists(agentProfile));
}

bool containsAll(const QByteArray& bytes,
                 const std::vector<const char*>& needles)
{
    for (const char* needle : needles) {
        if (!bytes.contains(needle))
            return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    const std::array<quint16, 4> ports = nextAvailablePorts();
    const std::string controlEndpoint = "127.0.0.1:" + std::to_string(ports[0]);
    const std::string smallDataEndpoint = "127.0.0.1:" + std::to_string(ports[1]);
    const std::string screenEndpoint = "127.0.0.1:" + std::to_string(ports[2]);
    const std::string largeDataEndpoint = "127.0.0.1:" + std::to_string(ports[3]);
    const std::filesystem::path agentProfile =
        profilePath("pc_clipboard_reconnect_agent_listen");
    const std::filesystem::path clientProfile =
        profilePath("pc_clipboard_reconnect_client_connect");
    const std::filesystem::path agentReconnectProfile =
        profilePath("pc_clipboard_reconnect_agent_unused");
    const std::filesystem::path clientReconnectProfile =
        profilePath("pc_clipboard_reconnect_client_large_data");
    const std::string expectedText =
        "fusiondesk pc clipboard reconnect text";

    generateProfiles(clientProfile,
                     agentProfile,
                     controlEndpoint,
                     smallDataEndpoint,
                     screenEndpoint,
                     largeDataEndpoint);
    generateLargeDataReconnectProfile(clientReconnectProfile,
                                      agentReconnectProfile,
                                      largeDataEndpoint);

    QProcess agent;
    QStringList agentArguments = {"--listen-profile",
                                  QString::fromStdString(agentProfile.string()),
                                  "--session-id",
                                  "2",
                                  "--start-clipboard",
                                  "--pump-clipboard",
                                  "--clipboard-no-receive",
                                  "--clipboard-dry-run-text",
                                  QString::fromStdString(expectedText),
                                  "--print-clipboard-diagnostics",
                                  "--print-session-diagnostics",
                                  "--wait-channels-ms",
                                  "6000",
                                  "--run-ms",
                                  "9000"};
    agent.start(QString::fromUtf8(FUSIONDESK_PC_AGENT_EXE), agentArguments);
    assert(agent.waitForStarted(3000));
    waitBriefly(100);

    QProcess client;
    QStringList clientArguments = {"--transport-profile",
                                   QString::fromStdString(clientProfile.string()),
                                   "--session-id",
                                   "1",
                                   "--start-clipboard",
                                   "--pump-clipboard",
                                   "--clipboard-no-announce",
                                   "--require-clipboard-text",
                                   QString::fromStdString(expectedText),
                                   "--clipboard-require-wait-ms",
                                   "2500",
                                   "--reconnect-profile",
                                   QString::fromStdString(
                                       clientReconnectProfile.string()),
                                   "--reconnect-after-ms",
                                   "2000",
                                   "--reconnect-reason",
                                   "pc clipboard large_data reconnect smoke",
                                   "--reconnect-no-display-keyframe",
                                   "--print-reconnect-diagnostics",
                                   "--print-clipboard-diagnostics",
                                   "--print-session-diagnostics",
                                   "--wait-channels-ms",
                                   "6000",
                                   "--run-ms",
                                   "4500"};
    client.start(QString::fromUtf8(FUSIONDESK_PC_CLIENT_EXE), clientArguments);
    assert(client.waitForStarted(3000));
    assert(client.waitForFinished(15000));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard reconnect client failed: control=%s small_data=%s large_data=%s status=%d code=%d agentState=%d\n",
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     largeDataEndpoint.c_str(),
                     static_cast<int>(client.exitStatus()),
                     client.exitCode(),
                     static_cast<int>(agent.state()));
        dumpProcessOutput("client", client);
        if (agent.state() != QProcess::NotRunning) {
            agent.terminate();
            agent.waitForFinished(3000);
        }
        dumpProcessOutput("agent", agent);
        return 1;
    }

    const QByteArray clientStdout = client.readAllStandardOutput();
    if (!containsAll(clientStdout,
                     {"reconnect.diagnostics",
                      "phase=reconnect_complete",
                      "attempted=1",
                      "complete=1",
                      "ok=1",
                      "timeoutOk=1",
                      "keyframe=0",
                      "reason=pc clipboard large_data reconnect smoke",
                      "reconnect.diagnostics.channel",
                      "reconnect.diagnostics.stage phase=reconnect_complete name=session_rebind"})) {
        std::fputs("client reconnect diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(),
                    1,
                    static_cast<std::size_t>(clientStdout.size()),
                    stderr);
        std::fputs("\n", stderr);
        return 1;
    }
    if (!containsAll(clientStdout,
                     {"clipboard.runtime phase=exit_precheck",
                      "active=true",
                      "endpoint=true",
                      "clipboard.module phase=exit_precheck module=clipboard.redirect.client",
                      "formatListsReceived=",
                      "inlineResponsesReceived=",
                      "pendingReads=0",
                      "clipboard.endpoint phase=exit_precheck",
                      "kind=windows",
                      "dryRun=true"})) {
        std::fputs("client clipboard diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(),
                    1,
                    static_cast<std::size_t>(clientStdout.size()),
                    stderr);
        std::fputs("\n", stderr);
        return 1;
    }
    if (!containsAll(clientStdout,
                     {"session.diagnostics",
                      "phase=reconnect_complete",
                      "phase=exit",
                      "linkReady=1",
                      "blocked=0",
                      "mounted=1",
                      "running=1",
                      "session.diagnostics.channel",
                      "ready=1"})) {
        std::fputs("client session diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(),
                    1,
                    static_cast<std::size_t>(clientStdout.size()),
                    stderr);
        std::fputs("\n", stderr);
        return 1;
    }

    assert(agent.waitForFinished(15000));
    if (agent.exitStatus() != QProcess::NormalExit || agent.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard reconnect agent failed: control=%s small_data=%s large_data=%s status=%d code=%d\n",
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     largeDataEndpoint.c_str(),
                     static_cast<int>(agent.exitStatus()),
                     agent.exitCode());
        dumpProcessOutput("agent", agent);
        return 1;
    }
    const QByteArray agentStdout = agent.readAllStandardOutput();
    if (!containsAll(agentStdout,
                     {"clipboard.runtime phase=exit_precheck",
                      "active=true",
                      "endpoint=true",
                      "announcementsSent=",
                      "clipboard.module phase=exit_precheck module=clipboard.redirect.agent",
                      "formatListsSent=",
                      "pendingReads=0",
                      "clipboard.endpoint phase=exit_precheck",
                      "kind=windows",
                      "dryRun=true"})) {
        std::fputs("agent clipboard diagnostics output was missing expected fields\n", stderr);
        std::fwrite(agentStdout.constData(),
                    1,
                    static_cast<std::size_t>(agentStdout.size()),
                    stderr);
        std::fputs("\n", stderr);
        return 1;
    }

    std::filesystem::remove(agentProfile);
    std::filesystem::remove(clientProfile);
    std::filesystem::remove(agentReconnectProfile);
    std::filesystem::remove(clientReconnectProfile);
    return 0;
}
