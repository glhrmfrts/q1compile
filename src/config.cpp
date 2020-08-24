#include <fstream>
#include <sstream>
#include <functional>
#include <cctype>
#include <string>
#include "config.h"
#include "common.h"
#include "path.h"

static const char* g_config_path_names[] = {
    "tools_dir",
    "work_dir",
    "out_dir",
    "editor_exe",
    "engine_exe",
    "mapsrc_file"
};

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

static void SetConfigVar(Config& config, std::string name, ConfigLineParser& p)
{
    bool b = false;
    if (name == "config_name")
        p.ParseString(config.config_name);
    else if (name == "qbsp_args")
        p.ParseString(config.qbsp_args);
    else if (name == "light_args")
        p.ParseString(config.light_args);
    else if (name == "vis_args")
        p.ParseString(config.vis_args);
    else if (name == "quake_args")
        p.ParseString(config.quake_args);
    else if (name == "qbsp_enabled") {
        p.ParseBool(b);
        if (b)
            config.tool_flags |= CONFIG_FLAG_QBSP_ENABLED;
    }
    else if (name == "light_enabled") {
        p.ParseBool(b);
        if (b)
            config.tool_flags |= CONFIG_FLAG_LIGHT_ENABLED;
    }
    else if (name == "vis_enabled") {
        p.ParseBool(b);
        if (b)
            config.tool_flags |= CONFIG_FLAG_VIS_ENABLED;
    }
    else if (name == "watch_map_file") {
        p.ParseBool(config.watch_map_file);
    }
    else if (name == "use_map_mod") {
        p.ParseBool(config.use_map_mod);
    }
    else if (name == "quake_output_enabled") {
        p.ParseBool(config.quake_output_enabled);
    }
    else if (name == "open_editor_on_launch") {
        p.ParseBool(config.open_editor_on_launch);
    }
    else if (name == "selected_preset") {
        p.ParseString(config.selected_preset);
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
    if (name == "loaded_config") {
        p.ParseString(config.loaded_config);
    }
    else if (name == "last_import_preset_location") {
        p.ParseString(config.last_import_preset_location);
    }
    else if (name == "last_export_preset_location") {
        p.ParseString(config.last_export_preset_location);
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
    else if (name == "tool_preset_qbsp_args") {
        auto& preset = config.tool_presets.back();
        if (!p.ParseString(preset.qbsp_args)) return;
    }
    else if (name == "tool_preset_light_args") {
        auto& preset = config.tool_presets.back();
        if (!p.ParseString(preset.light_args)) return;
    }
    else if (name == "tool_preset_vis_args") {
        auto& preset = config.tool_presets.back();
        if (!p.ParseString(preset.vis_args)) return;
    }
    else if (name == "tool_preset_qbsp_enabled") {
        auto& preset = config.tool_presets.back();

        bool qbsp_enabled;
        if (!p.ParseBool(qbsp_enabled)) return;

        if (qbsp_enabled) preset.flags |= CONFIG_FLAG_QBSP_ENABLED;
    }
    else if (name == "tool_preset_light_enabled") {
        auto& preset = config.tool_presets.back();

        bool light_enabled;
        if (!p.ParseBool(light_enabled)) return;

        if (light_enabled) preset.flags |= CONFIG_FLAG_LIGHT_ENABLED;
    }
    else if (name == "tool_preset_vis_enabled") {
        auto& preset = config.tool_presets.back();

        bool vis_enabled;
        if (!p.ParseBool(vis_enabled)) return;

        if (vis_enabled) preset.flags |= CONFIG_FLAG_VIS_ENABLED;
    }
}

static void WriteToolPreset(std::ofstream& fh, const ToolPreset& preset)
{
    WriteVar(fh, "tool_preset", preset.name);
    WriteVar(fh, "tool_preset_qbsp_args", preset.qbsp_args);
    WriteVar(fh, "tool_preset_light_args", preset.light_args);
    WriteVar(fh, "tool_preset_vis_args", preset.vis_args);
    WriteVar(fh, "tool_preset_qbsp_enabled", (preset.flags & CONFIG_FLAG_QBSP_ENABLED));
    WriteVar(fh, "tool_preset_light_enabled", (preset.flags & CONFIG_FLAG_LIGHT_ENABLED));
    WriteVar(fh, "tool_preset_vis_enabled", (preset.flags & CONFIG_FLAG_VIS_ENABLED));
}

void WriteConfig(const Config& config, const std::string& path)
{
    std::ofstream fh{ path };
    WriteVar(fh, "config_name", config.config_name);

    for (int i = 0; i < PATH_COUNT; i++) {
        WriteVar(fh, g_config_path_names[i], config.config_paths[i]);
    }

    WriteVar(fh, "qbsp_args", config.qbsp_args);
    WriteVar(fh, "light_args", config.light_args);
    WriteVar(fh, "vis_args", config.vis_args);
    WriteVar(fh, "quake_args", config.quake_args);
    WriteVar(fh, "qbsp_enabled", 0 != (config.tool_flags & CONFIG_FLAG_QBSP_ENABLED));
    WriteVar(fh, "light_enabled", 0 != (config.tool_flags & CONFIG_FLAG_LIGHT_ENABLED));
    WriteVar(fh, "vis_enabled", 0 != (config.tool_flags & CONFIG_FLAG_VIS_ENABLED));
    WriteVar(fh, "watch_map_file", config.watch_map_file);
    WriteVar(fh, "use_map_mod", config.use_map_mod);
    WriteVar(fh, "quake_output_enabled", config.quake_output_enabled);
    WriteVar(fh, "open_editor_on_launch", config.open_editor_on_launch);
    WriteVar(fh, "selected_preset", config.selected_preset);
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

    WriteVar(fh, "loaded_config", config.loaded_config);
    WriteVar(fh, "last_import_preset_location", config.last_import_preset_location);
    WriteVar(fh, "last_export_preset_location", config.last_export_preset_location);

    for (const auto& rc : config.recent_configs) {
        WriteVar(fh, "recent_config", rc);
    }
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
    ParseConfigFile(path, [&config](std::string name, ConfigLineParser& p) {
        SetUserConfigVar(config, name, p);
    });
    return config;
}

void WriteToolPreset(const ToolPreset& preset, const std::string& path)
{
    std::ofstream fh{ path };
    WriteToolPreset(fh, preset);
}

ToolPreset ReadToolPreset(const std::string& path)
{
    if (!PathExists(path))
        return {};

    ToolPreset preset = {};
    ParseConfigFile(path, [&preset](std::string name, ConfigLineParser& p) {
        bool b;
        if (name == "tool_preset") {
            p.ParseString(preset.name);
        }
        else if (name == "tool_preset_qbsp_args") {
            p.ParseString(preset.qbsp_args);
        }
        else if (name == "tool_preset_light_args") {
            p.ParseString(preset.light_args);
        }
        else if (name == "tool_preset_vis_args") {
            p.ParseString(preset.vis_args);
        }
        else if (name == "tool_preset_qbsp_enabled") {
            if (p.ParseBool(b)) {
                preset.flags |= CONFIG_FLAG_QBSP_ENABLED;
            }
        }
        else if (name == "tool_preset_light_enabled") {
            if (p.ParseBool(b)) {
                preset.flags |= CONFIG_FLAG_LIGHT_ENABLED;
            }
        }
        else if (name == "tool_preset_vis_enabled") {
            if (p.ParseBool(b)) {
                preset.flags |= CONFIG_FLAG_VIS_ENABLED;
            }
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