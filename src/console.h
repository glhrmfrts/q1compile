#pragma once

#include <string>
#include <vector>

namespace console {

enum LogLevel
{
    LOG_INFO,
    LOG_ERROR,
};

struct LogEntry
{
    std::string text;
    LogLevel level;
};

void ClearConsole();

void SetErrorLogFile(const std::string& path);

void SetPrintToFile(bool b);

void Print(char c);

void Print(const char* cstr);

void PrintError(char c);

void PrintError(const char* cstr);

std::vector<LogEntry>& GetLogEntries();

template<typename T> void PrintValue(const T& arg) {
    std::string str = std::to_string(arg);
    Print(str.c_str());
}

template<typename T> void PrintValueError(const T& arg) {
    std::string str = std::to_string(arg);
    PrintError(str.c_str());
}

}