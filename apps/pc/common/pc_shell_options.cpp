#include "pc_shell_options.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>

namespace fusiondesk {
namespace apps {
namespace pc {

bool hasArg(int argc, char** argv, const std::string& name)
{
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && name == argv[index])
            return true;
    }
    return false;
}

std::string optionValue(int argc, char** argv, const std::string& name)
{
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] != nullptr && name == argv[index] &&
            argv[index + 1] != nullptr) {
            return argv[index + 1];
        }
    }
    return {};
}

std::vector<std::string> optionValues(int argc,
                                      char** argv,
                                      const std::string& name)
{
    std::vector<std::string> result;
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] != nullptr && name == argv[index] &&
            argv[index + 1] != nullptr) {
            result.push_back(argv[index + 1]);
        }
    }
    return result;
}

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool envFlagEnabled(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
        return false;

    std::string normalized(value);
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return normalized == "1" || normalized == "true" ||
           normalized == "yes" || normalized == "on";
}

void writeShellError(const std::string& message)
{
    std::cerr << message << std::endl;
}

void writeShellMessages(const std::vector<std::string>& messages)
{
    for (const std::string& message : messages)
        writeShellError(message);
}

int intOptionValue(int argc, char** argv, const std::string& name, int fallback)
{
    const std::string value = optionValue(argc, argv, name);
    if (value.empty())
        return fallback;

    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < 0)
        return fallback;

    return static_cast<int>(parsed);
}

std::uint64_t uint64OptionValue(int argc,
                                char** argv,
                                const std::string& name,
                                std::uint64_t fallback)
{
    const std::string value = optionValue(argc, argv, name);
    if (value.empty())
        return fallback;

    char* end = nullptr;
    const unsigned long long parsed =
        std::strtoull(value.c_str(), &end, 0);
    if (end == nullptr || *end != '\0')
        return fallback;

    return static_cast<std::uint64_t>(parsed);
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
