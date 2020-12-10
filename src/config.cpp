#include <fstream>
#include <sstream>
#include <functional>
#include <cctype>
#include <string>
#include "config.h"
#include "common.h"
#include "path.h"

struct ConfigLineParser
{
    explicit ConfigLineParser(std::string l) : _line{ l }, _size{ l.size() } {}

    char Ch() { return _offs < _size ? _line[_offs] : 0; }

    bool ParseBool(bool& b) {
        ConsumeSpace();

        if (Ch() == 't' || Ch() == 'f') {
            std::string str;
            if (ParseRawString(str)) {
                b = (str == "true");
                return true;
            }
            return false;
        }
        return false;
    }

    bool ParseRawString(std::string& str) {
        enum { LIMIT = 1024*1024 };
        ConsumeSpace();

        for (int i = 0; i < LIMIT; i++) {
            if (isspace(Ch()) || (Ch() == '\n') || (Ch() == '\r') || !Ch()) break;
            
            str.push_back(Ch());
            _offs++;
        }
        return true;
    }

    bool ParseString(std::string& str) {
        enum { LIMIT = 1024*1024 };
        ConsumeSpace();

        if (Ch() != '"') return false;
        _offs++;

        for (int i = 0; i < LIMIT; i++) {
            if (Ch() == '"' || !Ch()) break;
            
            str.push_back(Ch());
            _offs++;
        }
        return true;
    }

    bool ParseVar(std::string& name) {
        if (Ch() != '$') return false;
        _offs++;

        return ParseRawString(name);
    }

    void ConsumeSpace() {
        enum { LIMIT = 1024*1024 };

        // eat whitespace
        for (int i = 0; i < LIMIT; i++) {
            if (!isspace(Ch())) break;
            _offs++;
        }
    }

    std::string _line;
    std::size_t _offs = 0;
    std::size_t _size = 0;
};

typedef std::function<void(std::string, ConfigLineParser&)> ConfigVarHandler;

static void WriteVarName(std::ofstream& fh, const std::string& name)
{
    fh << "$" << name;
}

static void WriteVar(std::ofstream& fh, const std::string& name, const std::string& value)
{
    WriteVarName(fh, name);
    fh << " ";
    fh << "\"" << value << "\"" << "\n";
}

static void WriteVar(std::ofstream& fh, const std::string& name, bool value)
{
    WriteVarName(fh, name);
    fh << " ";
    fh << (value ? "true" : "false") << "\n";
}

static void ParseConfigLine(const std::string& line, ConfigVarHandler var_handler)
{
    ConfigLineParser parser{ line };

    std::string name;
    if (!parser.ParseVar(name)) return;

    var_handler(name, parser);
}

static void ParseConfigFile(const std::string& path, ConfigVarHandler var_handler)
{
    std::ifstream fh{ path };
    std::string line;
    while (std::getline(fh, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        ParseConfigLine(line, var_handler);
    }
}

static std::string GetUserConfigPath()
{
    auto dirname = qc_GetAppDir();
    auto username = qc_GetUserName();
    return PathJoin(dirname, "q1compile_userprefs-"+ username +".cfg");
}

static const char* g_compile_step_names[] = {"QBSP", "LIGHT", "VIS", "CUSTOM"};

static const char* CompileStepName(CompileStepType type)
{
    return g_compile_step_names[int(type)];
}

static CompileStepType ParseCompileStepType(const std::string& tstr)
{
    const size_t count = sizeof(g_compile_step_names) / sizeof(const char*);
    for (size_t i = 0; i < count; i++) {
        if (tstr == g_compile_step_names[i]) {
            return CompileStepType(i);
        }
    }
    return COMPILE_INVALID;
}

static void SetCompileStepVar(std::string name, ConfigLineParser& p, std::vector<CompileStep>& steps)
{
    if (name == "compile_step") {
        std::string typestr;
        if (p.ParseString(typestr)) {
            CompileStepType type = ParseCompileStepType(typestr);
            std::string cmd = "";
            if (type == COMPILE_QBSP) {
                cmd = "qbsp";
            }
            else if (type == COMPILE_LIGHT) {
                cmd = "light";
            }
            else if (type == COMPILE_VIS) {
                cmd = "vis";
            }
            steps.push_back({ type, cmd, "", false });
        }
    }
    else if (name == "compile_step_cmd") {
        p.ParseString(steps.back().cmd);
    }
    else if (name == "compile_step_args") {
        p.ParseString(steps.back().args);
    }
    else if (name == "compile_step_enabled") {
        p.ParseBool(steps.back().enabled);
    }
}

static void SetConfigVar(Config& config, std::string name, ConfigLineParser& p)
{
    bool b = false;
    if (name == "version") {
        p.ParseString(config.version);
    }
    else if (name == "config_name") {
        p.ParseString(config.config_name);
    }
    else if (name == "quake_args") {
        p.ParseString(config.quake_args);
    }
    else if (name == "watch_map_file") {
        p.ParseBool(config.watch_map_file);
    }
    else if (name == "auto_apply_onlyents") {
        p.ParseBool(config.auto_apply_onlyents);
    }
    else if (name == "use_map_mod") {
        p.ParseBool(config.use_map_mod);
    }
    else if (name == "quake_output_enabled") {
        p.ParseBool(config.quake_output_enabled);
    }
    else if (name == "compile_map_on_launch") {
        p.ParseBool(config.compile_map_on_launch);
    }
    else if (name == "open_editor_on_launch") {
        p.ParseBool(config.open_editor_on_launch);
    }
    else if (name == "selected_preset") {
        p.ParseString(config.selected_preset);
    }
    else if (name.find("compile_step") != std::string::npos) {
        SetCompileStepVar(name, p, config.steps);
    }
    else if (config.version.empty()) {
        // old version (< v0.7)
        SetConfigVarOld(config, name, p);
    }
    else {
        int idx = -1;
        for (int i = 0; i < PATH_COUNT; i++) {
            if (!std::strcmp(g_config_path_names[i], name.c_str())) {
                idx = i;
                break;
            }
        }

        if (idx != -1) {
            p.ParseString( config.config_paths[idx] );
        }
    }
}

static void SetUserConfigVar(UserConfig& config, std::string name, ConfigLineParser& p)
{
    if (name == "version") {
        p.ParseString(config.version);
    }
    else if (name == "loaded_config") {
        p.ParseString(config.loaded_config);
    }
    else if (name == "last_import_preset_location") {
        p.ParseString(config.last_import_preset_location);
    }
    else if (name == "last_export_preset_location") {
        p.ParseString(config.last_export_preset_location);
    }
    else if (name == "ui_section_info_open") {
        p.ParseBool(config.ui_section_info_open);
    }
    else if (name == "ui_section_paths_open") {
        p.ParseBool(config.ui_section_paths_open);
    }
    else if (name == "ui_section_tools_open") {
        p.ParseBool(config.ui_section_tools_open);
    }
    else if (name == "ui_section_other_open") {
        p.ParseBool(config.ui_section_other_open);
    }
    else if (name == "recent_config") {
        std::string value;
        if (p.ParseString(value))
            config.recent_configs.push_back(value);
    }
    else if (name == "tool_preset") {
        ToolPreset preset = {};
        if (!p.ParseString(preset.name)) return;
        config.tool_presets.push_back(preset);
    }
    else if (name.find("compile_step") != std::string::npos) {
        auto& preset = config.tool_presets.back();
        SetCompileStepVar(name, p, preset.steps);
    }
    else if (config.version.empty()) {
        // old version (< v0.7)
        SetUserConfigVarOld(config, name, p);
    }
}

static void WriteCompileStep(std::ofstream& fh, const CompileStep& step)
{
    WriteVar(fh, "compile_step", CompileStepName(step.type));
    WriteVar(fh, "compile_step_cmd", step.cmd);
    WriteVar(fh, "compile_step_args", step.args);
    WriteVar(fh, "compile_step_enabled", step.enabled);
}

static void WriteToolPreset(std::ofstream& fh, const ToolPreset& preset)
{
    WriteVar(fh, "tool_preset", preset.name);
    for (const auto& step : preset.steps) {
        WriteCompileStep(fh, step);
    }
#if 0
    WriteVar(fh, "tool_preset_qbsp_args", preset.qbsp_args);
    WriteVar(fh, "tool_preset_light_args", preset.light_args);
    WriteVar(fh, "tool_preset_vis_args", preset.vis_args);
    WriteVar(fh, "tool_preset_qbsp_enabled", (preset.flags & CONFIG_FLAG_QBSP_ENABLED));
    WriteVar(fh, "tool_preset_light_enabled", (preset.flags & CONFIG_FLAG_LIGHT_ENABLED));
    WriteVar(fh, "tool_preset_vis_enabled", (preset.flags & CONFIG_FLAG_VIS_ENABLED));
#endif
}

void WriteConfig(const Config& config, const std::string& path)
{
    std::ofstream fh{ path };

    // write config header
    WriteVar(fh, "version", APP_VERSION);
    WriteVar(fh, "config_name", config.config_name);

    // write config paths
    for (int i = 0; i < PATH_COUNT; i++) {
        WriteVar(fh, g_config_path_names[i], config.config_paths[i]);
    }

    // write config vars
    WriteVar(fh, "quake_args", config.quake_args);
    WriteVar(fh, "watch_map_file", config.watch_map_file);
    WriteVar(fh, "auto_apply_onlyents", config.auto_apply_onlyents);
    WriteVar(fh, "use_map_mod", config.use_map_mod);
    WriteVar(fh, "quake_output_enabled", config.quake_output_enabled);
    WriteVar(fh, "compile_map_on_launch", config.compile_map_on_launch);
    WriteVar(fh, "open_editor_on_launch", config.open_editor_on_launch);
    WriteVar(fh, "selected_preset", config.selected_preset);
    
    // write compile steps
    for (const auto& step : config.steps) {
        WriteCompileStep(fh, step);
    }
}

Config ReadConfig(const std::string& path)
{
    if (!PathExists(path))
        return {};

    Config config = {};

    // set defaults
    config.quake_output_enabled = true;

    ParseConfigFile(path, [&config](std::string name, ConfigLineParser& p) {
        SetConfigVar(config, name, p);
    });
    return config;
}

void WriteUserConfig(const UserConfig& config)
{
    std::string path = GetUserConfigPath();
    std::ofstream fh{ path };

    // write user config vars
    WriteVar(fh, "version", APP_VERSION);
    WriteVar(fh, "loaded_config", config.loaded_config);
    WriteVar(fh, "last_import_preset_location", config.last_import_preset_location);
    WriteVar(fh, "last_export_preset_location", config.last_export_preset_location);
    WriteVar(fh, "ui_section_info_open", config.ui_section_info_open);
    WriteVar(fh, "ui_section_paths_open", config.ui_section_paths_open);
    WriteVar(fh, "ui_section_tools_open", config.ui_section_tools_open);
    WriteVar(fh, "ui_section_other_open", config.ui_section_other_open);

    // write recent configs
    for (const auto& rc : config.recent_configs) {
        WriteVar(fh, "recent_config", rc);
    }

    // write tool presets
    for (const auto& preset : config.tool_presets) {
        if (!preset.builtin) {
            WriteToolPreset(fh, preset);
        }
    }
}

UserConfig ReadUserConfig()
{
    std::string path = GetUserConfigPath();
    std::string oldpath = PathJoin(ConfigurationDir(APP_NAME), "config.cfg");

    if (!PathExists(path))
        path = oldpath;
    
    if (!PathExists(path))
        return {};

    UserConfig config = {};

    // set defaults
    config.ui_section_info_open = true;
    config.ui_section_paths_open = true;
    config.ui_section_tools_open = true;
    config.ui_section_other_open = true;

    ParseConfigFile(path, [&config](std::string name, ConfigLineParser& p) {
        SetUserConfigVar(config, name, p);
    });
    return config;
}

void WriteToolPreset(const ToolPreset& preset, const std::string& path)
{
    std::ofstream fh{ path };

    // when writing a standalone tool preset file, write the version before everything
    WriteVar(fh, "version", APP_VERSION);

    WriteToolPreset(fh, preset);
}

static void SetToolPresetVarOld(const std::string& name, ConfigLineParser& p, ToolPreset& preset)
{
    if (name == "tool_preset_qbsp_args") {
        p.ParseString(preset.steps[0].args);
    }
    else if (name == "tool_preset_light_args") {
        p.ParseString(preset.steps[1].args);
    }
    else if (name == "tool_preset_vis_args") {
        p.ParseString(preset.steps[2].args);
    }
    else if (name == "tool_preset_qbsp_enabled") {
        p.ParseBool(preset.steps[0].enabled);
    }
    else if (name == "tool_preset_light_enabled") {
        p.ParseBool(preset.steps[1].enabled);
    }
    else if (name == "tool_preset_vis_enabled") {
        p.ParseBool(preset.steps[2].enabled);
    }
}

ToolPreset ReadToolPreset(const std::string& path)
{
    if (!PathExists(path))
        return {};

    ToolPreset preset = {};
    ParseConfigFile(path, [&preset](std::string name, ConfigLineParser& p) {
        bool b;
        if (name == "version") {
            p.ParseString(preset.version);
        }
        else if (name == "tool_preset") {
            p.ParseString(preset.name);
        }
        else if (name.find("compile_step") != std::string::npos) {
            SetCompileStepVar(name, p, preset.steps);
        }
        else if (preset.version.empty()) {
            // if it's old version (< v0.7)
            if (preset.steps.empty()) {
                preset.steps.push_back({ COMPILE_QBSP, "qbsp", "", false });
                preset.steps.push_back({ COMPILE_LIGHT, "light", "", false });
                preset.steps.push_back({ COMPILE_VIS, "vis", "", false });
            }
            SetToolPresetVarOld(name, p, preset);
        }
    });
    return preset;
}

void MigrateUserConfig(const UserConfig& cfg)
{
    if (!PathExists(GetUserConfigPath())) {
        // write the user config to the new path already
        WriteUserConfig(cfg);
    }
}