#include "keybind.h"

namespace keybind {

void KeyBindState::AddKeyBind(const std::string& keys, KeyBindCommandType type, const KeyBindCommandArgs& args) {
    auto cmd = std::make_unique<KeyBindCommand>();
    cmd->type = type;
    cmd->args = args;
    binds[keys] = std::move(cmd);
}

const KeyBindCommand* KeyBindState::GetCommand(const std::string& keys) const {
    auto it = binds.find(keys);
    if (it == binds.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::string GetKeysString(char key, bool ctrl, bool alt, bool shift) {
    std::string str{};
    if (ctrl) {
        str.append("Ctrl+");
    }
    if (alt) {
        str.append("Alt+");
    }
    if (shift) {
        str.append("Shift+");
    }
    str.append(&key, 1);
    return str;
}


KeyBindCommandType ParseCommandType(const std::string& str) {
    if (str == "compile") {
        return KEYCMD_COMPILE;
    }
    if (str == "compile_with_preset") {
        return KEYCMD_COMPILE_WITH_PRESET;
    }
    if (str == "shell_command") {
        return KEYCMD_SHELL_COMMAND;
    }
    return KEYCMD_INVALID;
}

std::string GetCommandTypeString(KeyBindCommandType type) {
    switch (type) {
    case KEYCMD_COMPILE:
        return "compile";
    case KEYCMD_COMPILE_WITH_PRESET:
        return "compile_with_preset";
    case KEYCMD_SHELL_COMMAND:
        return "shell_command";
    default:
        return "";
    }
}

}