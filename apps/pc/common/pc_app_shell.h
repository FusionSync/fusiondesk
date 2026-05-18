#ifndef FUSIONDESK_APPS_PC_COMMON_PC_APP_SHELL_H
#define FUSIONDESK_APPS_PC_COMMON_PC_APP_SHELL_H

namespace fusiondesk {
namespace apps {
namespace pc {

enum class PcShellRole
{
    Client,
    Agent
};

int runPcShell(int argc, char** argv, PcShellRole role);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_APP_SHELL_H
