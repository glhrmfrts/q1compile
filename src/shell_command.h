#pragma once

#include <string>

struct ShellCommand
{
    explicit ShellCommand(const std::string& cmd, const std::string& pwd);

    ~ShellCommand();

    bool ReadChar(char& c);

    FILE* handle;
};
