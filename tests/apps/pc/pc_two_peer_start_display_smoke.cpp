#include <cassert>
#include <cstdlib>
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
#include <QProcessEnvironment>
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

bool envEnabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

void configureOptionalH264(QProcess& process, QStringList& arguments)
{
    const bool validateExactH264 =
        envEnabled("FUSIONDESK_VALIDATE_PC_H264_DISPLAY");
    const bool validateProductionH264 =
        envEnabled("FUSIONDESK_VALIDATE_PC_H264_PRODUCTION");
    if (!validateExactH264 && !validateProductionH264)
        return;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!validateProductionH264) {
        environment.insert("FUSIONDESK_ENABLE_MF_CODEC", "1");
        environment.insert("FUSIONDESK_SELECT_MF_H264", "1");
    }
    if (envEnabled("FUSIONDESK_MF_H264_PFRAME"))
        environment.insert("FUSIONDESK_MF_H264_PFRAME", "1");
    process.setProcessEnvironment(environment);
    if (validateProductionH264) {
        arguments << "--display-codec-policy"
                  << "windows-h264-production";
    }
    if (validateExactH264) {
        arguments << "--display-codec"
                  << "h264"
                  << "--display-codec-backend"
                  << "windows.media_foundation.h264";
    }
    arguments << "--display-codec-negotiate-local"
              << "--display-target-width"
              << "640"
              << "--display-target-height"
              << "360"
              << "--print-display-codec-plan";
}

void generateProfiles(const std::filesystem::path& clientProfile,
                      const std::filesystem::path& agentProfile,
                      const std::string& controlEndpoint,
                      const std::string& smallDataEndpoint,
                      const std::string& screenEndpoint)
{
    QProcess profilePlan;
    profilePlan.start(QString::fromUtf8(FUSIONDESK_PC_PROFILE_PLAN_EXE),
                      {"--client-profile",
                       QString::fromStdString(clientProfile.string()),
                       "--agent-profile",
                       QString::fromStdString(agentProfile.string()),
                       "--client-ready-prefix",
                       "two-peer-client",
                       "--agent-ready-prefix",
                       "two-peer-agent",
                       "--channel",
                       QString::fromStdString("control=" + controlEndpoint),
                       "--channel",
                       QString::fromStdString("small_data=" + smallDataEndpoint),
                       "--channel",
                       QString::fromStdString("main_screen=" + screenEndpoint)});
    assert(profilePlan.waitForStarted(3000));
    assert(profilePlan.waitForFinished(5000));
    assert(profilePlan.exitStatus() == QProcess::NormalExit);
    assert(profilePlan.exitCode() == 0);
    assert(std::filesystem::exists(clientProfile));
    assert(std::filesystem::exists(agentProfile));
}

void generateScreenReconnectProfile(const std::filesystem::path& clientProfile,
                                    const std::filesystem::path& agentProfile,
                                    const std::string& screenEndpoint)
{
    QProcess profilePlan;
    profilePlan.start(QString::fromUtf8(FUSIONDESK_PC_PROFILE_PLAN_EXE),
                      {"--client-profile",
                       QString::fromStdString(clientProfile.string()),
                       "--agent-profile",
                       QString::fromStdString(agentProfile.string()),
                       "--client-ready-prefix",
                       "two-peer-reconnect-client",
                       "--agent-ready-prefix",
                       "two-peer-reconnect-agent",
                       "--channel",
                       QString::fromStdString("main_screen=" + screenEndpoint)});
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
    const bool validateProductionH264 =
        envEnabled("FUSIONDESK_VALIDATE_PC_H264_PRODUCTION");
    const bool validateH264 =
        envEnabled("FUSIONDESK_VALIDATE_PC_H264_DISPLAY") ||
        validateProductionH264;
    const bool validatePFrameH264 =
        envEnabled("FUSIONDESK_MF_H264_PFRAME") ||
        envEnabled("FUSIONDESK_VALIDATE_PC_H264_PRODUCTION");

    const std::array<quint16, 3> ports = nextAvailablePorts();
    const std::string controlEndpoint = "127.0.0.1:" + std::to_string(ports[0]);
    const std::string smallDataEndpoint = "127.0.0.1:" + std::to_string(ports[1]);
    const std::string screenEndpoint = "127.0.0.1:" + std::to_string(ports[2]);
    const std::filesystem::path agentProfile = profilePath("pc_agent_listen");
    const std::filesystem::path clientProfile = profilePath("pc_client_connect");
    const std::filesystem::path clientReconnectProfile = profilePath("pc_client_reconnect");
    const std::filesystem::path agentReconnectProfile = profilePath("pc_agent_reconnect_unused");

    generateProfiles(clientProfile, agentProfile, controlEndpoint, smallDataEndpoint, screenEndpoint);
    generateScreenReconnectProfile(clientReconnectProfile,
                                   agentReconnectProfile,
                                   screenEndpoint);

    QProcess agent;
    QStringList agentArguments = {"--listen-profile",
                                  QString::fromStdString(agentProfile.string()),
                                  "--print-session-diagnostics",
                                  "--print-display-runtime-diagnostics",
                                  "--start-display",
                                  "--require-display-frame",
                                  "--wait-channels-ms",
                                  "5000",
                                  "--run-ms",
                                  validatePFrameH264 ? "12000" : "8000"};
    configureOptionalH264(agent, agentArguments);
    if (validateH264) {
        agentArguments << "--display-capture-backend"
                       << "gdi";
    }
    agent.start(QString::fromUtf8(FUSIONDESK_PC_AGENT_EXE), agentArguments);
    assert(agent.waitForStarted(3000));
    waitBriefly(100);

    QProcess client;
    QStringList clientArguments = {"--transport-profile",
                                   QString::fromStdString(clientProfile.string()),
                                   "--print-session-diagnostics",
                                   "--print-display-runtime-diagnostics",
                                   "--start-display",
                                   "--display-first-frame-timeout-ms",
                                   "500",
                                   "--require-display-frame",
                                   "--wait-channels-ms",
                                   "5000",
                                   "--run-ms",
                                   validatePFrameH264 ? "12000" :
                                       (validateH264 ? "6000" : "4000"),
                                   "--reconnect-profile",
                                   QString::fromStdString(
                                       clientReconnectProfile.string()),
                                   "--reconnect-after-ms",
                                   "2000",
                                   "--reconnect-reason",
                                   "pc two peer reconnect smoke",
                                   "--print-reconnect-diagnostics"};
    configureOptionalH264(client, clientArguments);
    client.start(QString::fromUtf8(FUSIONDESK_PC_CLIENT_EXE), clientArguments);
    assert(client.waitForStarted(3000));
    assert(client.waitForFinished(validatePFrameH264 ? 20000 : 10000));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        std::fprintf(stderr,
                     "two-peer client failed: control=%s small_data=%s screen=%s clientExitStatus=%d clientExitCode=%d agentState=%d\n",
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     screenEndpoint.c_str(),
                     static_cast<int>(client.exitStatus()),
                     client.exitCode(),
                     static_cast<int>(agent.state()));
        dumpProcessOutput("client", client);
        if (agent.state() != QProcess::NotRunning) {
            if (!agent.waitForFinished(5000)) {
                agent.terminate();
                agent.waitForFinished(3000);
            }
        }
        dumpProcessOutput("agent", agent);
        assert(false);
    }

    const QByteArray clientStdout = client.readAllStandardOutput();
    if (validateH264 &&
        (!clientStdout.contains("display.codec.plan") ||
         !clientStdout.contains("display.codec.negotiation") ||
         !clientStdout.contains("session.diagnostics.display_codec") ||
         !clientStdout.contains("selected=windows.media_foundation.h264") ||
         !clientStdout.contains("selectedEncoder=windows.media_foundation.h264") ||
         !clientStdout.contains("selectedDecoder=windows.media_foundation.h264") ||
         !clientStdout.contains("adapter=windows.media_foundation.h264") ||
         !clientStdout.contains("codec=h264") ||
         (validateProductionH264 &&
          (!clientStdout.contains("selectionMode=production") ||
           !clientStdout.contains("deltaFrames=1") ||
           !clientStdout.contains("health=ok") ||
           !clientStdout.contains("codecAdapter=windows.media_foundation.h264") ||
           !clientStdout.contains("codecDeltaFrames=1"))))) {
        std::fputs("client H.264 codec diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(), 1, static_cast<std::size_t>(clientStdout.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }
    if (!clientStdout.contains("reconnect.diagnostics") ||
        !clientStdout.contains("phase=reconnect_complete") ||
        !clientStdout.contains("timerRunning=1") ||
        !clientStdout.contains("attempted=1") ||
        !clientStdout.contains("complete=1") ||
        !clientStdout.contains("ok=1") ||
        !clientStdout.contains("timeoutOk=1") ||
        !clientStdout.contains("keyframe=1") ||
        !clientStdout.contains("reason=pc two peer reconnect smoke") ||
        !clientStdout.contains("reconnect.diagnostics.channel") ||
        !clientStdout.contains("reconnect.diagnostics.stage phase=reconnect_complete name=session_rebind")) {
        std::fputs("client reconnect diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(), 1, static_cast<std::size_t>(clientStdout.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }
    if (!clientStdout.contains("display.runtime") ||
        !clientStdout.contains("phase=display_runtime_started") ||
        !clientStdout.contains("phase=exit") ||
        !clientStdout.contains("active=1") ||
        !clientStdout.contains("pumpAgentFrames=0") ||
        !clientStdout.contains("frameAttempts=") ||
        !clientStdout.contains("lastPumpUsec=") ||
        !clientStdout.contains("lastFrameAttemptUsec=") ||
        !clientStdout.contains("firstFrameSentUsec=") ||
        !clientStdout.contains("lastFrameSentUsec=") ||
        !clientStdout.contains("lastFrameAgeUsec=") ||
        !clientStdout.contains("effectiveFpsX1000=") ||
        !clientStdout.contains("sentPayloadBytes=") ||
        !clientStdout.contains("lastSentPayloadBytes=") ||
        !clientStdout.contains("effectiveBitrateKbps=") ||
        !clientStdout.contains("consecutiveFrameMisses=") ||
        !clientStdout.contains("captureErrors=0")) {
        std::fputs("client display runtime diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(), 1, static_cast<std::size_t>(clientStdout.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }
    if (!clientStdout.contains("session.diagnostics") ||
        !clientStdout.contains("phase=profile_started") ||
        !clientStdout.contains("phase=exit") ||
        !clientStdout.contains("linkReady=1") ||
        !clientStdout.contains("blocked=0") ||
        !clientStdout.contains("mounted=1") ||
        !clientStdout.contains("running=1") ||
        !clientStdout.contains("session.diagnostics.channel") ||
        !clientStdout.contains("session.diagnostics.display_codec") ||
        !clientStdout.contains("session.diagnostics.display_health") ||
        !clientStdout.contains("name=small_data") ||
        !clientStdout.contains("name=main_screen") ||
        !clientStdout.contains("ready=1")) {
        std::fputs("client session diagnostics output was missing expected fields\n", stderr);
        std::fwrite(clientStdout.constData(), 1, static_cast<std::size_t>(clientStdout.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }

    assert(agent.waitForFinished(10000));
    if (agent.exitStatus() != QProcess::NormalExit || agent.exitCode() != 0) {
        std::fprintf(stderr,
                     "two-peer agent failed: control=%s small_data=%s screen=%s agentExitStatus=%d agentExitCode=%d\n",
                     controlEndpoint.c_str(),
                     smallDataEndpoint.c_str(),
                     screenEndpoint.c_str(),
                     static_cast<int>(agent.exitStatus()),
                     agent.exitCode());
        dumpProcessOutput("agent", agent);
        assert(false);
    }
    const QByteArray agentStdout = agent.readAllStandardOutput();
    if (!agentStdout.contains("session.diagnostics") ||
        !agentStdout.contains("session.diagnostics.display_codec") ||
        !agentStdout.contains("session.diagnostics.display_health")) {
        std::fputs("agent session codec diagnostics output was missing expected fields\n", stderr);
        std::fwrite(agentStdout.constData(), 1, static_cast<std::size_t>(agentStdout.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }
    if (validateH264 &&
        (!agentStdout.contains("display.codec.plan") ||
         !agentStdout.contains("display.codec.negotiation") ||
         !agentStdout.contains("session.diagnostics.display_codec") ||
         !agentStdout.contains("selected=windows.media_foundation.h264") ||
         !agentStdout.contains("selectedEncoder=windows.media_foundation.h264") ||
         !agentStdout.contains("selectedDecoder=windows.media_foundation.h264") ||
         !agentStdout.contains("adapter=windows.media_foundation.h264") ||
         !agentStdout.contains("codec=h264") ||
         (validateProductionH264 &&
          (!agentStdout.contains("selectionMode=production") ||
           !agentStdout.contains("deltaFrames=1") ||
           !agentStdout.contains("health=ok") ||
           !agentStdout.contains("codecAdapter=windows.media_foundation.h264") ||
           !agentStdout.contains("codecDeltaFrames=1"))))) {
        std::fputs("agent H.264 codec diagnostics output was missing expected fields\n", stderr);
        std::fwrite(agentStdout.constData(), 1, static_cast<std::size_t>(agentStdout.size()), stderr);
        std::fputs("\n", stderr);
        assert(false);
    }

    std::filesystem::remove(agentProfile);
    std::filesystem::remove(clientProfile);
    std::filesystem::remove(agentReconnectProfile);
    std::filesystem::remove(clientReconnectProfile);
    return 0;
}
