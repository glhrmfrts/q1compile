#pragma once

#include <string>
#include <array>
#include <deque>

#define CONFIG_FLAG_QBSP_ENABLED    0x1
#define CONFIG_FLAG_LIGHT_ENABLED   0x2
#define CONFIG_FLAG_VIS_ENABLED     0x4

enum ConfigPath
{
    PATH_TOOLS_DIR,
    PATH_WORK_DIR,
    PATH_OUTPUT_DIR,
    PATH_ENGINE_EXE,
    PATH_MAP_SOURCE,
    PATH_COUNT
};

/// Config can be saved and loaded from the path the user chooses
struct Config
{
    std::string config_name;
    std::array<std::string, PATH_COUNT> config_paths;

    std::string qbsp_args;
    std::string light_args;
    std::string vis_args;
    std::string quake_args;

    std::string selected_preset;

    bool watch_map_file;
    int tool_flags;

    // Runtime only
    int selected_preset_index;
};

struct ToolPreset
{
    std::string name;
    std::string qbsp_args;
    std::string light_args;
    std::string vis_args;
    int flags;
    bool builtin;
};

/// UserConfig gets saved to the user directory
struct UserConfig
{
    std::string                 loaded_config;
    std::string                 last_import_preset_location;
    std::string                 last_export_preset_location;
    std::deque<std::string>     recent_configs;
    std::vector<ToolPreset>     tool_presets;
};

void WriteConfig(const Config& config, const std::string& path);

Config ReadConfig(const std::string& path);

void WriteUserConfig(const UserConfig& config);

UserConfig ReadUserConfig();

void WriteToolPreset(const ToolPreset& preset, const std::string& path);

ToolPreset ReadToolPreset(const std::string& path);