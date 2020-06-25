#pragma once

#include <string>
#include <array>
#include <deque>

#define CONFIG_FLAG_QBSP_ENABLED    0x1
#define CONFIG_FLAG_LIGHT_ENABLED   0x2
#define CONFIG_FLAG_VIS_ENABLED     0x4
#define CONFIG_FLAG_WATCH_MAP_FILE  0x10

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

    int flags;
};

/// UserConfig gets saved to the user directory
struct UserConfig
{
    std::string                 loaded_config;
    std::deque<std::string>     recent_configs;
};

void        WriteConfig(const Config& config, const std::string& path);

Config      ReadConfig(const std::string& path);

void        WriteUserConfig(const UserConfig& config);

UserConfig  ReadUserConfig();

