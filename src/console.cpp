#include "console.h"

static std::vector<LogEntry> entries;

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
    if (c == '\n') {
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