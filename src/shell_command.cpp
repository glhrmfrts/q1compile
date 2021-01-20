#include <cstdio>
#include <stdexcept>
#include "shell_command.h"

namespace shell_command {

ShellCommand::ShellCommand(const std::string& cmd, const std::string& pwd)
{
    handle = _popen(cmd.c_str(), "r");
}

ShellCommand::~ShellCommand()
{
    _pclose(handle);
}

bool ShellCommand::ReadChar(char& c)
{
    bool eof = feof(handle);
    if (!eof) {
        c = fgetc(handle);
        return true;
    }
    return false;
}

}