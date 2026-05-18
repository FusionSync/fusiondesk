#include <cassert>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>

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

bool nativeClipboardValidationRequested()
{
    const char* value =
        std::getenv("FUSIONDESK_VALIDATE_PC_NATIVE_CLIPBOARD_TEXT");
    return value != nullptr && std::string(value) == "1";
}

bool qtClipboardValidationRequested()
{
    const char* value =
        std::getenv("FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_TEXT");
    return value != nullptr && std::string(value) == "1";
}

bool containsAll(const QByteArray& bytes,
                 const std::vector<std::string>& needles)
{
    for (const std::string& needle : needles) {
        if (!bytes.contains(needle.c_str()))
            return false;
    }
    return true;
}

void appendOptionalEnvArgument(QStringList& arguments,
                               const char* envName,
                               const char* optionName)
{
    const char* value = std::getenv(envName);
    if (value == nullptr || value[0] == '\0')
        return;

    arguments << optionName << QString::fromUtf8(value);
}

void appendNativeClipboardRetryArguments(QStringList& arguments)
{
    appendOptionalEnvArgument(
        arguments,
        "FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_COUNT",
        "--clipboard-open-retry-count");
    appendOptionalEnvArgument(
        arguments,
        "FUSIONDESK_PC_NATIVE_CLIPBOARD_OPEN_RETRY_DELAY_MS",
        "--clipboard-open-retry-delay-ms");
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
                       "pc-clipboard-client",
                       "--agent-ready-prefix",
                       "pc-clipboard-agent",
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
        profilePath("pc_clipboard_agent_listen");
    const std::filesystem::path clientProfile =
        profilePath("pc_clipboard_client_connect");
    const bool nativeClipboard = nativeClipboardValidationRequested();
    const bool qtClipboard =
        !nativeClipboard && qtClipboardValidationRequested();
    const std::string expectedText =
        nativeClipboard
            ? "fusiondesk pc native clipboard text"
            : qtClipboard
                ? "fusiondesk pc qt clipboard text"
            : "fusiondesk pc clipboard text";

    generateProfiles(clientProfile,
                     agentProfile,
                     controlEndpoint,
                     smallDataEndpoint,
                     screenEndpoint,
                     largeDataEndpoint);

    QStringList agentArguments = {"--listen-profile",
                                  QString::fromStdString(agentProfile.string()),
                                  "--session-id",
                                  "2",
                                  "--start-clipboard",
                                  "--pump-clipboard",
                                  "--clipboard-no-receive",
                                  "--wait-channels-ms",
                                  "6000",
                                  "--run-ms",
                                  nativeClipboard ? "10000" : "7000"};
    if (nativeClipboard) {
        agentArguments << "--windows-clipboard-native"
                       << "--clipboard-no-owner-suppression"
                       << "--clipboard-no-delayed-rendering"
                       << "--clipboard-seed-text"
                       << QString::fromStdString(expectedText)
                       << "--print-clipboard-diagnostics";
        appendNativeClipboardRetryArguments(agentArguments);
    } else {
        agentArguments << "--clipboard-dry-run-text"
                       << QString::fromStdString(expectedText);
    }

    QProcess agent;
    agent.start(QString::fromUtf8(FUSIONDESK_PC_AGENT_EXE), agentArguments);
    assert(agent.waitForStarted(3000));
    waitBriefly(100);

    QStringList clientArguments = {"--transport-profile",
                                   QString::fromStdString(clientProfile.string()),
                                   "--session-id",
                                   "1",
                                   "--start-clipboard",
                                   "--pump-clipboard",
                                   "--clipboard-no-announce",
                                   "--require-clipboard-text",
                                   QString::fromStdString(expectedText),
                                   "--wait-channels-ms",
                                   "6000",
                                   "--run-ms",
                                   (nativeClipboard || qtClipboard) ? "7000" : "4000"};
    if (nativeClipboard) {
        clientArguments << "--windows-clipboard-native"
                        << "--clipboard-no-owner-suppression"
                        << "--clipboard-no-delayed-rendering"
                        << "--clipboard-require-wait-ms"
                        << "3000"
                        << "--print-clipboard-diagnostics";
        appendNativeClipboardRetryArguments(clientArguments);
    } else if (qtClipboard) {
        clientArguments << "--clipboard-endpoint"
                        << "qt"
                        << "--clipboard-require-wait-ms"
                        << "3000"
                        << "--print-clipboard-diagnostics";
    }

    QProcess client;
    client.start(QString::fromUtf8(FUSIONDESK_PC_CLIENT_EXE), clientArguments);
    assert(client.waitForStarted(3000));
    assert(client.waitForFinished((nativeClipboard || qtClipboard) ? 15000
                                                                   : 10000));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard client failed: native=%d control=%s small_data=%s large_data=%s status=%d code=%d agentState=%d\n",
                     nativeClipboard ? 1 : 0,
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
    if (qtClipboard &&
        !containsAll(clientStdout,
                     {"clipboard.endpoint",
                      "kind=qt",
                      "publishes=1",
                      "publishedOffer=1"})) {
        std::fputs("qt clipboard client diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(),
                    1,
                    static_cast<std::size_t>(clientStdout.size()),
                    stderr);
        std::fputs("\n", stderr);
        if (agent.state() != QProcess::NotRunning) {
            agent.terminate();
            agent.waitForFinished(3000);
        }
        dumpProcessOutput("agent", agent);
        return 1;
    }

    assert(agent.waitForFinished((nativeClipboard || qtClipboard) ? 15000
                                                                  : 10000));
    if (agent.exitStatus() != QProcess::NormalExit || agent.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard agent failed: native=%d control=%s small_data=%s large_data=%s status=%d code=%d\n",
                     nativeClipboard ? 1 : 0,
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     largeDataEndpoint.c_str(),
                     static_cast<int>(agent.exitStatus()),
                     agent.exitCode());
        dumpProcessOutput("agent", agent);
        return 1;
    }

    std::filesystem::remove(agentProfile);
    std::filesystem::remove(clientProfile);
    return 0;
}
