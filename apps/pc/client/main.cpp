#include "../common/pc_app_shell.h"

int main(int argc, char** argv)
{
    return fusiondesk::apps::pc::runPcShell(argc,
                                             argv,
                                             fusiondesk::apps::pc::PcShellRole::Client);
}
