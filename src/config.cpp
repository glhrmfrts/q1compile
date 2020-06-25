#include <fstream>
#include <sstream>
#include <functional>
#include <cctype>
#include <string>
#include "config.h"
#include "common.h"
#include "path.h"

static const char* config_path_names[] = {
    "tools_dir",
    "work_dir",
    "out_dir",
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
            config.flags |= CONFIG_FLAG_QBSP_ENABLED;
    }
    else if (name == "light_enabled") {
        p.ParseBool(b);
        if (b)
            config.flags |= CONFIG_FLAG_LIGHT_ENABLED;
    }
    else if (name == "vis_enabled") {
        p.ParseBool(b);
        if (b)
            config.flags |= CONFIG_FLAG_VIS_ENABLED;
    }
    else if (name == "watch_map_file") {
        p.ParseBool(b);
        if (b)
            config.flags |= CONFIG_FLAG_WATCH_MAP_FILE;
    }
    else {
        int idx = -1;
        for (int i = 0; i < PATH_COUNT; i++) {
            if (!std::strcmp(config_path_names[i], name.c_str())) {
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
    if (name == "loaded_config")
        p.ParseString(config.loaded_config);
    else if (name == "recent_config") {
        std::string value;
        if (p.ParseString(value))
            config.recent_configs.push_back(value);
    }
}

void WriteConfig(const Config& config, const std::string& path)
{
    std::ofstream fh{ path };
    WriteVar(fh, "config_name", config.config_name);

    for (int i = 0; i < PATH_COUNT; i++) {
        WriteVar(fh, config_path_names[i], config.config_paths[i]);
    }

    WriteVar(fh, "qbsp_args", config.qbsp_args);
    WriteVar(fh, "light_args", config.light_args);
    WriteVar(fh, "vis_args", config.vis_args);
    WriteVar(fh, "quake_args", config.quake_args);
    WriteVar(fh, "qbsp_enabled", 0 != (config.flags & CONFIG_FLAG_QBSP_ENABLED));
    WriteVar(fh, "light_enabled", 0 != (config.flags & CONFIG_FLAG_LIGHT_ENABLED));
    WriteVar(fh, "vis_enabled", 0 != (config.flags & CONFIG_FLAG_VIS_ENABLED));
    WriteVar(fh, "watch_map_file", 0 != (config.flags & CONFIG_FLAG_WATCH_MAP_FILE));
}

Config ReadConfig(const std::string& path)
{
    if (!PathExists(path))
        return {};

    Config config = {};
    ParseConfigFile(path, [&config](std::string name, ConfigLineParser& p) {
        SetConfigVar(config, name, p);
    });
    return config;
}

void WriteUserConfig(const UserConfig& config)
{
    std::string path = PathJoin(ConfigurationDir(APP_NAME), "config.cfg");
    std::ofstream fh{ path };

    WriteVar(fh, "loaded_config", config.loaded_config);
    for (const auto& rc : config.recent_configs) {
        WriteVar(fh, "recent_config", rc);
    }
}

UserConfig ReadUserConfig()
{
    std::string path = PathJoin(ConfigurationDir(APP_NAME), "config.cfg");
    if (!PathExists(path))
        return {};

    UserConfig config = {};
    ParseConfigFile(path, [&config](std::string name, ConfigLineParser& p) {
        SetUserConfigVar(config, name, p);
    });
    return config;
}