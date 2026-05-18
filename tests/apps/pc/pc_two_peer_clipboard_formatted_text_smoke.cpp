#include <cassert>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

struct FormattedScenario final {
    const char* name = nullptr;
    const char* seedOption = nullptr;
    const char* requireOption = nullptr;
    const char* fileName = nullptr;
    const char* bytes = nullptr;
};

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

std::filesystem::path uniquePath(const std::string& name)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fusiondesk_" + name + "_" + std::to_string(now));
}

void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    assert(file.good());
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
                      const std::string& largeDataEndpoint,
                      const std::string& prefix)
{
    QProcess profilePlan;
    profilePlan.start(QString::fromUtf8(FUSIONDESK_PC_PROFILE_PLAN_EXE),
                      {"--client-profile",
                       QString::fromStdString(clientProfile.string()),
                       "--agent-profile",
                       QString::fromStdString(agentProfile.string()),
                       "--client-ready-prefix",
                       QString::fromStdString(prefix + "-client"),
                       "--agent-ready-prefix",
                       QString::fromStdString(prefix + "-agent"),
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

bool runScenario(const FormattedScenario& scenario)
{
    const std::array<quint16, 4> ports = nextAvailablePorts();
    const std::string controlEndpoint = "127.0.0.1:" + std::to_string(ports[0]);
    const std::string smallDataEndpoint = "127.0.0.1:" + std::to_string(ports[1]);
    const std::string screenEndpoint = "127.0.0.1:" + std::to_string(ports[2]);
    const std::string largeDataEndpoint = "127.0.0.1:" + std::to_string(ports[3]);
    const std::filesystem::path root =
        uniquePath(std::string("pc_clipboard_") + scenario.name);
    std::filesystem::create_directories(root);
    const std::filesystem::path agentProfile =
        root / "agent_clipboard.json";
    const std::filesystem::path clientProfile =
        root / "client_clipboard.json";
    const std::filesystem::path payloadPath = root / scenario.fileName;
    writeFile(payloadPath, scenario.bytes);

    generateProfiles(clientProfile,
                     agentProfile,
                     controlEndpoint,
                     smallDataEndpoint,
                     screenEndpoint,
                     largeDataEndpoint,
                     std::string("pc-clipboard-") + scenario.name);

    QStringList agentArguments = {"--listen-profile",
                                  QString::fromStdString(agentProfile.string()),
                                  "--session-id",
                                  "2",
                                  "--start-clipboard",
                                  "--pump-clipboard",
                                  "--clipboard-no-receive",
                                  scenario.seedOption,
                                  QString::fromStdString(payloadPath.string()),
                                  "--print-clipboard-diagnostics",
                                  "--wait-channels-ms",
                                  "6000",
                                  "--run-ms",
                                  "7000"};

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
                                   scenario.requireOption,
                                   QString::fromStdString(payloadPath.string()),
                                   "--print-clipboard-diagnostics",
                                   "--clipboard-require-wait-ms",
                                   "3000",
                                   "--wait-channels-ms",
                                   "6000",
                                   "--run-ms",
                                   "4000"};

    QProcess client;
    client.start(QString::fromUtf8(FUSIONDESK_PC_CLIENT_EXE),
                 clientArguments);
    assert(client.waitForStarted(3000));
    assert(client.waitForFinished(10000));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard %s client failed: control=%s small_data=%s large_data=%s root=%s status=%d code=%d agentState=%d\n",
                     scenario.name,
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     largeDataEndpoint.c_str(),
                     root.string().c_str(),
                     static_cast<int>(client.exitStatus()),
                     client.exitCode(),
                     static_cast<int>(agent.state()));
        dumpProcessOutput("client", client);
        if (agent.state() != QProcess::NotRunning) {
            agent.terminate();
            agent.waitForFinished(3000);
        }
        dumpProcessOutput("agent", agent);
        return false;
    }

    assert(agent.waitForFinished(10000));
    if (agent.exitStatus() != QProcess::NormalExit || agent.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard %s agent failed: control=%s small_data=%s large_data=%s status=%d code=%d\n",
                     scenario.name,
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     largeDataEndpoint.c_str(),
                     static_cast<int>(agent.exitStatus()),
                     agent.exitCode());
        dumpProcessOutput("agent", agent);
        return false;
    }

    std::filesystem::remove_all(root);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    const FormattedScenario html = {
        "html",
        "--clipboard-seed-html-file",
        "--require-clipboard-html-file",
        "payload.html",
        "<!doctype html><meta charset=\"utf-8\"><p>fusiondesk <b>html</b></p>"};
    const FormattedScenario rtf = {
        "rtf",
        "--clipboard-seed-rtf-file",
        "--require-clipboard-rtf-file",
        "payload.rtf",
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0 "
        "fusiondesk \\b rtf\\b0 payload}"};

    return runScenario(html) && runScenario(rtf) ? 0 : 1;
}
