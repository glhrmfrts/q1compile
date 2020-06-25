#pragma once

#include <string>

struct SubProcess
{
    explicit SubProcess(const std::string& cmd, const std::string& pwd);

    ~SubProcess();

    bool ReadChar(char& c);

    bool Good();

    void* handle;
};