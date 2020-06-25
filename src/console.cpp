#include <memory>
#include <fstream>
#include <chrono>
#include <ctime>
#include "console.h"

static std::vector<LogEntry> entries;
static std::unique_ptr<std::ofstream> error_log_file;

static void AddNewEntry(LogLevel level)
{
    entries.push_back({ "", level });
}

static void Append(char c)
{
    entries.back().text.append(&c, 1);
}

static LogLevel CurrentLevel()
{
    return entries.back().level;
}

static void PrintLevel(LogLevel level, char c)
{
    if (level == LOG_ERROR && error_log_file.get()) {
        (*error_log_file) << c;
    }

    if (c == '\n') {
        if (error_log_file.get()) error_log_file->flush();
        
        AddNewEntry(level);
    }
    else {
        if (level != CurrentLevel()) {
            AddNewEntry(level);
        }
        Append(c);
    }
}

static void PrintLevel(LogLevel level, const char* cstr)
{
    const char* it = cstr;
    while (*it) {
        PrintLevel(level, *it);
        it++;
    }
}

void SetErrorLogFile(const std::string& path)
{
    error_log_file = std::make_unique<std::ofstream>(path);

    auto now_clock = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now_clock);
    (*error_log_file) << "q1compile log start: " << std::ctime(&now_time) << "\n\n";
    error_log_file->flush();
}

void ClearConsole()
{
    entries.clear();
    AddNewEntry(LOG_INFO);
}

void Print(char c)
{
    PrintLevel(LOG_INFO, c);
}

void Print(const char* cstr)
{
    PrintLevel(LOG_INFO, cstr);
}

void PrintError(char c)
{
    PrintLevel(LOG_ERROR, c);
}

void PrintError(const char* cstr)
{
    PrintLevel(LOG_ERROR, cstr);
}

std::vector<LogEntry>& GetLogEntries()
{
    return entries;
}