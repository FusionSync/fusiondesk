#include "../common/pc_app_shell.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

bool equals(const char* left, const char* right)
{
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

bool parseRoleValue(const char* value,
                    fusiondesk::apps::pc::PcShellRole& role)
{
    if (equals(value, "client")) {
        role = fusiondesk::apps::pc::PcShellRole::Client;
        return true;
    }
    if (equals(value, "agent")) {
        role = fusiondesk::apps::pc::PcShellRole::Agent;
        return true;
    }
    return false;
}

void printClipHelp()
{
    std::cout
        << "FusionDesk clipboard CLI\n"
        << "\n"
        << "Usage:\n"
        << "  fusiondesk_clip --clip-role client [pc shell options]\n"
        << "  fusiondesk_clip --clip-role agent  [pc shell options]\n"
        << "  fusiondesk_clip client [pc shell options]\n"
        << "  fusiondesk_clip agent  [pc shell options]\n"
        << "\n"
        << "Common clipboard options:\n"
        << "  --start-clipboard --pump-clipboard\n"
        << "  --clipboard-endpoint auto|windows|macos|qt\n"
        << "  --clipboard-seed-text <text>\n"
        << "  --require-clipboard-text <text>\n"
        << "  --print-clipboard-diagnostics\n"
        << "\n"
        << "This executable reuses the normal PC shell runtime and only selects\n"
        << "whether the process runs as the clipboard client or agent side.\n";
}

bool resolveClipRole(int argc,
                     char** argv,
                     fusiondesk::apps::pc::PcShellRole& role)
{
    role = fusiondesk::apps::pc::PcShellRole::Client;
    for (int index = 1; index < argc; ++index) {
        const char* arg = argv[index];
        if (arg == nullptr)
            continue;

        constexpr const char* prefix = "--clip-role=";
        constexpr std::size_t prefixLength = 12;
        if (std::strncmp(arg, prefix, prefixLength) == 0)
            return parseRoleValue(arg + prefixLength, role);

        if (equals(arg, "--clip-role")) {
            if (index + 1 >= argc)
                return false;
            return parseRoleValue(argv[index + 1], role);
        }

        if (index == 1 && parseRoleValue(arg, role))
            return true;
    }
    return true;
}

std::vector<char*> pcShellArguments(int argc, char** argv)
{
    std::vector<char*> result;
    if (argc > 0 && argv[0] != nullptr)
        result.push_back(argv[0]);

    for (int index = 1; index < argc; ++index) {
        char* arg = argv[index];
        if (arg == nullptr)
            continue;

        constexpr const char* prefix = "--clip-role=";
        constexpr std::size_t prefixLength = 12;
        if (std::strncmp(arg, prefix, prefixLength) == 0)
            continue;

        if (equals(arg, "--clip-role")) {
            if (index + 1 < argc)
                ++index;
            continue;
        }

        if (index == 1) {
            fusiondesk::apps::pc::PcShellRole ignored =
                fusiondesk::apps::pc::PcShellRole::Client;
            if (parseRoleValue(arg, ignored))
                continue;
        }

        result.push_back(arg);
    }

    return result;
}

} // namespace

int main(int argc, char** argv)
{
    for (int index = 1; index < argc; ++index) {
        if (equals(argv[index], "--clip-help")) {
            printClipHelp();
            return 0;
        }
    }

    fusiondesk::apps::pc::PcShellRole role =
        fusiondesk::apps::pc::PcShellRole::Client;
    if (!resolveClipRole(argc, argv, role)) {
        std::cerr << "invalid --clip-role, expected client or agent\n";
        return 2;
    }

    std::vector<char*> shellArguments = pcShellArguments(argc, argv);
    return fusiondesk::apps::pc::runPcShell(
        static_cast<int>(shellArguments.size()),
        shellArguments.data(),
        role);
}
