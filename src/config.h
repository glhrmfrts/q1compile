#pragma once

#include <string>
#include <array>
#include <deque>
#include <set>
#include "keybind.h"

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
    std::string ui_last_custom_cmd;
};

struct LayerSelection
{
    bool auto_select_new_layers;
    bool default_layer_selected;
    std::string name;
    std::vector<std::string> layers;
};

/// Config can be saved and loaded from the path the user chooses
struct Config
{
    std::string version;
    std::string config_name;
    std::array<std::string, PATH_COUNT> config_paths;
    std::vector<CompileStep> steps;
    std::vector<LayerSelection> layer_selections;
    std::vector<std::string> custom_worldspawn_light_fields;
    std::vector<std::string> custom_brush_light_fields;
    std::vector<std::string> custom_light_entities;
    std::vector<std::string> ignore_field_diff;
    std::string quake_args;
    std::string selected_preset;
    std::string selected_layers;

    bool watch_map_file;
    bool use_map_mod;
    bool auto_apply_onlyents;
    bool quake_output_enabled;
    bool compile_map_on_launch;
    bool open_editor_on_launch;
    bool autosave;

    bool ui_section_info_open;
    bool ui_section_paths_open;
    bool ui_section_tools_open;
    bool ui_section_engine_open;
    bool ui_section_other_open;

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

/// UserConfig gets saved to the application directory
struct UserConfig
{
    std::string                   version;
    std::set<std::string>         loaded_configs;
    std::string                   last_import_preset_location;
    std::string                   last_export_preset_location;
    std::string                   last_tools_dir;
    std::string                   last_engine_exe;
    std::deque<std::string>       recent_configs;
    std::vector<ToolPreset>       tool_presets;
    std::string                   selected_config;
    std::unique_ptr<keybind::KeyBindState> keybinds;
};

static const char* g_config_path_names[] = {
    "tools_dir",
    "work_dir",
    "out_dir",
    "editor_exe",
    "engine_exe",
    "mapsrc_file"
};

const char* CompileStepName(CompileStepType t);

void WriteConfig(const Config& config, const std::string& path);

Config ReadConfig(const std::string& path);

void WriteUserConfig(const UserConfig& config);

UserConfig ReadUserConfig();

void WriteToolPreset(const ToolPreset& preset, const std::string& path);

ToolPreset ReadToolPreset(const std::string& path);

void MigrateUserConfig(const UserConfig& cfg);

CompileStep* FindCompileStep(std::vector<CompileStep>& steps, CompileStepType t);

std::vector<CompileStep> GetDefaultCompileSteps();

void SetConfigDefaults(Config& config);

}