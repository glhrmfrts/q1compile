#include <chrono>
#include <memory>
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <imfilebrowser.h>
#include "common.h"
#include "console.h"
#include "compile.h"
#include "config.h"
#include "file_watcher.h"
#include "map_file.h"
#include "mutex_char_buffer.h"
#include "path.h"
#include "sub_process.h"
#include "shell_command.h"
#include "work_queue.h"
#include "q1compile.h"
#include "../include/imgui_markdown.h" // https://github.com/juliettef/imgui_markdown/
#include "fonts/icofont.h"

static const ImVec4 INPUT_TEXT_READ_ONLY_COLOR{ 0.15f, 0.15f, 0.195f, 1.0f };

static AppState g_app;

static std::vector<config::ToolPreset> g_builtin_presets = {
    config::ToolPreset{
        "No Vis (built-in)",
        "",
        {
            config::CompileStep{
                config::COMPILE_QBSP,
                "qbsp",
                "",
                true,
                0
            },
            config::CompileStep{
                config::COMPILE_LIGHT,
                "light",
                "",
                true,
                0
            }
        },
        0,
        true
    },
    config::ToolPreset{
        "No Vis, only ents (built-in)",
        "",
        {
            config::CompileStep{
                config::COMPILE_QBSP,
                "qbsp",
                "-onlyents",
                true,
                0
            },
            config::CompileStep{
                config::COMPILE_LIGHT,
                "light",
                "-onlyents",
                true,
                0
            }
        },
        0,
        true
    },
    config::ToolPreset{
        "Fast Vis (built-in)",
        "",
        {
            config::CompileStep{
                config::COMPILE_QBSP,
                "qbsp",
                "",
                true,
                0
            },
            config::CompileStep{
                config::COMPILE_LIGHT,
                "light",
                "",
                true,
                0
            },
            config::CompileStep{
                config::COMPILE_VIS,
                "vis",
                "-fast",
                true,
                0
            }
        },
        0,
        true
    },
    config::ToolPreset{
        "Full (built-in)",
        "",
        {
            config::CompileStep{
                config::COMPILE_QBSP,
                "qbsp",
                "",
                true,
                0
            },
            config::CompileStep{
                config::COMPILE_LIGHT,
                "light",
                "-extra4 -soft",
                true,
                0
            },
            config::CompileStep{
                config::COMPILE_VIS,
                "vis",
                "-level 4",
                true,
                0
            }
        },
        0,
        true
    }
};

static std::vector<std::string> g_config_paths_title = {
    "Select Tools Dir",
    "Select Work Dir",
    "Select Output Dir",
    "Select Editor Exe",
    "Select Engine Exe",
    "Select Map Source"
};

static std::vector<const char*> g_config_paths_filter = {
    NULL,
    NULL,
    NULL,
    "Executable Files (*.exe)\0*.exe\0",
    "Executable Files (*.exe)\0*.exe\0",
    "Map Source Files (*.map)\0*.map\0"
};

static std::vector<const char*> g_config_paths_help = {
    "Where the compiler tools are (qbsp.exe, light.exe, vis.exe)",
    "Temporary dir to work with files, it MUST NOT be the same as the map source directory",
    "Where the compiled .bsp and .lit files will be",
    "The map editor executable (optional)",
    "The Quake engine executable",
    "The .map file to compile"
};

template<class T>
static void ReadToMutexCharBuffer(T& obj, std::atomic_bool* stop, mutex_char_buffer::MutexCharBuffer* out)
{
    char c;
    while (obj.ReadChar(c)) {
        if (out) {
            out->push(c);
        }
        if (stop && *stop) {
            return;
        }
    }
}

static void ExecuteCompileProcess(const std::string& cmd, const std::string& pwd, bool suppress_output)
{
    sub_process::SubProcess proc{ cmd, pwd };
    if (!proc.Good()) {
        console::PrintError(cmd.c_str());
        console::PrintError(": failed to open subprocess\n");
        return;
    }
    
    auto output = &g_app.compile_output;
    if (suppress_output) {
        output = nullptr;
    }

    console::SetPrintToFile(false);
    ReadToMutexCharBuffer(proc, &g_app.stop_compiling, output);
    console::SetPrintToFile(true);
}

static void ExecuteShellCommand(const std::string& cmd, const std::string& pwd)
{
    shell_command::ShellCommand proc{ cmd, pwd };
    ReadToMutexCharBuffer(proc, nullptr, &g_app.compile_output);
}

static void AddExtensionIfNone(std::string& path, const std::string& extension)
{
    std::string root, ext;
    path::SplitExtension(path, root, ext);
    if (ext.empty()) {
        path = root + extension;
    }
}

static bool IsExtension(const std::string& path, const std::string& extension)
{
    std::string root, ext;
    path::SplitExtension(path, root, ext);
    return ext == extension;
}

static void ReportConfigChange(const std::string& name, const std::string& value)
{
    console::Print("Set '"); console::Print(name.c_str()); console::Print("' to "); console::Print(value.c_str()); console::Print("\n");
}

static void HandleMapSourceChanged()
{
    const std::string& path = g_app.current_config.config_paths[config::PATH_MAP_SOURCE];
    g_app.map_file_watcher->SetPath(path);
    g_app.map_file = std::make_unique<map_file::MapFile>(path);
    if (!g_app.map_file->Good()) {
        g_app.map_file = nullptr;
    }
}

static void SetConfigPath(config::ConfigPath path, const std::string& value)
{
    g_app.modified = true;
    g_app.current_config.config_paths[path] = value;
    //ReportConfigChange(g_config_path_names[path], value);
    if (path == config::PATH_MAP_SOURCE) {
        HandleMapSourceChanged();
    }
}

static void AddRecentConfig(const std::string& path)
{
    if (path.empty()) return;

    for (size_t i = 0; i < g_app.user_config.recent_configs.size(); i++) {
        if (path == g_app.user_config.recent_configs[i]) return;
    }

    if (g_app.user_config.recent_configs.size() >= CONFIG_RECENT_COUNT) {
        g_app.user_config.recent_configs.pop_front();
    }
    g_app.user_config.recent_configs.push_back(path);
}

static int FindPresetIndex(const std::string& name)
{
    for (std::size_t i = 0; i < g_app.user_config.tool_presets.size(); i++) {
        if (g_app.user_config.tool_presets[i].name == name) {
            return (int)i;
        }
    }
    return -1;
}

static bool ComparePresetToConfig(const config::ToolPreset& preset)
{
    if (preset.steps.size() != g_app.current_config.steps.size())
        return true;

    for (size_t i = 0; i < preset.steps.size(); i++) {
        const auto& pre_step = preset.steps[i];
        const auto& cfg_step = g_app.current_config.steps[i];
        if (pre_step.type != cfg_step.type)
            return true;
        if (pre_step.cmd != cfg_step.cmd)
            return true;
        if (pre_step.args != cfg_step.args)
            return true;
        if (pre_step.enabled != cfg_step.enabled)
            return true;
        if (pre_step.flags != cfg_step.flags)
            return true;
    }

    return false;
}

static void SaveConfig(const std::string& path)
{
    int pi = g_app.current_config.selected_preset_index - 1;
    if (pi >= 0) {
        g_app.current_config.selected_preset = g_app.user_config.tool_presets[pi].name;
    }
    else {
        g_app.current_config.selected_preset = "";
    }

    config::WriteConfig(g_app.current_config, path);
    g_app.user_config.loaded_config = path;
    g_app.last_loaded_config_name = g_app.current_config.config_name;
    config::WriteUserConfig(g_app.user_config);

    g_app.modified = false;
    g_app.console_auto_scroll = true;
    console::Print("Saved config: ");
    console::Print(path.c_str());
    console::Print("\n");
}

static bool LoadConfig(const std::string& path)
{
    if (!path::Exists(path)) {
        g_app.console_auto_scroll = true;
        console::PrintError("File not found: ");
        console::PrintError(path.c_str());
        console::PrintError("\n");
        return false;
    }

    if (!IsExtension(path, ".cfg")) {
        g_app.console_auto_scroll = true;
        console::PrintError("Invalid config file: ");
        console::PrintError(path.c_str());
        console::PrintError("\n");
        return false;
    }

    g_app.map_has_leak = false;
    g_app.current_config = config::ReadConfig(path);
    g_app.current_config.selected_preset_index = FindPresetIndex(g_app.current_config.selected_preset) + 1;
    if (g_app.current_config.selected_preset_index > 0) {
        if (ComparePresetToConfig(g_app.user_config.tool_presets[g_app.current_config.selected_preset_index - 1])) {
            g_app.modified_flags = true;
        }
    }

    g_app.user_config.loaded_config = path;
    g_app.last_loaded_config_name = g_app.current_config.config_name;
    config::WriteUserConfig(g_app.user_config);

    g_app.map_file_watcher->SetEnabled(g_app.current_config.watch_map_file);
    HandleMapSourceChanged();

    g_app.modified = false;
    g_app.console_auto_scroll = true;
    console::Print("Loaded config: ");
    console::Print(path.c_str());
    console::Print("\n");
    return true;
}

static void CopyPreset(const config::ToolPreset& preset)
{
    g_app.modified = g_app.modified || ComparePresetToConfig(preset);
    g_app.current_config.steps = preset.steps;
    
    /*
    ReportConfigChange("QBSP", (preset.flags & CONFIG_FLAG_QBSP_ENABLED) ? "enabled" : "disabled");
    ReportConfigChange("LIGHT", (preset.flags & CONFIG_FLAG_LIGHT_ENABLED)  ? "enabled" : "disabled");
    ReportConfigChange("VIS", (preset.flags & CONFIG_FLAG_VIS_ENABLED)  ? "enabled" : "disabled");
    ReportConfigChange("QBSP args", (preset.qbsp_args));
    ReportConfigChange("LIGHT args", (preset.light_args));
    ReportConfigChange("VIS args", (preset.vis_args));
    */
}

static config::ToolPreset& GetPreset(int i)
{
    return g_app.user_config.tool_presets[i - 1];
}

static void AddBuiltinPresets()
{
    for (const auto& preset : g_builtin_presets) {
        if (FindPresetIndex(preset.name) == -1) {
            g_app.user_config.tool_presets.insert(g_app.user_config.tool_presets.begin(), preset);
        }
    }
}

static bool ShouldConfigPathBeDirectory(config::ConfigPath path)
{
    return (path == config::PATH_TOOLS_DIR) || (path == config::PATH_WORK_DIR) || (path == config::PATH_OUTPUT_DIR);
}

static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{

    if (uMsg == BFFM_INITIALIZED)
    {
        std::string tmp = (const char*)lpData;
        //std::cout << "path: " << tmp << std::endl;
        SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
    }

    return 0;
}

bool SelectDirDialog(std::string& out)
{
    TCHAR path[MAX_PATH];

    out = path::ToNative(out);

    const char* path_param = out.c_str();

    BROWSEINFO bi = { 0 };
    bi.lpszTitle = ("Browse for folder...");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallbackProc;
    bi.lParam = (LPARAM)path_param;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

    if (pidl != 0)
    {
        //get the name of the folder and put it in path
        SHGetPathFromIDList(pidl, path);

        //free memory used
        IMalloc* imalloc = 0;
        if (SUCCEEDED(SHGetMalloc(&imalloc)))
        {
            imalloc->Free(pidl);
            imalloc->Release();
        }

        out = path::FromNative(path);
        return true;
    }

    return false;
}

bool SelectFileDialog(const char* filter, FileBrowserFlags flags, std::string& out)
{
    char buf[256];

    out = path::ToNative(out);

    // Set suggested path to save the new file
    std::memcpy(buf, out.c_str(), out.size() + 1);

    OPENFILENAME s;
    ZeroMemory(&s, sizeof(OPENFILENAME));
    s.lStructSize = sizeof(OPENFILENAME);
    s.hwndOwner = (HWND)g_app.platform_data;
    s.lpstrFilter = filter;
    s.lpstrFile = buf;
    s.nMaxFile = sizeof(buf);

    if (flags & FB_FLAG_MUST_EXIST) {
       s.Flags = OFN_FILEMUSTEXIST;
       s.lpstrInitialDir = out.c_str();
       ZeroMemory(buf, sizeof(buf));
    }

    bool ok = false;
    if (flags & FB_FLAG_LOAD) {
        ok = GetOpenFileName(&s);
    }
    else if (flags & FB_FLAG_SAVE) {
        ok = GetSaveFileName(&s);
    }

    if (ok) {
        std::string user_path = std::string{ s.lpstrFile, strlen(s.lpstrFile) };
        out = path::FromNative(user_path);
        return true;
    }

    DWORD err = CommDlgExtendedError();
    if (err) {
        console::PrintError("CommDlg Error: ");
        console::PrintValueError(err);
        console::PrintError("\n");
    }
    return false;
}

void ShowUnsavedChangesWindow(UnsavedChangesCallback cb)
{
    g_app.show_unsaved_changes_window = true;
    g_app.unsaved_changes_callback = cb;
}

static std::string GetDefaultWorkDir()
{
    std::string work_dir = path::Join(path::qc_GetTempDir(), "q1compile");
    if (!path::Exists(work_dir)) {
        if (!path::Create(work_dir)) {
            console::PrintError("Could not create work dir!\n");
            work_dir = "";
        }
    }
    return work_dir;
}

/*
===================
UI Event Handlers
===================
*/

static void HandleNewConfig()
{
    if (g_app.modified) {
        ShowUnsavedChangesWindow([](bool saved) {
            g_app.modified = false;
            HandleNewConfig();
        });
    }
    else {
        g_app.map_file = nullptr;
        g_app.map_has_leak = false;
        g_app.current_config = {};
        g_app.current_config.config_paths[config::PATH_WORK_DIR] = GetDefaultWorkDir();
        g_app.user_config.loaded_config = "";
        WriteUserConfig(g_app.user_config);
    }
}

static void HandleLoadConfig()
{
    if (g_app.modified) {
        ShowUnsavedChangesWindow([](bool saved) {
            g_app.modified = false;
            HandleLoadConfig();
        });
    }
    else {
        std::string pwd = path::Directory(g_app.user_config.loaded_config);
        std::string user_path = pwd;
        FileBrowserFlags flags = FB_FLAG_LOAD | FB_FLAG_MUST_EXIST;

        if (SelectFileDialog("Config File (*.cfg)\0*.cfg\0", flags, user_path)) {
            g_app.fb_callback = FB_LOAD_CONFIG;
            g_app.fb_path = user_path;
            HandleFileBrowserCallback();
        }
    }
}

static void HandleLoadRecentConfig(int i)
{
    if (g_app.modified) {
        ShowUnsavedChangesWindow([i](bool saved) {
            g_app.modified = false;
            HandleLoadRecentConfig(i);
        });
    }
    else {
        const std::string& path = g_app.user_config.recent_configs[i];
        if (!LoadConfig(path)) {
            g_app.user_config.recent_configs.erase(g_app.user_config.recent_configs.begin() + i);
        }
    }
}

static void HandleSaveConfigAs()
{
    std::string pwd = path::Directory(g_app.user_config.loaded_config);
    std::string filename = g_app.current_config.config_name + ".cfg";
    std::string path = path::Join(pwd, filename);
    std::string user_path = path;
    FileBrowserFlag flags = FB_FLAG_SAVE;

    if (SelectFileDialog("Config File (*.cfg)\0*.cfg\0", flags, user_path)) {
        g_app.fb_callback = FB_SAVE_CONFIG;
        g_app.fb_path = user_path;
        HandleFileBrowserCallback();
    }
}

static void HandleSaveConfig()
{
    if (g_app.user_config.loaded_config.empty() || g_app.current_config.config_name != g_app.last_loaded_config_name) {
        HandleSaveConfigAs();
        return;
    }

    SaveConfig(g_app.user_config.loaded_config);
}

static void HandleCompileAndRun()
{
    // run quake and ignore diff
    compile::StartCompileJob(g_app.current_config, true, true);
}

static void HandleCompileOnly()
{
    // don't run quake and ignore diff
    compile::StartCompileJob(g_app.current_config, false, true);
}

static void HandleAutoCompile()
{
    // don't run quake and don't ignore diff
    compile::StartCompileJob(g_app.current_config, false, false);
}

static void HandleStopCompiling()
{
    if (g_app.compiling) {
        g_app.stop_compiling = true;
    }
}

static void HandleRun()
{
    config::Config job_config = g_app.current_config;
    job_config.steps = {};

    if (!g_app.last_job_ran_quake) {
        // Instead of stopping the current compilation, just add the job to the queue
        compile::EnqueueCompileJob(job_config, true, true);
    }
    else {
        compile::StartCompileJob(job_config, true, true);
    }
}

static void HandleToolHelp(config::CompileStepType cstype)
{
    g_app.console_auto_scroll = false;
    g_app.console_lock_scroll = false;
    console::ClearConsole();
    compile::StartHelpJob(cstype);
}

static void HandlePathSelect(config::ConfigPath path)
{
    bool should_be_dir = ShouldConfigPathBeDirectory(path);
    std::string pwd = path::FromNative(g_app.current_config.config_paths[path]);
    std::string user_path = pwd;
    FileBrowserFlags flags = FB_FLAG_LOAD | FB_FLAG_MUST_EXIST;

    bool ok = false;
    if (should_be_dir) {
        ok = SelectDirDialog(user_path);
    }
    else {
        ok = SelectFileDialog(g_config_paths_filter[path], flags, user_path);
    }

    if (ok) {
        g_app.fb_callback = (FileBrowserCallback)(FB_CONFIG_STR + path);
        g_app.fb_path = user_path;
        HandleFileBrowserCallback();
    }
}

static void HandlePathOpen(config::ConfigPath path)
{
    std::string arg = path::FromNative(g_app.current_config.config_paths[path]);
    if (!ShouldConfigPathBeDirectory(path)) {
        arg = path::Directory(arg);
    }
    if (!sub_process::StartDetachedProcess("explorer " + path::ToNative(arg), "")) {
        console::PrintError("Error opening path: ");
        console::PrintError(g_app.current_config.config_paths[path].c_str());
        console::PrintError("\n");
    }
}

static void HandleFileBrowserCallback()
{
    switch (g_app.fb_callback) {
    case FB_LOAD_CONFIG: {
        std::string path = path::FromNative(g_app.fb_path);
        if (!IsExtension(path, ".cfg")) {
            g_app.console_auto_scroll = true;
            console::PrintError("Invalid config file: ");
            console::PrintError(path.c_str());
            console::PrintError("\n");
            return;
        }

        if (path::Exists(path)) {
            if (LoadConfig(path)) {
                AddRecentConfig(path);
            }
        }
    } break;

    case FB_SAVE_CONFIG: {
        std::string path = path::FromNative(g_app.fb_path);
        AddExtensionIfNone(path, ".cfg");
        SaveConfig(path);
    } break;

    case FB_IMPORT_PRESET: {
        std::string path = path::FromNative(g_app.fb_path);
        if (!IsExtension(path, ".pre")) {
            g_app.console_auto_scroll = true;
            console::PrintError("Invalid preset file: ");
            console::PrintError(path.c_str());
            console::PrintError("\n");
            return;
        }

        if (path::Exists(path)) {
            config::ToolPreset preset = config::ReadToolPreset(path);
            if (preset.name.empty()) {
                g_app.console_auto_scroll = true;
                console::PrintError("Invalid preset file: ");
                console::PrintError(path.c_str());
                console::PrintError("\n");
                return;
            }

            while (common::StrReplace(preset.name, "(built-in)", "")) {}

            g_app.user_config.last_import_preset_location = path::Directory(path);
            g_app.user_config.tool_presets.push_back(preset);
            config::WriteUserConfig(g_app.user_config);

            g_app.show_preset_window = true;

            g_app.console_auto_scroll = true;
            console::Print("Preset imported: ");
            console::Print(path.c_str());
            console::Print("\n");
        }
    } break;

    case FB_EXPORT_PRESET: {
        std::string path = path::FromNative(g_app.fb_path);
        AddExtensionIfNone(path, ".pre");

        auto exp_preset = g_app.user_config.tool_presets[g_app.preset_to_export];
        while (common::StrReplace(exp_preset.name, "(built-in)", "")) {}
        config::WriteToolPreset(exp_preset, path);
        
        g_app.user_config.last_export_preset_location = path::Directory(path);
        config::WriteUserConfig(g_app.user_config);

        g_app.show_preset_window = true;

        g_app.console_auto_scroll = true;
        console::Print("Preset exported: ");
        console::Print(path.c_str());
        console::Print("\n");
    } break;

    default:
        if (g_app.fb_callback >= FB_CONFIG_STR)
            SetConfigPath(
                (config::ConfigPath)(g_app.fb_callback - FB_CONFIG_STR),
                path::FromNative(g_app.fb_path)
            );
        break;
    }
}

static void HandleFileBrowserCancel()
{
    switch (g_app.fb_callback) {
    case FB_IMPORT_PRESET:
    case FB_EXPORT_PRESET:
        g_app.show_preset_window = true;
        break;
    }
}

static void HandleSelectPreset(int i)
{
    if (i < 1) {
        console::PrintError("invalid preset: ");
        console::PrintValueError(i);
        console::PrintError("\n");
        return;
    }

    g_app.current_config.selected_preset_index = i;
    CopyPreset(g_app.user_config.tool_presets[i - 1]);

    g_app.modified_flags = false;
}

static void HandleImportPreset()
{
    std::string pwd = g_app.user_config.last_import_preset_location;
    std::string user_path = pwd;
    FileBrowserFlags flags = FB_FLAG_LOAD | FB_FLAG_MUST_EXIST;

    if (SelectFileDialog("Preset File (*.pre)\0*.pre\0", flags, user_path)) {
        g_app.fb_callback = FB_IMPORT_PRESET;
        g_app.fb_path = user_path;
        HandleFileBrowserCallback();
    }
}

static void HandleExportPreset(int real_index)
{
    std::string pwd = g_app.user_config.last_export_preset_location;
    std::string filename = g_app.user_config.tool_presets[real_index].name + ".pre";
    std::string path = path::Join(pwd, filename);
    std::string user_path = path;
    FileBrowserFlags flags = FB_FLAG_SAVE;

    if (SelectFileDialog("Preset File (*.pre)\0*.pre\0", flags, user_path)) {
        g_app.preset_to_export = real_index;
        g_app.fb_callback = FB_EXPORT_PRESET;
        g_app.fb_path = user_path;
        HandleFileBrowserCallback();
    }
}

static void HandleClearWorkingFiles()
{
    std::string source_map = path::FromNative(g_app.current_config.config_paths[config::PATH_MAP_SOURCE]);
    std::string work_map = path::Join(path::FromNative(g_app.current_config.config_paths[config::PATH_WORK_DIR]), path::Filename(source_map));
    std::string work_bsp = work_map;
    std::string work_lit = work_map;
    common::StrReplace(work_bsp, ".map", ".bsp");
    common::StrReplace(work_lit, ".map", ".lit");

    g_app.console_auto_scroll = true;
    if (path::Remove(work_bsp)) {
        console::Print("Removed ");
        console::Print(work_bsp.c_str());
        console::Print("\n");
    }
    if (path::Remove(work_lit)) {
        console::Print("Removed ");
        console::Print(work_lit.c_str());
        console::Print("\n");
    }
}

static void HandleClearOutputFiles()
{
    std::string source_map = path::FromNative(g_app.current_config.config_paths[config::PATH_MAP_SOURCE]);
    std::string out_map = path::Join(path::FromNative(g_app.current_config.config_paths[config::PATH_OUTPUT_DIR]), path::Filename(source_map));
    std::string out_bsp = out_map;
    std::string out_lit = out_map;
    common::StrReplace(out_bsp, ".map", ".bsp");
    common::StrReplace(out_lit, ".map", ".lit");

    g_app.console_auto_scroll = true;
    if (path::Remove(out_bsp)) {
        console::Print("Removed ");
        console::Print(out_bsp.c_str());
        console::Print("\n");
    }
    if (path::Remove(out_lit)) {
        console::Print("Removed ");
        console::Print(out_lit.c_str());
        console::Print("\n");
    }
}

static void HandleResetWorkDir()
{
    std::string value = GetDefaultWorkDir();
    if (!value.empty()) {
        g_app.current_config.config_paths[config::PATH_WORK_DIR] = value;
        ReportConfigChange("Work Dir:", value);
    }
}

static std::string GetReadmeText()
{
    static std::string readme;

    if (readme.empty()) {
        if (!path::ReadFileText("q1compile_readme.txt", readme)) return "";
    }

    return readme;
}

static void LaunchEditorProcess(const std::string& cmd)
{
    if (!sub_process::StartDetachedProcess(cmd, "")) {
        console::PrintError("Error launching editor: ");
        console::PrintError(g_app.current_config.config_paths[config::PATH_EDITOR_EXE].c_str());
        console::PrintError("\n");
    }
}

static void HandleOpenMapInEditor()
{
    auto cmd = g_app.current_config.config_paths[config::PATH_EDITOR_EXE];
    cmd.append(" ");
    cmd.append(g_app.current_config.config_paths[config::PATH_MAP_SOURCE]);
    LaunchEditorProcess(cmd);
}

static void HandleOpenEditor()
{
    auto cmd = g_app.current_config.config_paths[config::PATH_EDITOR_EXE];
    LaunchEditorProcess(cmd);
}

/*
====================
UI Drawing
====================
*/

// Helper to display a little (?) mark which shows a tooltip when hovered.
// In your own code you may want to display an actual icon if you are using a merged icon fonts (see docs/FONTS.md)
static void DrawHelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void DrawRecentConfigs()
{
    int size = g_app.user_config.recent_configs.size();
    for (int i = size - 1; i >= 0; i--) {
        const std::string& path = g_app.user_config.recent_configs[i];
        if (path == g_app.user_config.loaded_config) continue;

        std::string filename = path::Filename(path);
        if (ImGui::MenuItem(filename.c_str(), "", nullptr)) {
            HandleLoadRecentConfig(i);
        }
    }
}

static void DrawMenuBar()
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Config", "Ctrl+N", nullptr)) {
                HandleNewConfig();
            }

            if (ImGui::MenuItem("Load Config", "Ctrl+O", nullptr)) {
                HandleLoadConfig();
            }

            if (ImGui::BeginMenu("Recent Configs")) {
                DrawRecentConfigs();
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Save Config", "Ctrl+S", nullptr)) {
                HandleSaveConfig();
            }

            if (ImGui::MenuItem("Save Config As...", "Ctrl+Shift+S", nullptr)) {
                HandleSaveConfigAs();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Quit", "Ctrl+Q", nullptr)) {
                g_app.app_running = false;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Open map in editor", "Ctrl+E", nullptr)) {
                HandleOpenMapInEditor();
            }
            if (ImGui::MenuItem("Open editor", "Ctrl+Shift+E", nullptr)) {
                HandleOpenEditor();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Compile")) {
            if (ImGui::MenuItem("Compile and run", "Ctrl+C", nullptr)) {
                HandleCompileAndRun();
            }

            if (ImGui::MenuItem("Compile only", "Ctrl+Shift+C", nullptr)) {
                HandleCompileOnly();
            }

            if (ImGui::MenuItem("Stop", "Ctrl+B", nullptr)) {
                HandleStopCompiling();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Run", "Ctrl+R", nullptr)) {
                HandleRun();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Clear working files", "", nullptr)) {
                HandleClearWorkingFiles();
            }

            if (ImGui::MenuItem("Clear output files", "", nullptr)) {
                HandleClearOutputFiles();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Reset Work Dir", "Ctrl+Shift+W", nullptr)) {
                HandleResetWorkDir();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Manage presets...", "Ctrl+P", nullptr)) {
                g_app.show_preset_window = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About", "", nullptr)) {
                g_app.show_help_window = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

static void DrawSpacing(float w, float h)
{
    ImGui::Dummy(ImVec2{ w, h });
}

static void DrawSeparator(float margin)
{
    DrawSpacing(0, margin);
    ImGui::Separator();
    DrawSpacing(0, margin);
}

static bool DrawTextInput(const char* label, std::string& str, float spacing, ImGuiInputTextFlags flags = 0, const char* help = nullptr, bool report_change = true)
{
    ImGui::Text(label);
    ImGui::SameLine();
    DrawSpacing(spacing, 0);
    if (help) {
        ImGui::SameLine();
        DrawHelpMarker(help);
    }
    ImGui::SameLine();

    if (flags & ImGuiInputTextFlags_ReadOnly) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, INPUT_TEXT_READ_ONLY_COLOR);
    }

    ImGui::PushID(label);
    bool changed = ImGui::InputTextWithHint("", label, &str, flags);
    ImGui::PopID();

    if (changed && report_change) {
        ReportConfigChange(label, str);
    }

    if (flags & ImGuiInputTextFlags_ReadOnly) {
        ImGui::PopStyleColor(1);
    }

    return changed;
}

static bool DrawFileInput(const char* label, config::ConfigPath path, float spacing)
{
    bool changed = DrawTextInput(label, g_app.current_config.config_paths[path], spacing, 0, g_config_paths_help[path]);
    if (changed) {
        if (path == config::PATH_MAP_SOURCE) {
            HandleMapSourceChanged();
        }
    }

    ImGui::SameLine();

    ImGui::PushID(path);
    if (ImGui::Button("...")) {
        HandlePathSelect(path);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICOFONT_EXTERNAL)) {
        HandlePathOpen(path);
    }
    ImGui::PopID();

    return changed;
}

static void DrawPresetCombo()
{
    ImGui::Text("Preset: "); ImGui::SameLine();

    float offs = 9.0f;
    DrawSpacing(42+offs, 0); ImGui::SameLine();

    std::string selected_preset_name = "None";
    if (g_app.current_config.selected_preset_index > 0) {
        selected_preset_name = GetPreset(g_app.current_config.selected_preset_index).name;

        if (g_app.modified_flags) {
            selected_preset_name.append(" (modified)");
        }
    }

    if (ImGui::BeginCombo("##presets", selected_preset_name.c_str())) {
        if (ImGui::Selectable("None", false)) {
            g_app.current_config.selected_preset_index = 0;
            CopyPreset({});
        }

        for (std::size_t i = 0; i < g_app.user_config.tool_presets.size(); i++) {
            auto& preset = g_app.user_config.tool_presets[i];
            bool selected = (i + 1) == g_app.current_config.selected_preset_index;

            if (ImGui::Selectable(preset.name.c_str(), selected)) {
                HandleSelectPreset(i + 1);
            }
        }

        DrawSeparator(2);

        if (ImGui::Selectable("Save current options as new preset", false)) {
            g_app.show_preset_window = true;
            g_app.save_current_tools_options_as_preset = true;
        }

        DrawSeparator(2);

        if (ImGui::Selectable("Manage presets...", false)) {
            g_app.show_preset_window = true;
        }
        ImGui::EndCombo();
    }
}

static bool DrawToolParams(const char* label, int tool_flag, std::string& args, float spacing)
{
    bool changed = false;
    ImGui::PushID(tool_flag);

    if (tool_flag) {
        bool checked = g_app.current_config.tool_flags & tool_flag;
        if (ImGui::Checkbox(label, &checked)) {
            changed = true;

            ReportConfigChange(label, checked ? "enabled" : "disabled");

            if (checked) {
                g_app.current_config.tool_flags |= tool_flag;
            }
            else {
                g_app.current_config.tool_flags = g_app.current_config.tool_flags & ~tool_flag;
            }
        }
    }
    else {
        DrawSpacing(14, 0); ImGui::SameLine();
        ImGui::Text(label);
    }
    ImGui::SameLine();

    DrawSpacing(spacing, 0);
    ImGui::SameLine();

    if (ImGui::InputTextWithHint("", "command-line arguments", &args)) {
        changed = true;
        ReportConfigChange(std::string(label) + " args", args);
    }

    if (tool_flag) {
        ImGui::SameLine();
        if (ImGui::Button("Help")) {
            HandleToolHelp(tool_flag);
        }
    }

    ImGui::PopID();

    return changed;
}

static bool DrawPresetToolParams(config::ToolPreset& preset, const char* label, int tool_flag, std::string& args, float spacing, ImGuiInputTextFlags flags = 0)
{
    bool changed = false;
    ImGui::PushID(tool_flag);

    if (tool_flag) {
        bool checked = preset.flags & tool_flag;
        if (ImGui::Checkbox(label, &checked)) {
            changed = true;
            if (checked) {
                preset.flags |= tool_flag;
            }
            else {
                preset.flags = preset.flags & ~tool_flag;
            }
        }
    }
    else {
        DrawSpacing(14, 0); ImGui::SameLine();
        ImGui::Text(label);
    }
    ImGui::SameLine();

    DrawSpacing(spacing, 0);
    ImGui::SameLine();

    if (flags & ImGuiInputTextFlags_ReadOnly) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, INPUT_TEXT_READ_ONLY_COLOR);
    }

    if (ImGui::InputTextWithHint("", "command-line arguments", &args, flags)) {
        changed = true;
    }

    if (flags & ImGuiInputTextFlags_ReadOnly) {
        ImGui::PopStyleColor();
    }

    ImGui::PopID();

    return changed;
}

static void DrawConsoleView()
{
    static int num_entries;

    float height = (float)g_app.window_height - ImGui::GetCursorPosY() - 70.0f;
    height = std::fmaxf(100.0f, height);

    if (ImGui::BeginChild("ConsoleView", ImVec2{ (float)g_app.window_width - 28, height }, true)) {
    
        auto& ents = console::GetLogEntries();
        for (const auto& ent : ents) {
            ImVec4 col = ImVec4{ 0.9f, 0.9f, 0.9f, 1.0 };
            if (ent.level == LOG_ERROR) {
                col = ImVec4{ 1.0f, 0.0f, 0.0f, 1.0 };
            }
            ImGui::TextColored(col, ent.text.c_str());
        }

        bool should_scroll_down = (
            (num_entries != ents.size() && g_app.console_auto_scroll) || g_app.console_lock_scroll
        );
        if (should_scroll_down) {
            ImGui::SetScrollHereY();
        }

        num_entries = ents.size();
    }

    ImGui::EndChild();
}

// tool preset actions
static constexpr int TOOL_PRESET_DUPLICATE = 1;
static constexpr int TOOL_PRESET_REMOVE = 2;

static int DrawPresetListItem(config::ToolPreset& preset, int index)
{
    int result = 0;
    if (ImGui::TreeNode("", preset.name.c_str())) {
        DrawSpacing(0, 5);

        ImGuiInputTextFlags flags = preset.builtin ? ImGuiInputTextFlags_ReadOnly : 0;

        DrawTextInput("Name: ", preset.name, 24, flags, nullptr, false);

        DrawPresetToolParams(preset, "QBSP", CONFIG_FLAG_QBSP_ENABLED, preset.qbsp_args, 13, flags);
        DrawPresetToolParams(preset, "LIGHT", CONFIG_FLAG_LIGHT_ENABLED, preset.light_args, 5, flags);
        DrawPresetToolParams(preset, "VIS", CONFIG_FLAG_VIS_ENABLED, preset.vis_args, 20, flags);
        //DrawPresetToolParams(preset, "QUAKE", 0, preset.quake_args, 8);

        ImGui::Text("Action: "); ImGui::SameLine();  DrawSpacing(10, 0); ImGui::SameLine();
        if (ImGui::BeginCombo("##actions", "Actions...")) {
            if (ImGui::Selectable("Copy")) {
                result = TOOL_PRESET_DUPLICATE;
            }
            if (ImGui::Selectable("Select")) {
                HandleSelectPreset(index + 1);
            }
            if (ImGui::Selectable("Export")) {
                HandleExportPreset(index);
            }
            if (!preset.builtin) {
                if (ImGui::Selectable("Remove")) {
                    result = TOOL_PRESET_REMOVE;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TreePop();
    }
    return result;
}

static void DrawPresetWindow()
{
    static bool prev_show;

    if (g_app.show_preset_window) {
        if (!prev_show) {
            ImGui::OpenPopup("Manage Tools Presets");
        }

        ImGui::SetNextWindowSize({ g_app.window_width*0.5f, g_app.window_height * 0.7f }, ImGuiCond_Always);
        ImGui::SetNextWindowPos({ g_app.window_width * 0.5f, g_app.window_height * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (ImGui::BeginPopupModal("Manage Tools Presets", &g_app.show_preset_window, flags)) {
            static int newly_added_index = -1;
            int remove_index = -1;
            int dup_index = -1;

            // Check conditions to add new preset.
            if (g_app.save_current_tools_options_as_preset) {
                newly_added_index = g_app.user_config.tool_presets.size();

                config::ToolPreset preset = {};
                preset.name = "New Preset";
                preset.flags = g_app.current_config.tool_flags;
                preset.qbsp_args = g_app.current_config.qbsp_args;
                preset.light_args = g_app.current_config.light_args;
                preset.vis_args = g_app.current_config.vis_args;
                g_app.user_config.tool_presets.push_back(preset);

                g_app.save_current_tools_options_as_preset = false;
            }

            if (ImGui::Button("Add Preset")) {
                newly_added_index = g_app.user_config.tool_presets.size();

                config::ToolPreset preset = {};
                preset.name = "New Preset";
                g_app.user_config.tool_presets.push_back(preset);
            }

            ImGui::SameLine();
            if (ImGui::Button("Import Preset")) {
                HandleImportPreset();
            }

            DrawSpacing(0, 5);

            if (ImGui::BeginChild("Preset list")) {
                for (int i = 0; i < (int)g_app.user_config.tool_presets.size(); i++) {
                    auto& preset = g_app.user_config.tool_presets[i];

                    ImGui::PushID(i);

                    // If it's a newly added preset, set it open
                    if (newly_added_index != -1 && (i == newly_added_index)) {
                        ImGui::SetNextTreeNodeOpen(true);
                        newly_added_index = -1;
                    }

                    int action = DrawPresetListItem(preset, i);
                    if (action == TOOL_PRESET_DUPLICATE) {
                        dup_index = i;
                    }
                    else if (action == TOOL_PRESET_REMOVE) {
                        remove_index = i;
                    }

                    ImGui::PopID();

                    DrawSeparator(5);
                }
            }
            ImGui::EndChild();

            if (remove_index != -1) {
                if (remove_index + 1 == g_app.current_config.selected_preset_index) {
                    g_app.current_config.selected_preset_index = 0;
                }

                g_app.user_config.tool_presets.erase(g_app.user_config.tool_presets.begin() + remove_index);
            }
            if (dup_index != -1) {
                auto dup_preset = g_app.user_config.tool_presets[dup_index];
                dup_preset.builtin = false;
                while (common::StrReplace(dup_preset.name, "(built-in)", "")) {}
                dup_preset.name.append(" (copy)");

                auto it = g_app.user_config.tool_presets.begin() + dup_index + 1;
                g_app.user_config.tool_presets.insert(it, dup_preset);

                newly_added_index = dup_index + 1;
            }

            ImGui::EndPopup();
        }

        // Just closed
        if (!g_app.show_preset_window) {
            config::WriteUserConfig(g_app.user_config);
        }
    }

    prev_show = g_app.show_preset_window;
}

static void DrawUnsavedChangesWindow()
{
    static bool prev_show;

    if (g_app.show_unsaved_changes_window) {
        if (!prev_show) {
            ImGui::OpenPopup("Save Changes");
        }

        bool clicked_btn = false;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (ImGui::BeginPopupModal("Save Changes", &g_app.show_unsaved_changes_window, flags)) {
            ImGui::Text("You have unsaved changes to your config, do you want to save?");
            DrawSeparator(5);

            if (ImGui::Button("Save")) {
                ImGui::CloseCurrentPopup();
                clicked_btn = true;
                g_app.show_unsaved_changes_window = false;
                HandleSaveConfig();
                g_app.unsaved_changes_callback(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't save")) {
                ImGui::CloseCurrentPopup();
                clicked_btn = true;
                g_app.show_unsaved_changes_window = false;
                g_app.unsaved_changes_callback(false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
                clicked_btn = true;
                g_app.show_unsaved_changes_window = false;
            }
            ImGui::EndPopup();
        }

        // Just closed
        if (!g_app.show_unsaved_changes_window && !clicked_btn) {
            g_app.modified = false;
            HandleNewConfig();
        }
    }

    prev_show = g_app.show_unsaved_changes_window;
}

static void DrawCredits()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.5f, 0.5f, 0.5f, 1.0f });
    DrawSpacing(0, 12);
    ImGui::Text("Quake 1 Compile GUI (%s)", APP_VERSION);
    ImGui::PopStyleColor();
}

static void DrawHelpWindow()
{
    static bool prev_show;
    static ImGui::MarkdownConfig mdConfig = {};

    if (g_app.show_help_window) {
        if (!prev_show) {
            ImGui::OpenPopup("About");
        }

        bool clicked_btn = false;
        ImGuiWindowFlags flags = 0;
        ImGui::SetNextWindowSize(ImVec2{ g_app.window_width * 0.75f, g_app.window_height * 0.75f }, ImGuiCond_Always);
        //ImGui::SetNextWindowPosCenter(ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("About", &g_app.show_help_window, flags)) {
            auto readmeText = GetReadmeText();
            ImGui::Markdown( readmeText.c_str(), readmeText.length(), mdConfig );
            ImGui::EndPopup();
        }
    }

    prev_show = g_app.show_help_window;
}

static void DrawMainContent()
{
    ImGui::SetNextItemOpen(g_app.user_config.ui_section_info_open, ImGuiCond_Once);
    if (ImGui::TreeNode("Info")) {
        g_app.user_config.ui_section_info_open = true;

        DrawSpacing(0, 5);

        float offs = 9.0f;
        if (DrawTextInput("Config Name: ", g_app.current_config.config_name, 4 + offs)) g_app.modified = true;
        DrawTextInput("Path: ", g_app.user_config.loaded_config, 55 + offs, ImGuiInputTextFlags_ReadOnly);

        ImGui::TreePop();
    }
    else {
        g_app.user_config.ui_section_info_open = false;
    }

    DrawSeparator(5);

    ImGui::SetNextItemOpen(g_app.user_config.ui_section_paths_open, ImGuiCond_Once);
    if (ImGui::TreeNode("Paths")) {
        g_app.user_config.ui_section_paths_open = true;

        DrawSpacing(0, 5);

        float offs = -15.0f;
        if (DrawFileInput("Tools Dir:", config::PATH_TOOLS_DIR, 20 + offs)) g_app.modified = true;
        if (DrawFileInput("Work Dir:", config::PATH_WORK_DIR, 27 + offs)) g_app.modified = true;
        if (DrawFileInput("Output Dir:", config::PATH_OUTPUT_DIR, 13.5f + offs)) g_app.modified = true;
        if (DrawFileInput("Editor Exe:", config::PATH_EDITOR_EXE, 13.5f + offs)) g_app.modified = true;
        if (DrawFileInput("Engine Exe:", config::PATH_ENGINE_EXE, 13.5f + offs)) g_app.modified = true;
        if (DrawFileInput("Map Source:", config::PATH_MAP_SOURCE, 13.5f + offs)) g_app.modified = true;

        ImGui::TreePop();
    }
    else {
        g_app.user_config.ui_section_paths_open = false;
    }

    DrawSeparator(5);

    ImGui::SetNextItemOpen(g_app.user_config.ui_section_tools_open, ImGuiCond_Once);
    if (ImGui::TreeNode("Tools options")) {
        g_app.user_config.ui_section_tools_open = true;

        DrawSpacing(0, 5);

        DrawPresetCombo();

        float offs = 9.0f;
        if (DrawToolParams("QBSP", CONFIG_FLAG_QBSP_ENABLED, g_app.current_config.qbsp_args, 45 + offs)) {
            g_app.modified = true;
            g_app.modified_flags = true;
        }
        if (DrawToolParams("LIGHT", CONFIG_FLAG_LIGHT_ENABLED, g_app.current_config.light_args, 37 + offs)) {
            g_app.modified = true;
            g_app.modified_flags = true;
        }
        if (DrawToolParams("VIS", CONFIG_FLAG_VIS_ENABLED, g_app.current_config.vis_args, 52 + offs)) {
            g_app.modified = true;
            g_app.modified_flags = true;
        }
        if (DrawToolParams("QUAKE", 0, g_app.current_config.quake_args, 41 + offs)) {
            g_app.modified = true;
        }
        ImGui::TreePop();
    }
    else {
        g_app.user_config.ui_section_tools_open = false;
    }

    DrawSeparator(5);

    ImGui::SetNextItemOpen(g_app.user_config.ui_section_other_open, ImGuiCond_Once);
    if (ImGui::TreeNode("Other options")) {
        g_app.user_config.ui_section_other_open = true;

        DrawSpacing(0, 5);
        if (ImGui::Checkbox("Watch map file for changes and pre-compile", &g_app.current_config.watch_map_file)) {
            g_app.modified = true;
            g_app.map_file_watcher->SetEnabled(g_app.current_config.watch_map_file);
        }
        ImGui::SameLine();
        DrawHelpMarker("Watch the map source file for saves/changes and automatically compile. It will also compile when q1compile is launched.");

        if (g_app.current_config.watch_map_file) {
            float spacing = ImGui::GetTreeNodeToLabelSpacing();
            DrawSpacing(spacing, 0);ImGui::SameLine();
            if (ImGui::Checkbox("Apply -onlyents automatically (experimental)", &g_app.current_config.auto_apply_onlyents)) {
                g_app.modified = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Apply the -onlyents argument to QBSP and/or LIGHT depending on what has changed in the map. "
                "If a brush changes, it will run a full compile. "
                "If a light changes, it will apply -onlyents to QBSP. "
                "If only point entities changes, it will apply -onlyents to both QBSP and LIGHT. "
            );
            DrawSpacing(0, 5.0f);
        }

        if (ImGui::Checkbox("Use map mod as -game argument (TB only)", &g_app.current_config.use_map_mod)) {
            g_app.modified = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Use the '_tb_mod' worldspawn field as the '-game' argument for the Quake engine. "
            "In case multiple mods were used, the first one is picked. This option only works if TrenchBroom was used as the editor."
        );

        if (ImGui::Checkbox("Quake console output enabled", &g_app.current_config.quake_output_enabled)) {
            g_app.modified = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Option to display the output from the Quake engine while it's running.");

        if (ImGui::Checkbox("Compile map on launch", &g_app.current_config.compile_map_on_launch)) {
            g_app.modified = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Compile the map as soon as q1compile launches.");

        if (ImGui::Checkbox("Open editor on launch", &g_app.current_config.open_editor_on_launch)) {
            g_app.modified = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Open the map in the editor as soon as q1compile launches.");

        ImGui::TreePop();
    }
    else {
        g_app.user_config.ui_section_other_open = false;
    }

    DrawSeparator(5);

    ImGui::Text("Status: "); ImGui::SameLine();
    ImGui::InputText("##status", &g_app.compile_status, ImGuiInputTextFlags_ReadOnly);
    if (g_app.map_has_leak) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{ 1.0f, 0.0f, 0.0f, 1.0 }, ICOFONT_EXCLAMATION_TRI " Map has leak");
    }

    DrawSpacing(0, 10);

    DrawConsoleView();

    DrawCredits();

    DrawPresetWindow();

    DrawHelpWindow();

    DrawUnsavedChangesWindow();
}

static void DrawMainWindow()
{
    ImGuiWindowFlags flags = (
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings
    );

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

    ImGui::SetNextWindowPos(ImVec2{ 0, 18 }, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2{ (float)g_app.window_width, (float)g_app.window_height }, ImGuiCond_Always);
    if (ImGui::Begin("##main", nullptr, flags)) {
        if (ImGui::BeginChild("##content", ImVec2{ (float)g_app.window_width - 28, (float)g_app.window_height - 28 } )) {
            DrawMainContent();
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleVar(1);
}

/*
==============================
Application lifetime functions
==============================
*/

void qc_init(void* pdata)
{    
    g_app.platform_data = pdata;
    g_app.app_running = true;

    console::ClearConsole();
    console::SetErrorLogFile("q1compile_err.log");

    g_app.map_file_watcher = std::make_unique<file_watcher::FileWatcher>("", WATCH_MAP_FILE_INTERVAL);
    g_app.compile_queue = std::make_unique<work_queue::WorkQueue>(std::size_t{ 1 }, std::size_t{ 128 });
    g_app.compile_status = "Doing nothing.";

    g_app.user_config = config::ReadUserConfig();
    config::MigrateUserConfig(g_app.user_config);

    for (auto& preset : g_app.user_config.tool_presets) {
        while (common::StrReplace(preset.name, "(built-in)", "")) {}
    }
    AddBuiltinPresets();

    if (!g_app.user_config.loaded_config.empty()) {
        // Load the last config
        if (!LoadConfig(g_app.user_config.loaded_config)) {
            g_app.user_config.loaded_config = "";
        }

        // If watching a map, compile on launch
        if (g_app.current_config.compile_map_on_launch
            && !g_app.current_config.config_paths[config::PATH_MAP_SOURCE].empty()) {
            compile::StartCompileJob(g_app.current_config, false, true);
        }

        // If enabled, open the editor on launch
        if (g_app.current_config.open_editor_on_launch
            && !g_app.current_config.config_paths[config::PATH_MAP_SOURCE].empty()
            && !g_app.current_config.config_paths[config::PATH_EDITOR_EXE].empty()) {
            HandleOpenMapInEditor();
        }
    }
    else {
        HandleNewConfig();
        g_app.show_help_window = true;
    }
}

void qc_key_down(unsigned int key)
{
    auto& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    switch (key) {
    case 'C':
        if (io.KeyCtrl) {
            if (io.KeyShift) {
                HandleCompileOnly();
            }
            else {
                HandleCompileAndRun();
            }
        }
        break;
    case 'B':
        if (io.KeyCtrl) {
            HandleStopCompiling();
        }
        break;
    case 'E':
        if (io.KeyCtrl) {
            if (io.KeyShift) {
                HandleOpenEditor();
            }
            else {
                HandleOpenMapInEditor();
            }
        }
        break;
    case 'R':
        if (io.KeyCtrl) {
            HandleRun();
        }
        break;
    case 'N':
        if (io.KeyCtrl) {
            HandleNewConfig();
        }
        break;
    case 'O':
        if (io.KeyCtrl) {
            HandleLoadConfig();
        }
        break;
    case 'S':
        if (io.KeyCtrl) {
            if (io.KeyShift) {
                HandleSaveConfigAs();
            }
            else {
                HandleSaveConfig();
            }
        }
        break;
    case 'P':
        if (io.KeyCtrl) {
            g_app.show_preset_window = true;
        }
        break;
    case 'W':
        if (io.KeyCtrl && io.KeyShift) {
            HandleResetWorkDir();
        }
        break;
    case 'Q':
        if (io.KeyCtrl) {
            g_app.app_running = false;
        }
        break;
    }
}

void qc_resize(unsigned int width, unsigned int height)
{
    g_app.window_width = width;
    g_app.window_height = height - 18;
}

bool qc_render()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (g_app.map_file_watcher->Update(dt)) {
        HandleAutoCompile();
    }

    while (!g_app.compile_output.empty()) {
        char c = g_app.compile_output.pop();
        console::Print(c);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.9f, 0.9f, 0.9f, 1.0f });
    DrawMenuBar();
    DrawMainWindow();
    ImGui::PopStyleColor(1);

    return g_app.app_running;
}

void qc_deinit()
{
    config::WriteUserConfig(g_app.user_config);
}