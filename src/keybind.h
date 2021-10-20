#pragma once

#include <array>
#include <unordered_map>
#include <memory>

namespace keybind {

enum KeyBindCommandType : unsigned int
{
    KEYCMD_INVALID,
    KEYCMD_COMPILE,
    KEYCMD_COMPILE_WITH_PRESET,
    KEYCMD_SHELL_COMMAND,
};

using KeyBindCommandArgs = std::array<std::string, 4>;

struct KeyBindCommand
{
    KeyBindCommandType type;
    KeyBindCommandArgs args;
};

struct KeyBindState
{
    std::unordered_map<std::string, std::unique_ptr<KeyBindCommand>> binds;

    void AddKeyBind(const std::string& keys, KeyBindCommandType type, const KeyBindCommandArgs& args);

    const KeyBindCommand* GetCommand(const std::string& keys) const;
};

std::string GetKeysString(char key, bool ctrl = false, bool alt = false, bool shift = false);

KeyBindCommandType ParseCommandType(const std::string& str);

std::string GetCommandTypeString(KeyBindCommandType type);

}