#include "pc_option_registry.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool validate(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));

    const fusiondesk::apps::pc::PcOptionValidationResult result =
        fusiondesk::apps::pc::validatePcShellOptions(
            static_cast<int>(argv.size()),
            argv.data());
    if (!result.ok) {
        for (const std::string& message : result.messages)
            std::cerr << message << std::endl;
    }
    return result.ok;
}

} // namespace

int main()
{
    if (!validate({"fusiondesk_pc_client",
                   "--display-scale-mode",
                   "fit",
                   "--clipboard-endpoint=linux",
                   "--profile-module",
                   "clipboard.redirect",
                   "--clipboard-drag-start-x",
                   "-10"})) {
        return 1;
    }

    if (validate({"fusiondesk_pc_client", "--not-a-real-option"}))
        return 2;

    if (validate({"fusiondesk_pc_client", "--clipboard-endpoint", "plan9"}))
        return 3;

    if (validate({"fusiondesk_pc_client", "--display-source-id"}))
        return 4;

    if (validate({"fusiondesk_pc_client", "--show-display-window="}))
        return 10;

    if (validate({"fusiondesk_pc_client", "--display-scale-mode="}))
        return 11;

    std::ostringstream help;
    fusiondesk::apps::pc::writePcShellHelp(help,
                                           "fusiondesk_pc_client",
                                           false);
    const std::string helpText = help.str();
    if (helpText.find("--display-scale-mode") == std::string::npos ||
        helpText.find("--smoke") != std::string::npos) {
        return 5;
    }

    std::ostringstream allHelp;
    fusiondesk::apps::pc::writePcShellHelp(allHelp,
                                           "fusiondesk_pc_client",
                                           true);
    if (allHelp.str().find("--smoke") == std::string::npos)
        return 6;
    if (allHelp.str().find("adapter-idForce") != std::string::npos)
        return 8;

    std::ostringstream guiModel;
    fusiondesk::apps::pc::writePcShellGuiConfigModelJson(guiModel);
    const std::string model = guiModel.str();
    if (model.find("\"path\": \"clipboard.files.maxFileCount\"") ==
            std::string::npos ||
        model.find("\"path\": \"display.scaleMode\"") ==
            std::string::npos) {
        return 9;
    }

    return 0;
}
