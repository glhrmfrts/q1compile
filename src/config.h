#pragma once

#include <string>
#include <array>
#include <deque>

//#define CONFIG_FLAG_QBSP_ENABLED    0x1
//#define CONFIG_FLAG_LIGHT_ENABLED   0x2
//#define CONFIG_FLAG_VIS_ENABLED     0x4

namespace config {

enum ConfigPath
{
    PATH_TOOLS_DIR,
    PATH_WORK_DIR,
    PATH_OUTPUT_DIR,
    PATH_EDITOR_EXE,
    PATH_ENGINE_EXE,
    PATH_MAP_SOURCE,
    PATH_COUNT
};

enum CompileStepType
{
    COMPILE_QBSP,
    COMPILE_LIGHT,
    COMPILE_VIS,
    COMPILE_CUSTOM,
    COMPILE_INVALID,
};

struct CompileStep
{
    CompileStepType type;
    std::string cmd;
    std::string args;
    bool enabled = false;
    int flags = 0;
};

/// Config can be saved and loaded from the path the user chooses
struct Config
{
    std::string version;
    std::string config_name;
    std::array<std::string, PATH_COUNT> config_paths;
    std::vector<CompileStep> steps;

    std::string quake_args;
    std::string selected_preset;

    bool watch_map_file;
    bool use_map_mod;
    bool auto_apply_onlyents;
    bool quake_output_enabled;
    bool compile_map_on_launch;
    bool open_editor_on_launch;

    // Runtime only
    int selected_preset_index;
};

struct ToolPreset
{
    std::string version;
    std::string name;
    std::vector<CompileStep> steps;
    int flags = 0;
    bool builtin = false;
};

/// UserConfig gets saved to the user directory
struct UserConfig
{
    std::string                 version;
    std::string                 loaded_config;
    std::string                 last_import_preset_location;
    std::string                 last_export_preset_location;
    std::deque<std::string>     recent_configs;
    std::vector<ToolPreset>     tool_presets;
    bool                        ui_section_info_open;
    bool                        ui_section_paths_open;
    bool                        ui_section_tools_open;
    bool                        ui_section_other_open;
};

static const char* g_config_path_names[] = {
    "tools_dir",
    "work_dir",
    "out_dir",
    "editor_exe",
    "engine_exe",
    "mapsrc_file"
};

void WriteConfig(const Config& config, const std::string& path);

Config ReadConfig(const std::string& path);

void WriteUserConfig(const UserConfig& config);

UserConfig ReadUserConfig();

void WriteToolPreset(const ToolPreset& preset, const std::string& path);

ToolPreset ReadToolPreset(const std::string& path);

void MigrateUserConfig(const UserConfig& cfg);

CompileStep* FindCompileStep(std::vector<CompileStep>& steps, CompileStepType t);

}