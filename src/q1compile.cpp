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
#include "config.h"
#include "file_watcher.h"
#include "map_file.h"
#include "mutex_char_buffer.h"
#include "path.h"
#include "sub_process.h"
#include "shell_command.h"
#include "work_queue.h"
#include "../include/imgui_markdown.h" // https://github.com/juliettef/imgui_markdown/
#include "fonts/icofont.h"

#define CONFIG_RECENT_COUNT 5

typedef int FileBrowserFlags;

typedef std::function<void(bool)> UnsavedChangesCallback;

enum FileBrowserFlag {
    FB_FLAG_LOAD = 0x1,
    FB_FLAG_SAVE = 0x2,
    FB_FLAG_MUST_EXIST = 0x4,
};

enum FileBrowserCallback {
    FB_LOAD_CONFIG,
    FB_SAVE_CONFIG,
    FB_IMPORT_PRESET,
    FB_EXPORT_PRESET,
    FB_CONFIG_STR,
};

static const ImVec4 INPUT_TEXT_READ_ONLY_COLOR{ 0.15f, 0.15f, 0.195f, 1.0f };

struct AppState {
    Config                       current_config;
    UserConfig                   user_config;
    std::string                  last_loaded_config_name;

    std::unique_ptr<WorkQueue>   compile_queue;
    MutexCharBuffer              compile_output;
    std::atomic_bool             compiling;
    std::atomic_bool             stop_compiling;
    std::string                  compile_status;
    bool                         last_job_ran_quake;
    std::atomic_bool             map_has_leak = false;

    FileBrowserCallback          fb_callback;
    std::string                  fb_path;

    std::unique_ptr<MapFile>     map_file;
    std::unique_ptr<FileWatcher> map_file_watcher;

    std::atomic_bool             console_auto_scroll = true;
    std::atomic_bool             console_lock_scroll = false;

    UnsavedChangesCallback       unsaved_changes_callback;

    bool modified = false;
    bool modified_flags = false;
    bool show_preset_window = false;
    bool show_unsaved_changes_window = false;
    bool show_help_window = false;
    bool save_current_tools_options_as_preset = false;
    int preset_to_export = 0;

    int window_width = WINDOW_WIDTH;
    int window_height = WINDOW_HEIGHT;
    bool app_running = false;

    void* platform_data = NULL;
};

static AppState g_app;

static std::vector<ToolPreset> g_builtin_presets = {
    ToolPreset{
        "No Vis (built-in)",
        "",
        "",
        "",
        (CONFIG_FLAG_QBSP_ENABLED | CONFIG_FLAG_LIGHT_ENABLED),
        true
    },
    ToolPreset{
        "No Vis, Only ents (built-in)",
        "-onlyents",
        "-onlyents",
        "",
        (CONFIG_FLAG_QBSP_ENABLED | CONFIG_FLAG_LIGHT_ENABLED),
        true
    },
    ToolPreset{
        "Fast Vis (built-in)",
        "",
        "",
        "-fast",
        (CONFIG_FLAG_QBSP_ENABLED | CONFIG_FLAG_LIGHT_ENABLED | CONFIG_FLAG_VIS_ENABLED),
        true
    },
    ToolPreset{
        "Full (built-in)",
        "",
        "-extra4",
        "-level 4",
        (CONFIG_FLAG_QBSP_ENABLED | CONFIG_FLAG_LIGHT_ENABLED | CONFIG_FLAG_VIS_ENABLED),
        true
    },
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

static void ExecuteCompileProcess(const std::string& cmd, const std::string& pwd, bool suppress_output = false);

static void HandleFileBrowserCallback();

static void ReportCopy(const std::string& from_path, const std::string& to_path)
{
    g_app.compile_output.append("Copied ");
    g_app.compile_output.append(from_path);
    g_app.compile_output.append(" to ");
    g_app.compile_output.append(to_path);
    g_app.compile_output.append("\n");
}

static ToolPreset GetMapDiffArgs(const MapFile& map_a, const MapFile& map_b)
{
    ToolPreset pre = {};
    auto flags = GetDiffFlags(map_a, map_b);

    if (flags & MAP_DIFF_BRUSHES) {
        pre.flags |= CONFIG_FLAG_QBSP_ENABLED | CONFIG_FLAG_LIGHT_ENABLED | CONFIG_FLAG_VIS_ENABLED;
    }
    else if (flags & MAP_DIFF_LIGHTS) {
        pre.flags |= CONFIG_FLAG_QBSP_ENABLED | CONFIG_FLAG_LIGHT_ENABLED;
        pre.qbsp_args = "-onlyents";
    }
    else if (flags & MAP_DIFF_ENTS) {
        pre.flags |= CONFIG_FLAG_QBSP_ENABLED | CONFIG_FLAG_LIGHT_ENABLED;
        pre.qbsp_args = "-onlyents";
        pre.light_args = "-onlyents";
    }
    
    return pre;
}

/*
=================
Async jobs
=================
*/

struct CompileJob
{
    Config config;
    bool run_quake;
    bool ignore_diff;

    bool RunTool(const std::string& exe, const std::string& args)
    {
        std::string cmd = PathJoin(PathFromNative(config.config_paths[PATH_TOOLS_DIR]), exe);
        if (!PathExists(cmd)) {
            PrintError(exe.c_str());
            PrintError(" not found, is the tools directory right?");
            return false;
        }

        g_app.compile_status = exe + " " + args;

        cmd.append(" ");
        cmd.append(args);
        ExecuteCompileProcess(cmd, "");
        return true;
    }

    void operator()()
    {
        g_app.compile_status = "Preparing to compile...";

        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto time_begin = std::chrono::system_clock::now();

        std::string source_map = PathFromNative(config.config_paths[PATH_MAP_SOURCE]);
        std::string work_map = PathJoin(PathFromNative(config.config_paths[PATH_WORK_DIR]), PathFilename(source_map));
        if (source_map == work_map) {
            g_app.compile_status = "Stopped.";
            PrintError("ERROR: 'Work Dir' is the same as the map source directory, there's a risk of messing with your files, compilation will not proceed!\n");
            Print("Please set the 'Work Dir' to somewhere different.\n");
            Print("If you don't care about this setting, use the menu 'Compile -> Reset Work Dir' or press 'Ctrl + Shift + W'.\n");
            return;
        }

        std::string work_bsp = work_map;
        std::string work_lit = work_map;
        bool copy_bsp = false;
        bool copy_lit = false;
        StrReplace(work_bsp, ".map", ".bsp");
        StrReplace(work_lit, ".map", ".lit");

        auto prev_map_file = std::unique_ptr<MapFile>(g_app.map_file.release());
        g_app.map_file = std::make_unique<MapFile>(source_map);
        
        g_app.map_has_leak = false;
        g_app.compiling = true;
        g_app.compile_status = "Copying source file to work dir...";
        ScopeGuard end{ [work_bsp, source_map]() {
            g_app.compiling = false;
            g_app.stop_compiling = false;
            g_app.console_lock_scroll = false;

            {
                // copy .pts file if generated
                std::string work_pts = work_bsp;
                while (StrReplace(work_pts, ".bsp", ".pts")) {}

                if (PathExists(work_pts)) {
                    std::string source_pts = source_map;
                    while (StrReplace(source_pts, ".map", ".pts")) {}
                    if (Copy(work_pts, source_pts)) {
                        ReportCopy(work_pts, source_pts);
                        RemovePath(work_pts);
                        g_app.map_has_leak = true;
                    }
                }
            }

            if (g_app.compile_status.find("Finished") == std::string::npos) {
                g_app.compile_status = "Stopped.";
            }
        } };

        auto ReportCompileParams = [this](const std::string& name) {
            g_app.compile_output.append(name);
            if (config.tool_flags & CONFIG_FLAG_QBSP_ENABLED)
                g_app.compile_output.append(" qbsp=true");
            else
                g_app.compile_output.append(" qbsp=false");

            if (config.tool_flags & CONFIG_FLAG_LIGHT_ENABLED)
                g_app.compile_output.append(" light=true");
            else
                g_app.compile_output.append(" light=false");

            if (config.tool_flags & CONFIG_FLAG_VIS_ENABLED)
                g_app.compile_output.append(" vis=true");
            else
                g_app.compile_output.append(" vis=false");

            g_app.compile_output.append("\n");
        };
        ReportCompileParams("Starting to compile:");

        if (!g_app.map_file->Good()) {
            g_app.compile_output.append("Could not read map file!\n");
        }
        else {
            if (prev_map_file && g_app.current_config.watch_map_file && g_app.current_config.auto_apply_onlyents && !this->ignore_diff) {
                g_app.compile_output.append("Doing map diff...\n");

                ToolPreset diff_pre = GetMapDiffArgs(*prev_map_file, *g_app.map_file);
                config.tool_flags = diff_pre.flags;
                config.qbsp_args.append(" " + diff_pre.qbsp_args);
                config.light_args.append(" " + diff_pre.light_args);

                ReportCompileParams("Map diff results:");
                g_app.compile_output.append("Diff QBSP args: " + diff_pre.qbsp_args + "\n");
                g_app.compile_output.append("Diff LIGHT args: " + diff_pre.light_args + "\n");
            }
        }

        if (Copy(source_map, work_map)) {
            ReportCopy(source_map, work_map);
        }
        else {
            return;
        }

        if (g_app.stop_compiling) return;

        if (config.tool_flags & CONFIG_FLAG_QBSP_ENABLED) {
            g_app.compile_output.append("Starting qbsp\n");

            copy_bsp = true;

            std::string args = config.qbsp_args + " " + work_map;
            if (!RunTool("qbsp.exe", args))
                return;

            g_app.compile_output.append("Finished qbsp\n");
            g_app.compile_output.append("------------------------------------------------\n");
        }
        if (g_app.stop_compiling) return;

        if (config.tool_flags & CONFIG_FLAG_LIGHT_ENABLED) {
            g_app.compile_output.append("Starting light\n");

            copy_lit = true;
            std::string args = config.light_args + " " + work_bsp;
            if (!RunTool("light.exe", args))
                return;

            g_app.compile_output.append("Finished light\n");
            g_app.compile_output.append("------------------------------------------------\n");
        }
        if (g_app.stop_compiling) return;

        if (config.tool_flags & CONFIG_FLAG_VIS_ENABLED) {
            g_app.compile_output.append("Starting vis\n");

            std::string args = config.vis_args + " " + work_bsp;
            if (!RunTool("vis.exe", args))
                return;

            g_app.compile_output.append("Finished vis\n");
            g_app.compile_output.append("------------------------------------------------\n");
        }
        if (g_app.stop_compiling) return;

        std::string out_bsp = PathJoin(PathFromNative(config.config_paths[PATH_OUTPUT_DIR]), PathFilename(work_bsp));
        std::string out_lit = PathJoin(PathFromNative(config.config_paths[PATH_OUTPUT_DIR]), PathFilename(work_lit));

        if (copy_bsp && PathExists(work_bsp)) {
            if (Copy(work_bsp, out_bsp)) {
                ReportCopy(work_bsp, out_bsp);
            }
            else {
                return;
            }
        }
        
        if (copy_lit && PathExists(work_lit)) {
            if (Copy(work_lit, out_lit)) {
                ReportCopy(work_lit, out_lit);
            }
        }

        if (copy_lit || copy_bsp) g_app.compile_output.append("------------------------------------------------\n");

        auto time_end = std::chrono::system_clock::now();
        auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_begin);
        float secs = time_elapsed.count() / 1000.0f;
        g_app.compile_status = "Finished in " + std::to_string(secs) + " seconds.";
        g_app.compile_output.append(g_app.compile_status);
        g_app.compile_output.append("\n\n");

        if (run_quake) {
            g_app.compile_output.append("------------------------------------------------\n");

            std::string args = config.quake_args;

            if (g_app.current_config.use_map_mod && g_app.map_file.get() && (args.find("-game") == std::string::npos)) {
                args.append(" -game ");
                args.append(g_app.map_file->GetTBMod());
            }

            if (args.find("+map") == std::string::npos) {
                std::string map_arg = PathFilename(out_bsp);
                StrReplace(map_arg, ".bsp", "");

                args.append(" +map ");
                args.append(map_arg);
            }

            g_app.compile_status = "Running quake with command-line: " + args;

            g_app.compile_output.append(g_app.compile_status);
            g_app.compile_output.append("\n");

            std::string cmd = PathFromNative(config.config_paths[PATH_ENGINE_EXE]);
            std::string pwd = PathDirectory(cmd);
            cmd.append(" ");
            cmd.append(args);

            ExecuteCompileProcess(cmd, pwd, !config.quake_output_enabled);
        }
    }
};

struct HelpJob
{
    std::string tools_dir;
    int tool_flag;

    void operator()()
    {
        std::string cmd;
        switch (tool_flag) {
        case CONFIG_FLAG_QBSP_ENABLED:
            cmd = "qbsp.exe";
            break;
        case CONFIG_FLAG_LIGHT_ENABLED:
            cmd = "light.exe";
            break;
        case CONFIG_FLAG_VIS_ENABLED:
            cmd = "vis.exe";
            break;
        default:
            return;
        }

        std::string proc = PathJoin(PathFromNative(tools_dir), cmd);
        if (!PathExists(proc)) {
            PrintError(cmd.c_str());
            PrintError(" not found, is the tools directory right?\n");
            return;
        }

        ExecuteCompileProcess(proc, "");
    }
};

template<class T>
static void ReadToMutexCharBuffer(T& obj, std::atomic_bool* stop, MutexCharBuffer* out)
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
    SubProcess proc{ cmd, pwd };
    if (!proc.Good()) {
        PrintError(cmd.c_str());
        PrintError(": failed to open subprocess\n");
        return;
    }
    
    auto output = &g_app.compile_output;
    if (suppress_output) {
        output = nullptr;
    }

    SetPrintToFile(false);
    ReadToMutexCharBuffer(proc, &g_app.stop_compiling, output);
    SetPrintToFile(true);
}

static void ExecuteShellCommand(const std::string& cmd, const std::string& pwd)
{
    ShellCommand proc{ cmd, pwd };
    ReadToMutexCharBuffer(proc, nullptr, &g_app.compile_output);
}

static void StartCompileJob(const Config& cfg, bool run_quake, bool ignore_diff = false)
{
    g_app.console_auto_scroll = true;
    g_app.console_lock_scroll = true;
    ClearConsole();

    if (g_app.compiling) {
        g_app.stop_compiling = true;
    }

    g_app.last_job_ran_quake = run_quake;
    g_app.compile_queue->AddWork(0, CompileJob{ cfg, run_quake, ignore_diff });
}

static void AddExtensionIfNone(std::string& path, const std::string& extension)
{
    std::string root, ext;
    SplitExtension(path, root, ext);
    if (ext.empty()) {
        path = root + extension;
    }
}

static bool IsExtension(const std::string& path, const std::string& extension)
{
    std::string root, ext;
    SplitExtension(path, root, ext);
    return ext == extension;
}

static void ReportConfigChange(const std::string& name, const std::string& value)
{
    Print("Set '"); Print(name.c_str()); Print("' to "); Print(value.c_str()); Print("\n");
}

static void HandleMapSourceChanged()
{
    const std::string& path = g_app.current_config.config_paths[PATH_MAP_SOURCE];
    g_app.map_file_watcher->SetPath(path);
    g_app.map_file = std::make_unique<MapFile>(path);
    if (!g_app.map_file->Good()) {
        g_app.map_file = nullptr;
    }
}

static void SetConfigPath(ConfigPath path, const std::string& value)
{
    g_app.modified = true;
    g_app.current_config.config_paths[path] = value;
    ReportConfigChange(g_config_path_names[path], value);
    if (path == PATH_MAP_SOURCE) {
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

static bool ComparePresetToConfig(const ToolPreset& preset)
{
    if (preset.flags != g_app.current_config.tool_flags)
        return true;
    if (preset.qbsp_args != g_app.current_config.qbsp_args)
        return true;
    if (preset.light_args != g_app.current_config.light_args)
        return true;
    if (preset.vis_args != g_app.current_config.vis_args)
        return true;

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

    WriteConfig(g_app.current_config, path);
    g_app.user_config.loaded_config = path;
    g_app.last_loaded_config_name = g_app.current_config.config_name;
    WriteUserConfig(g_app.user_config);

    g_app.modified = false;
    g_app.console_auto_scroll = true;
    Print("Saved config: ");
    Print(path.c_str());
    Print("\n");
}

static bool LoadConfig(const std::string& path)
{
    if (!PathExists(path)) {
        g_app.console_auto_scroll = true;
        PrintError("File not found: ");
        PrintError(path.c_str());
        PrintError("\n");
        return false;
    }

    if (!IsExtension(path, ".cfg")) {
        g_app.console_auto_scroll = true;
        PrintError("Invalid config file: ");
        PrintError(path.c_str());
        PrintError("\n");
        return false;
    }

    g_app.map_has_leak = false;
    g_app.current_config = ReadConfig(path);
    g_app.current_config.selected_preset_index = FindPresetIndex(g_app.current_config.selected_preset) + 1;
    if (g_app.current_config.selected_preset_index > 0) {
        if (ComparePresetToConfig(g_app.user_config.tool_presets[g_app.current_config.selected_preset_index - 1])) {
            g_app.modified_flags = true;
        }
    }

    g_app.user_config.loaded_config = path;
    g_app.last_loaded_config_name = g_app.current_config.config_name;
    WriteUserConfig(g_app.user_config);

    g_app.map_file_watcher->SetEnabled(g_app.current_config.watch_map_file);
    HandleMapSourceChanged();

    g_app.modified = false;
    g_app.console_auto_scroll = true;
    Print("Loaded config: ");
    Print(path.c_str());
    Print("\n");
    return true;
}

static void CopyPreset(const ToolPreset& preset)
{
    g_app.modified = g_app.modified || ComparePresetToConfig(preset);
    g_app.current_config.qbsp_args = preset.qbsp_args;
    g_app.current_config.light_args = preset.light_args;
    g_app.current_config.vis_args = preset.vis_args;
    g_app.current_config.tool_flags = preset.flags;
    ReportConfigChange("QBSP", (preset.flags & CONFIG_FLAG_QBSP_ENABLED) ? "enabled" : "disabled");
    ReportConfigChange("LIGHT", (preset.flags & CONFIG_FLAG_LIGHT_ENABLED)  ? "enabled" : "disabled");
    ReportConfigChange("VIS", (preset.flags & CONFIG_FLAG_VIS_ENABLED)  ? "enabled" : "disabled");
    ReportConfigChange("QBSP args", (preset.qbsp_args));
    ReportConfigChange("LIGHT args", (preset.light_args));
    ReportConfigChange("VIS args", (preset.vis_args));
}

static ToolPreset& GetPreset(int i)
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

static bool ShouldConfigPathBeDirectory(ConfigPath path)
{
    return (path == PATH_TOOLS_DIR) || (path == PATH_WORK_DIR) || (path == PATH_OUTPUT_DIR);
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

    out = PathToNative(out);

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

        out = PathFromNative(path);
        return true;
    }

    return false;
}

bool SelectFileDialog(const char* filter, FileBrowserFlags flags, std::string& out)
{
    char buf[256];

    out = PathToNative(out);

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
        out = PathFromNative(user_path);
        return true;
    }

    DWORD err = CommDlgExtendedError();
    if (err) {
        PrintError("CommDlg Error: ");
        PrintValueError(err);
        PrintError("\n");
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
    std::string work_dir = PathJoin(qc_GetTempDir(), "q1compile");
    if (!PathExists(work_dir)) {
        if (!CreatePath(work_dir)) {
            PrintError("Could not create work dir!\n");
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
        g_app.current_config.config_paths[PATH_WORK_DIR] = GetDefaultWorkDir();
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
        std::string pwd = PathDirectory(g_app.user_config.loaded_config);
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
    std::string pwd = PathDirectory(g_app.user_config.loaded_config);
    std::string filename = g_app.current_config.config_name + ".cfg";
    std::string path = PathJoin(pwd, filename);
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
    StartCompileJob(g_app.current_config, true, true);
}

static void HandleCompileOnly()
{
    // don't run quake and ignore diff
    StartCompileJob(g_app.current_config, false, true);
}

static void HandleAutoCompile()
{
    // don't run quake and don't ignore diff
    StartCompileJob(g_app.current_config, false, false);
}

static void HandleStopCompiling()
{
    if (g_app.compiling) {
        g_app.stop_compiling = true;
    }
}

static void HandleRun()
{
    Config job_config = g_app.current_config;
    job_config.tool_flags = 0;

    if (!g_app.last_job_ran_quake) {
        // Instead of stopping the current compilation, just add the job to the queue
        g_app.compile_queue->AddWork(0, CompileJob{ job_config, true, true });
    }
    else {
        StartCompileJob(job_config, true, true);
    }
}

static void HandleToolHelp(int tool_flag)
{
    g_app.console_auto_scroll = false;
    g_app.console_lock_scroll = false;
    ClearConsole();
    g_app.compile_queue->AddWork(0, HelpJob{ g_app.current_config.config_paths[PATH_TOOLS_DIR], tool_flag });
}

static void HandlePathSelect(ConfigPath path)
{
    bool should_be_dir = ShouldConfigPathBeDirectory(path);
    std::string pwd = PathFromNative(g_app.current_config.config_paths[path]);
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

static void HandlePathOpen(ConfigPath path)
{
    std::string arg = PathFromNative(g_app.current_config.config_paths[path]);
    if (!ShouldConfigPathBeDirectory(path)) {
        arg = PathDirectory(arg);
    }
    if (!StartDetachedProcess("explorer " + PathToNative(arg), "")) {
        PrintError("Error opening path: ");
        PrintError(g_app.current_config.config_paths[path].c_str());
        PrintError("\n");
    }
}

static void HandleFileBrowserCallback()
{
    switch (g_app.fb_callback) {
    case FB_LOAD_CONFIG: {
        std::string path = PathFromNative(g_app.fb_path);
        if (!IsExtension(path, ".cfg")) {
            g_app.console_auto_scroll = true;
            PrintError("Invalid config file: ");
            PrintError(path.c_str());
            PrintError("\n");
            return;
        }

        if (PathExists(path)) {
            if (LoadConfig(path)) {
                AddRecentConfig(path);
            }
        }
    } break;

    case FB_SAVE_CONFIG: {
        std::string path = PathFromNative(g_app.fb_path);
        AddExtensionIfNone(path, ".cfg");
        SaveConfig(path);
    } break;

    case FB_IMPORT_PRESET: {
        std::string path = PathFromNative(g_app.fb_path);
        if (!IsExtension(path, ".pre")) {
            g_app.console_auto_scroll = true;
            PrintError("Invalid preset file: ");
            PrintError(path.c_str());
            PrintError("\n");
            return;
        }

        if (PathExists(path)) {
            ToolPreset preset = ReadToolPreset(path);
            if (preset.name.empty()) {
                g_app.console_auto_scroll = true;
                PrintError("Invalid preset file: ");
                PrintError(path.c_str());
                PrintError("\n");
                return;
            }

            while (StrReplace(preset.name, "(built-in)", "")) {}

            g_app.user_config.last_import_preset_location = PathDirectory(path);
            g_app.user_config.tool_presets.push_back(preset);
            WriteUserConfig(g_app.user_config);

            g_app.show_preset_window = true;

            g_app.console_auto_scroll = true;
            Print("Preset imported: ");
            Print(path.c_str());
            Print("\n");
        }
    } break;

    case FB_EXPORT_PRESET: {
        std::string path = PathFromNative(g_app.fb_path);
        AddExtensionIfNone(path, ".pre");

        auto exp_preset = g_app.user_config.tool_presets[g_app.preset_to_export];
        while (StrReplace(exp_preset.name, "(built-in)", "")) {}
        WriteToolPreset(exp_preset, path);
        
        g_app.user_config.last_export_preset_location = PathDirectory(path);
        WriteUserConfig(g_app.user_config);

        g_app.show_preset_window = true;

        g_app.console_auto_scroll = true;
        Print("Preset exported: ");
        Print(path.c_str());
        Print("\n");
    } break;

    default:
        if (g_app.fb_callback >= FB_CONFIG_STR)
            SetConfigPath(
                (ConfigPath)(g_app.fb_callback - FB_CONFIG_STR),
                PathFromNative(g_app.fb_path)
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
        PrintError("invalid preset: ");
        PrintValueError(i);
        PrintError("\n");
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
    std::string path = PathJoin(pwd, filename);
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
    std::string source_map = PathFromNative(g_app.current_config.config_paths[PATH_MAP_SOURCE]);
    std::string work_map = PathJoin(PathFromNative(g_app.current_config.config_paths[PATH_WORK_DIR]), PathFilename(source_map));
    std::string work_bsp = work_map;
    std::string work_lit = work_map;
    StrReplace(work_bsp, ".map", ".bsp");
    StrReplace(work_lit, ".map", ".lit");

    g_app.console_auto_scroll = true;
    if (RemovePath(work_bsp)) {
        Print("Removed ");
        Print(work_bsp.c_str());
        Print("\n");
    }
    if (RemovePath(work_lit)) {
        Print("Removed ");
        Print(work_lit.c_str());
        Print("\n");
    }
}

static void HandleClearOutputFiles()
{
    std::string source_map = PathFromNative(g_app.current_config.config_paths[PATH_MAP_SOURCE]);
    std::string out_map = PathJoin(PathFromNative(g_app.current_config.config_paths[PATH_OUTPUT_DIR]), PathFilename(source_map));
    std::string out_bsp = out_map;
    std::string out_lit = out_map;
    StrReplace(out_bsp, ".map", ".bsp");
    StrReplace(out_lit, ".map", ".lit");

    g_app.console_auto_scroll = true;
    if (RemovePath(out_bsp)) {
        Print("Removed ");
        Print(out_bsp.c_str());
        Print("\n");
    }
    if (RemovePath(out_lit)) {
        Print("Removed ");
        Print(out_lit.c_str());
        Print("\n");
    }
}

static void HandleResetWorkDir()
{
    std::string value = GetDefaultWorkDir();
    if (!value.empty()) {
        g_app.current_config.config_paths[PATH_WORK_DIR] = value;
        ReportConfigChange("Work Dir:", value);
    }
}

static std::string GetReadmeText()
{
    static std::string readme;

    if (readme.empty()) {
        if (!ReadFileText("q1compile_readme.txt", readme)) return "";
    }

    return readme;
}

static void LaunchEditorProcess(const std::string& cmd)
{
    if (!StartDetachedProcess(cmd, "")) {
        PrintError("Error launching editor: ");
        PrintError(g_app.current_config.config_paths[PATH_EDITOR_EXE].c_str());
        PrintError("\n");
    }
}

static void HandleOpenMapInEditor()
{
    auto cmd = g_app.current_config.config_paths[PATH_EDITOR_EXE];
    cmd.append(" ");
    cmd.append(g_app.current_config.config_paths[PATH_MAP_SOURCE]);
    LaunchEditorProcess(cmd);
}

static void HandleOpenEditor()
{
    auto cmd = g_app.current_config.config_paths[PATH_EDITOR_EXE];
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

        std::string filename = PathFilename(path);
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

static bool DrawFileInput(const char* label, ConfigPath path, float spacing)
{
    bool changed = DrawTextInput(label, g_app.current_config.config_paths[path], spacing, 0, g_config_paths_help[path]);
    if (changed) {
        if (path == PATH_MAP_SOURCE) {
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

static bool DrawPresetToolParams(ToolPreset& preset, const char* label, int tool_flag, std::string& args, float spacing, ImGuiInputTextFlags flags = 0)
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
    
        auto& ents = GetLogEntries();
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

static int DrawPresetListItem(ToolPreset& preset, int index)
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

                ToolPreset preset = {};
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

                ToolPreset preset = {};
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
                while (StrReplace(dup_preset.name, "(built-in)", "")) {}
                dup_preset.name.append(" (copy)");

                auto it = g_app.user_config.tool_presets.begin() + dup_index + 1;
                g_app.user_config.tool_presets.insert(it, dup_preset);

                newly_added_index = dup_index + 1;
            }

            ImGui::EndPopup();
        }

        // Just closed
        if (!g_app.show_preset_window) {
            WriteUserConfig(g_app.user_config);
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
    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::TreeNode("Info")) {
        DrawSpacing(0, 5);

        float offs = 9.0f;
        if (DrawTextInput("Config Name: ", g_app.current_config.config_name, 4 + offs)) g_app.modified = true;
        DrawTextInput("Path: ", g_app.user_config.loaded_config, 55 + offs, ImGuiInputTextFlags_ReadOnly);

        ImGui::TreePop();
    }

    DrawSeparator(5);

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::TreeNode("Paths")) {
        DrawSpacing(0, 5);

        float offs = -15.0f;
        if (DrawFileInput("Tools Dir:", PATH_TOOLS_DIR, 20 + offs)) g_app.modified = true;
        if (DrawFileInput("Work Dir:", PATH_WORK_DIR, 27 + offs)) g_app.modified = true;
        if (DrawFileInput("Output Dir:", PATH_OUTPUT_DIR, 13.5f + offs)) g_app.modified = true;
        if (DrawFileInput("Editor Exe:", PATH_EDITOR_EXE, 13.5f + offs)) g_app.modified = true;
        if (DrawFileInput("Engine Exe:", PATH_ENGINE_EXE, 13.5f + offs)) g_app.modified = true;
        if (DrawFileInput("Map Source:", PATH_MAP_SOURCE, 13.5f + offs)) g_app.modified = true;

        ImGui::TreePop();
    }

    DrawSeparator(5);

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::TreeNode("Tools options")) {
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

    DrawSeparator(5);

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    if (ImGui::TreeNode("Other options")) {
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

    ClearConsole();
    SetErrorLogFile("q1compile_err.log");

    g_app.map_file_watcher = std::make_unique<FileWatcher>("", WATCH_MAP_FILE_INTERVAL);
    g_app.compile_queue = std::make_unique<WorkQueue>(std::size_t{ 1 }, std::size_t{ 128 });
    g_app.compile_status = "Doing nothing.";

    g_app.user_config = ReadUserConfig();
    MigrateUserConfig(g_app.user_config);

    for (auto& preset : g_app.user_config.tool_presets) {
        while (StrReplace(preset.name, "(built-in)", "")) {}
    }
    AddBuiltinPresets();

    if (!g_app.user_config.loaded_config.empty()) {
        // Load the last config
        if (!LoadConfig(g_app.user_config.loaded_config)) {
            g_app.user_config.loaded_config = "";
        }

        // If watching a map, compile on launch
        if (g_app.current_config.compile_map_on_launch
            && !g_app.current_config.config_paths[PATH_MAP_SOURCE].empty()) {
            StartCompileJob(g_app.current_config, false, true);
        }

        // If enabled, open the editor on launch
        if (g_app.current_config.open_editor_on_launch
            && !g_app.current_config.config_paths[PATH_MAP_SOURCE].empty()
            && !g_app.current_config.config_paths[PATH_EDITOR_EXE].empty()) {
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
        Print(c);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.9f, 0.9f, 0.9f, 1.0f });
    DrawMenuBar();
    DrawMainWindow();
    ImGui::PopStyleColor(1);

    return g_app.app_running;
}

void qc_deinit()
{
    WriteUserConfig(g_app.user_config);
}