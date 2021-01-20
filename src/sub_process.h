#pragma once

#include <string>

namespace sub_process {

struct SubProcess
{
    explicit SubProcess(const std::string& cmd, const std::string& pwd);

    ~SubProcess();

    bool ReadChar(char& c);

    bool Good();

    void* handle;
};

bool StartDetachedProcess(const std::string& cmd, const std::string& pwd);

}