#include <cassert>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        ("fusiondesk_pc_clipboard_file_" + std::to_string(now));
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
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
                       "pc-clipboard-file-client",
                       "--agent-ready-prefix",
                       "pc-clipboard-file-agent",
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
                 const std::vector<std::string>& needles)
{
    for (const std::string& needle : needles) {
        if (!bytes.contains(needle.c_str()))
            return false;
    }
    return true;
}

std::uint64_t chunkCount(const std::string& bytes, std::uint64_t chunkBytes)
{
    assert(chunkBytes != 0);
    return (static_cast<std::uint64_t>(bytes.size()) + chunkBytes - 1) /
           chunkBytes;
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
        profilePath("pc_clipboard_file_agent_listen");
    const std::filesystem::path clientProfile =
        profilePath("pc_clipboard_file_client_connect");
    const std::filesystem::path root = tempRoot();
    const std::filesystem::path payloadDir = root / "payload_dir";
    const std::filesystem::path nestedDir = payloadDir / "nested";
    std::filesystem::create_directories(nestedDir);
    const std::filesystem::path filePath = payloadDir / "alpha.txt";
    const std::filesystem::path nestedFilePath = nestedDir / "beta.txt";
    const std::filesystem::path looseFilePath = root / "loose.txt";
    const std::filesystem::path savedDir = root / "saved";
    const std::string expectedText = "fusiondesk file clipboard payload";
    const std::string nestedText = "fusiondesk nested file payload";
    const std::string looseText = "fusiondesk loose file payload";
    const std::uint64_t readChunkBytes = 16;
    writeFile(filePath, expectedText);
    writeFile(nestedFilePath, nestedText);
    writeFile(looseFilePath, looseText);
    const std::uint64_t expectedBytes =
        expectedText.size() + nestedText.size() + looseText.size();
    const std::uint64_t expectedRangeRequests =
        chunkCount(expectedText, readChunkBytes) +
        chunkCount(nestedText, readChunkBytes) +
        chunkCount(looseText, readChunkBytes);
    const bool validateQtEndpoint =
        std::getenv("FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_FILE") != nullptr &&
        std::string(std::getenv("FUSIONDESK_VALIDATE_PC_QT_CLIPBOARD_FILE")) ==
            "1";
    const std::uint64_t expectedRemotePasses = 2;
    const std::uint64_t expectedFileCount = 3;
    const std::uint64_t expectedEndpointPasses =
        validateQtEndpoint ? 1 : 0;
    const std::uint64_t expectedObjectPasses =
        expectedRemotePasses + expectedEndpointPasses;
    const std::uint64_t expectedRangeRequestTotal =
        expectedRangeRequests * expectedRemotePasses +
        expectedFileCount * expectedEndpointPasses;
    const std::uint64_t expectedByteTotal =
        expectedBytes * expectedRemotePasses +
        expectedBytes * expectedEndpointPasses;

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
                                  QString::fromStdString(payloadDir.string()),
                                  "--clipboard-seed-file",
                                  QString::fromStdString(looseFilePath.string()),
                                  "--clipboard-max-file-range-bytes",
                                  "128",
                                  "--clipboard-runtime-audit",
                                  "--print-clipboard-diagnostics",
                                  "--require-clipboard-endpoint-file-text",
                                  QString::fromStdString("payload_dir/alpha.txt=" +
                                                         expectedText),
                                  "--require-clipboard-endpoint-file-text",
                                  QString::fromStdString(
                                      "payload_dir/nested/beta.txt=" +
                                      nestedText),
                                  "--require-clipboard-endpoint-file-text",
                                  QString::fromStdString("loose.txt=" +
                                                         looseText),
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
                                   "--require-clipboard-file-text",
                                   QString::fromStdString("payload_dir/alpha.txt=" +
                                                          expectedText),
                                   "--require-clipboard-file-text",
                                   QString::fromStdString(
                                       "payload_dir/nested/beta.txt=" +
                                       nestedText),
                                   "--require-clipboard-file-text",
                                   QString::fromStdString("loose.txt=" +
                                                          looseText),
                                   "--clipboard-require-wait-ms",
                                   "3500",
                                   "--clipboard-file-read-timeout-ms",
                                   "3000",
                                   "--clipboard-file-read-chunk-bytes",
                                   QString::fromStdString(
                                       std::to_string(readChunkBytes)),
                                   "--save-clipboard-files-dir",
                                   QString::fromStdString(savedDir.string()),
                                   "--clipboard-runtime-audit",
                                   "--print-clipboard-diagnostics",
                                   "--wait-channels-ms",
                                   "6000",
                                   "--run-ms",
                                   "5000"};
    if (validateQtEndpoint) {
        clientArguments << "--clipboard-endpoint"
                        << "qt"
                        << "--clipboard-max-file-range-bytes"
                        << "128"
                        << "--require-clipboard-endpoint-file-text"
                        << QString::fromStdString("payload_dir/alpha.txt=" +
                                                  expectedText)
                        << "--require-clipboard-endpoint-file-text"
                        << QString::fromStdString(
                               "payload_dir/nested/beta.txt=" + nestedText)
                        << "--require-clipboard-endpoint-file-text"
                        << QString::fromStdString("loose.txt=" + looseText);
    }
    client.start(QString::fromUtf8(FUSIONDESK_PC_CLIENT_EXE), clientArguments);
    assert(client.waitForStarted(3000));
    assert(client.waitForFinished(15000));
    if (client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0) {
        std::fprintf(stderr,
                     "clipboard file client failed: control=%s small_data=%s large_data=%s status=%d code=%d agentState=%d\n",
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
    std::vector<std::string> clientNeedles = {
        "clipboard.module",
        "clipboard.policy",
        "clipboard.health",
        "clipboard.runtime_policy",
        "clipboard.audit",
        "operation=RemoteFileRangeRead",
        "format=application/x-fdcl-file-list",
        "audit=true",
        "mode=clipboard.policy.restricted",
        "phase=exit_postcheck",
        "clipboard.files.saved",
        "files=3",
        "bytes=" + std::to_string(expectedBytes),
        "chunks=" + std::to_string(expectedRangeRequests),
        "objectLockRequestsSent=" +
            std::to_string(expectedFileCount * expectedObjectPasses),
        "objectLockResponsesReceived=" +
            std::to_string(expectedFileCount * expectedObjectPasses),
        "objectUnlockRequestsSent=" +
            std::to_string(expectedFileCount * expectedObjectPasses),
        "objectUnlockResponsesReceived=" +
            std::to_string(expectedFileCount * expectedObjectPasses),
        "fileRangeRequestsSent=" + std::to_string(expectedRangeRequestTotal),
        "fileRangeResponsesReceived=" +
            std::to_string(expectedRangeRequestTotal),
        "fileRangeBytesReceived=" + std::to_string(expectedByteTotal),
        "pendingReads=0"};
    if (validateQtEndpoint) {
        clientNeedles.push_back("clipboard.endpoint phase=exit_postcheck");
        clientNeedles.push_back("kind=qt");
        clientNeedles.push_back("remoteFilePublishes=1");
        clientNeedles.push_back("remoteFilesMaterialized=3");
        clientNeedles.push_back("remoteDirectoriesMaterialized=1");
        clientNeedles.push_back("remoteFileBytesMaterialized=" +
                                std::to_string(expectedBytes));
    }
    if (!containsAll(clientStdout, clientNeedles)) {
        std::fputs("client clipboard file diagnostics output was missing expected fields\n", stderr);
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
    if (!std::filesystem::is_directory(savedDir / "payload_dir" / "nested") ||
        readFile(savedDir / "payload_dir" / "alpha.txt") != expectedText ||
        readFile(savedDir / "payload_dir" / "nested" / "beta.txt") !=
            nestedText ||
        readFile(savedDir / "loose.txt") != looseText) {
        std::fputs("client saved clipboard files were missing or mismatched\n",
                   stderr);
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
                     "clipboard file agent failed: control=%s small_data=%s large_data=%s status=%d code=%d\n",
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
                     {"clipboard.module",
                      "clipboard.policy",
                      "clipboard.health",
                      "clipboard.runtime_policy",
                      "clipboard.audit",
                      "operation=LocalSnapshotAnnounce",
                      "audit=true",
                      "mode=clipboard.policy.restricted",
                      "objectLockRequestsReceived=" +
                          std::to_string(expectedFileCount * expectedObjectPasses),
                      "objectLockResponsesSent=" +
                          std::to_string(expectedFileCount * expectedObjectPasses),
                      "objectUnlockRequestsReceived=" +
                          std::to_string(expectedFileCount * expectedObjectPasses),
                      "objectUnlockResponsesSent=" +
                          std::to_string(expectedFileCount * expectedObjectPasses),
                      "fileRangeRequestsReceived=" +
                          std::to_string(expectedRangeRequestTotal),
                      "fileRangeResponsesSent=" +
                          std::to_string(expectedRangeRequestTotal),
                      "fileRangeBytesSent=" +
                          std::to_string(expectedByteTotal),
                      "pendingReads=0"})) {
        std::fputs("agent clipboard file diagnostics output was missing expected fields\n", stderr);
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
