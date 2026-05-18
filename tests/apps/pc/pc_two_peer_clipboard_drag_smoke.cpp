#include <cassert>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::filesystem::path tempRoot()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("fusiondesk_pc_clipboard_drag_" + std::to_string(now));
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
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
                       "pc-clipboard-drag-client",
                       "--agent-ready-prefix",
                       "pc-clipboard-drag-agent",
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
        profilePath("pc_clipboard_drag_agent_listen");
    const std::filesystem::path clientProfile =
        profilePath("pc_clipboard_drag_client_connect");
    const std::filesystem::path root = tempRoot();
    const std::filesystem::path filePath = root / "drag.txt";
    writeFile(filePath, "fusiondesk drag payload");

    generateProfiles(clientProfile,
                     agentProfile,
                     controlEndpoint,
                     smallDataEndpoint,
                     screenEndpoint,
                     largeDataEndpoint);

    QProcess agent;
    QStringList agentArguments = {"--listen-profile",
                                  QString::fromStdString(agentProfile.string()),
                                  "--session-id",
                                  "2",
                                  "--start-clipboard",
                                  "--pump-clipboard",
                                  "--clipboard-no-receive",
                                  "--clipboard-seed-file",
                                  QString::fromStdString(filePath.string()),
                                  "--clipboard-send-drag-drop",
                                  "--clipboard-drag-offer-wait-ms",
                                  "2500",
                                  "--clipboard-drag-session-id",
                                  "7701",
                                  "--clipboard-drag-start-x",
                                  "12",
                                  "--clipboard-drag-start-y",
                                  "24",
                                  "--clipboard-drag-move-x",
                                  "34",
                                  "--clipboard-drag-move-y",
                                  "46",
                                  "--clipboard-drag-drop-x",
                                  "56",
                                  "--clipboard-drag-drop-y",
                                  "68",
                                  "--print-clipboard-diagnostics",
                                  "--wait-channels-ms",
                                  "6000",
                                  "--run-ms",
                                  "8500"};
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
                                   "--print-clipboard-diagnostics",
                                   "--wait-channels-ms",
                                   "6000",
                                   "--run-ms",
                                   "5500"};
    client.start(QString::fromUtf8(FUSIONDESK_PC_CLIENT_EXE), clientArguments);
    assert(client.waitForStarted(3000));
    assert(client.waitForFinished(15000));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard drag client failed: control=%s small_data=%s large_data=%s status=%d code=%d agentState=%d\n",
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
                     {"clipboard.module",
                      "dragStartsReceived=1",
                      "dragMovesReceived=1",
                      "dragDropsReceived=1",
                      "dragNativePublicationFailures=0",
                      "clipboard.endpoint",
                      "dragStarts=1",
                      "dragMoves=1",
                      "dragDrops=1",
                      "activeDrag=0",
                      "lastDragSession=7701",
                      "lastDragX=56",
                      "lastDragY=68"})) {
        std::fputs("client clipboard drag diagnostics output was missing expected fields\n", stderr);
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

    assert(agent.waitForFinished(15000));
    if (agent.exitStatus() != QProcess::NormalExit || agent.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard drag agent failed: control=%s small_data=%s large_data=%s status=%d code=%d\n",
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
                     {"clipboard.drag.sent",
                      "session=7701",
                      "clipboard.module",
                      "dragStartsSent=1",
                      "dragMovesSent=1",
                      "dragDropsSent=1"})) {
        std::fputs("agent clipboard drag diagnostics output was missing expected fields\n", stderr);
        std::fwrite(agentStdout.constData(),
                    1,
                    static_cast<std::size_t>(agentStdout.size()),
                    stderr);
        std::fputs("\n", stderr);
        return 1;
    }

    std::filesystem::remove(agentProfile);
    std::filesystem::remove(clientProfile);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return 0;
}
