#pragma once

#include <string>

namespace shell_command {

struct ShellCommand
{
    explicit ShellCommand(const std::string& cmd, const std::string& pwd);

    ~ShellCommand();

    bool Good() const;

    bool ReadChar(char& c);

    FILE* handle;
};

}