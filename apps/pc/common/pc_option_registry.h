#ifndef FUSIONDESK_APPS_PC_COMMON_PC_OPTION_REGISTRY_H
#define FUSIONDESK_APPS_PC_COMMON_PC_OPTION_REGISTRY_H

#include <iosfwd>
#include <string>
#include <vector>

namespace fusiondesk {
namespace apps {
namespace pc {

enum class PcOptionValueType
{
    Flag,
    String,
    Integer,
    UnsignedInteger,
    Path,
    Enum
};

enum class PcOptionVisibility
{
    User,
    Advanced,
    AdminPolicy,
    Diagnostics,
    Developer,
    Test
};

struct PcOptionDefinition
{
    std::string name;
    std::string valueName;
    PcOptionValueType valueType = PcOptionValueType::Flag;
    bool repeatable = false;
    std::vector<std::string> enumValues;
    std::string owner;
    std::string configPath;
    std::string guiGroup;
    std::string guiLabel;
    PcOptionVisibility visibility = PcOptionVisibility::Developer;
    std::string description;
};

struct PcOptionValidationResult
{
    bool ok = true;
    std::vector<std::string> messages;
};

const std::vector<PcOptionDefinition>& pcOptionDefinitions();

const PcOptionDefinition* findPcOptionDefinition(const std::string& name);

bool pcShellHelpRequested(int argc, char** argv);

bool pcShellGuiConfigModelRequested(int argc, char** argv);

PcOptionValidationResult validatePcShellOptions(int argc, char** argv);

void writePcShellHelp(std::ostream& output,
                      const std::string& executableName,
                      bool includeDeveloperOptions);

void writePcShellGuiConfigModelJson(std::ostream& output);

const char* pcOptionValueTypeName(PcOptionValueType type);

const char* pcOptionVisibilityName(PcOptionVisibility visibility);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_OPTION_REGISTRY_H
