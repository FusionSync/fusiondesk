#ifndef FUSIONDESK_APPS_PC_COMMON_PC_SHELL_OPTIONS_H
#define FUSIONDESK_APPS_PC_COMMON_PC_SHELL_OPTIONS_H

#include <cstdint>
#include <string>
#include <vector>

namespace fusiondesk {
namespace apps {
namespace pc {

bool hasArg(int argc, char** argv, const std::string& name);

std::string optionValue(int argc, char** argv, const std::string& name);

std::vector<std::string> optionValues(int argc,
                                      char** argv,
                                      const std::string& name);

bool startsWith(const std::string& value, const std::string& prefix);

bool envFlagEnabled(const char* name);

void writeShellError(const std::string& message);

void writeShellMessages(const std::vector<std::string>& messages);

int intOptionValue(int argc, char** argv, const std::string& name, int fallback);

std::uint64_t uint64OptionValue(int argc,
                                char** argv,
                                const std::string& name,
                                std::uint64_t fallback);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_SHELL_OPTIONS_H
