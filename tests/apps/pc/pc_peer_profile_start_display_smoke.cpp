#include <cassert>
#include <cstdio>
#include <array>
#include <chrono>
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

std::array<quint16, 3> nextAvailablePorts()
{
    QTcpServer controlProbe;
    QTcpServer smallDataProbe;
    QTcpServer screenProbe;
    assert(controlProbe.listen(QHostAddress::LocalHost, 0));
    assert(smallDataProbe.listen(QHostAddress::LocalHost, 0));
    assert(screenProbe.listen(QHostAddress::LocalHost, 0));
    const std::array<quint16, 3> ports = {
        controlProbe.serverPort(),
        smallDataProbe.serverPort(),
        screenProbe.serverPort(),
    };
    controlProbe.close();
    smallDataProbe.close();
    screenProbe.close();
    assert(ports[0] != ports[1]);
    assert(ports[0] != ports[2]);
    assert(ports[1] != ports[2]);
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
    return
        std::filesystem::temp_directory_path() /
        ("fusiondesk_" + name + "_" + std::to_string(now) + ".json");
}

void dumpProcessOutput(const char* label, QProcess& process)
{
    const QByteArray stdoutBytes = process.readAllStandardOutput();
    const QByteArray stderrBytes = process.readAllStandardError();
    if (!stdoutBytes.isEmpty()) {
        std::fputs(label, stderr);
        std::fputs(" stdout:\n", stderr);
        std::fwrite(stdoutBytes.constData(), 1, static_cast<std::size_t>(stdoutBytes.size()), stderr);
        std::fputs("\n", stderr);
    }
    if (!stderrBytes.isEmpty()) {
        std::fputs(label, stderr);
        std::fputs(" stderr:\n", stderr);
        std::fwrite(stderrBytes.constData(), 1, static_cast<std::size_t>(stderrBytes.size()), stderr);
        std::fputs("\n", stderr);
    }
}

void generateControlProfiles(const std::filesystem::path& clientProfile,
                             const std::filesystem::path& agentProfile,
                             const std::string& controlEndpoint)
{
    QProcess profilePlan;
    profilePlan.start(QString::fromUtf8(FUSIONDESK_PC_PROFILE_PLAN_EXE),
                      {"--client-profile",
                       QString::fromStdString(clientProfile.string()),
                       "--agent-profile",
                       QString::fromStdString(agentProfile.string()),
                       "--client-ready-prefix",
                       "pc-fdpp-client",
                       "--agent-ready-prefix",
                       "pc-fdpp-agent",
                       "--channel",
                       QString::fromStdString("control=" + controlEndpoint)});
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

    const std::array<quint16, 3> ports = nextAvailablePorts();
    const std::string controlEndpoint = "127.0.0.1:" + std::to_string(ports[0]);
    const std::string smallDataEndpoint = "127.0.0.1:" + std::to_string(ports[1]);
    const std::string screenEndpoint = "127.0.0.1:" + std::to_string(ports[2]);
    const std::filesystem::path agentProfile = profilePath("pc_fdpp_agent_control");
    const std::filesystem::path clientProfile = profilePath("pc_fdpp_client_control");

    generateControlProfiles(clientProfile, agentProfile, controlEndpoint);

    QProcess agent;
    agent.start(QString::fromUtf8(FUSIONDESK_PC_AGENT_EXE),
                {"--listen-profile",
                 QString::fromStdString(agentProfile.string()),
                 "--peer-profile-service",
                 "--display-codec-negotiate-fdpp",
                 "--print-display-codec-plan",
                 "--module-inventory-service",
                 "--module-inventory-wait-ms",
                 "6000",
                 "--print-module-inventory-diagnostics",
                 "--start-display",
                 "--require-display-frame",
                 "--wait-channels-ms",
                 "6000",
                 "--run-ms",
                 "8000"});
    assert(agent.waitForStarted(3000));
    waitBriefly(100);

    QProcess client;
    client.start(QString::fromUtf8(FUSIONDESK_PC_CLIENT_EXE),
                 {"--transport-profile",
                  QString::fromStdString(clientProfile.string()),
                  "--peer-profile-channel",
                  QString::fromStdString("small_data=" + smallDataEndpoint),
                  "--peer-profile-channel",
                  QString::fromStdString("main_screen=" + screenEndpoint),
                  "--peer-profile-wait-ms",
                  "6000",
                  "--display-codec-negotiate-fdpp",
                  "--print-display-codec-plan",
                  "--module-inventory-request",
                  "--module-inventory-wait-ms",
                  "6000",
                  "--print-module-inventory-diagnostics",
                  "--print-session-diagnostics",
                  "--start-display",
                  "--require-display-frame",
                  "--wait-channels-ms",
                  "6000",
                  "--run-ms",
                  "4000"});
    assert(client.waitForStarted(3000));
    assert(client.waitForFinished(10000));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        std::fprintf(stderr,
                     "peer-profile client failed: control=%s small_data=%s screen=%s clientExitStatus=%d clientExitCode=%d agentState=%d\n",
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     screenEndpoint.c_str(),
                     static_cast<int>(client.exitStatus()),
                     client.exitCode(),
                     static_cast<int>(agent.state()));
        dumpProcessOutput("client", client);
        if (agent.state() != QProcess::NotRunning) {
            agent.terminate();
            agent.waitForFinished(3000);
        }
        dumpProcessOutput("agent", agent);
        assert(false);
    }

    assert(agent.waitForFinished(10000));
    if (agent.exitStatus() != QProcess::NormalExit || agent.exitCode() != 0) {
        std::fprintf(stderr,
                     "peer-profile agent failed: control=%s small_data=%s screen=%s agentExitStatus=%d agentExitCode=%d\n",
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     screenEndpoint.c_str(),
                     static_cast<int>(agent.exitStatus()),
                     agent.exitCode());
        dumpProcessOutput("agent", agent);
        assert(false);
    }

    const QByteArray clientOutput =
        client.readAllStandardOutput() + client.readAllStandardError();
    const QByteArray agentOutput =
        agent.readAllStandardOutput() + agent.readAllStandardError();
    if (!clientOutput.contains("module.inventory.completion")) {
        std::fputs("client output did not include module inventory completion diagnostics\n", stderr);
        std::fwrite(clientOutput.constData(), 1, static_cast<std::size_t>(clientOutput.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }
    if (!clientOutput.contains("session.diagnostics.remote_module")) {
        std::fputs("client output did not include remote module session diagnostics\n", stderr);
        std::fwrite(clientOutput.constData(), 1, static_cast<std::size_t>(clientOutput.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }
    if (!clientOutput.contains("display.codec.negotiation") ||
        !clientOutput.contains("phase=peer_profile_client_response") ||
        !clientOutput.contains("selectedEncoder=windows.raw_frame") ||
        !clientOutput.contains("selectedDecoder=windows.raw_frame") ||
        !clientOutput.contains("codec=raw_bgra")) {
        std::fputs("client output did not include FDPP codec negotiation diagnostics\n", stderr);
        std::fwrite(clientOutput.constData(), 1, static_cast<std::size_t>(clientOutput.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }
    if (!agentOutput.contains("display.codec.negotiation") ||
        !agentOutput.contains("phase=peer_profile_agent_response") ||
        !agentOutput.contains("selectedEncoder=windows.raw_frame") ||
        !agentOutput.contains("selectedDecoder=windows.raw_frame") ||
        !agentOutput.contains("codec=raw_bgra")) {
        std::fputs("agent output did not include FDPP codec negotiation diagnostics\n", stderr);
        std::fwrite(agentOutput.constData(), 1, static_cast<std::size_t>(agentOutput.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }

    std::filesystem::remove(agentProfile);
    std::filesystem::remove(clientProfile);
    return 0;
}
