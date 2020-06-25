#pragma once

#include <string>
#include <vector>

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

void Print(char c);

void Print(const char* cstr);

void PrintError(char c);

void PrintError(const char* cstr);

std::vector<LogEntry>& GetLogEntries();
