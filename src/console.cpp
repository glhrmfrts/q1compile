#include <memory>
#include <fstream>
#include <chrono>
#include <ctime>
#include "console.h"

static struct ConsoleState {
    std::vector<LogEntry> entries;
    std::unique_ptr<std::ofstream> error_log_file;
    std::size_t cr_count = 0;
} g_console;

static void AddNewEntry(LogLevel level)
{
    g_console.entries.push_back({ "", level });
}

static void Append(char c)
{
    g_console.entries.back().text.append(&c, 1);
}

static LogLevel CurrentLevel()
{
    return g_console.entries.back().level;
}

static void PrintLevel(LogLevel level, char c)
{
    if (level == LOG_ERROR && g_console.error_log_file.get()) {
        (*g_console.error_log_file) << c;
    }

    if (c == '\n') {
        if (g_console.error_log_file.get()) g_console.error_log_file->flush();
        
        AddNewEntry(level);
        g_console.cr_count = 0;
    }
    else if (c == '\r') {
        g_console.cr_count++;
    }
    else {
        if (level != CurrentLevel()) {
            AddNewEntry(level);
        }

        if (g_console.cr_count > 0) {
            g_console.entries.back().text = "";
            g_console.cr_count = 0;
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
    g_console.error_log_file = std::make_unique<std::ofstream>(path);

    auto now_clock = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now_clock);
    (*g_console.error_log_file) << "q1compile log start: " << std::ctime(&now_time) << std::endl;
    g_console.error_log_file->flush();
}

void ClearConsole()
{
    g_console.entries.clear();
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
    return g_console.entries;
}