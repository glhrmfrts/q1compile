#include <memory>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <imfilebrowser.h>
#include "common.h"
#include "console.h"
#include "config.h"
#include "file_watcher.h"
#include "mutex_queue.h"
#include "path.h"
#include "sub_process.h"
#include "shell_command.h"
#include "work_queue.h"

#define CONFIG_RECENT_COUNT 5

enum FileBrowserCallback {
    FB_LOAD_CONFIG,
    FB_SAVE_CONFIG,
    FB_IMPORT_PRESET,
    FB_EXPORT_PRESET,
    FB_CONFIG_STR,
};

static Config                       current_config;
static UserConfig                   user_config;
static std::string                  last_loaded_config_name;

static std::unique_ptr<WorkQueue>   compile_queue;
static MutexQueue<char>             compile_output;
static std::atomic_bool             compiling;
static std::atomic_bool             stop_compiling;
static std::string                  compile_status;

static FileBrowserCallback                  fb_callback;
static std::unique_ptr<ImGui::FileBrowser>  fb;

static std::unique_ptr<FileWatcher> map_file_watcher;

static std::atomic_bool console_auto_scroll = true;
static std::atomic_bool console_lock_scroll = false;

static bool modified;
static bool modified_flags;
static bool show_preset_window;
static bool show_confirm_new_window;
static int preset_to_export;

static int window_width = WINDOW_WIDTH;
static int window_height = WINDOW_HEIGHT;
static bool app_running;

static std::vector<ToolPreset> builtin_presets = {
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

static std::vector<std::string> config_paths_title = {
    "Select Tools Dir",
    "Select Work Dir",
    "Select Output Dir",
    "Select Engine Exe",
    "Select Map Source"
};

static void ExecuteCompileProcess(const std::string& cmd, const std::string& pwd);


/*
=================
Async jobs
=================
*/

struct CompileJob
{
    Config config;
    bool run_quake;

    bool RunTool(const std::string& exe, const std::string& args)
    {
        std::string cmd = PathJoin(PathFromNative(config.config_paths[PATH_TOOLS_DIR]), exe);
        if (!PathExists(cmd)) {
            PrintError(exe.c_str());
            PrintError(" not found, is the tools directory right?");
            return false;
        }

        compile_status = exe + " " + args;

        cmd.append(" ");
        cmd.append(args);
        ExecuteCompileProcess(cmd, "");
        return true;
    }

    void operator()()
    {
        compiling = true;
        compile_status = "Copying source file to work dir...";
        ScopeGuard end{ []() {
            compiling = false;
            stop_compiling = false;
            compile_status = "Done.";
            console_lock_scroll = false;
        } };

        std::string source_map = PathFromNative(config.config_paths[PATH_MAP_SOURCE]);
        std::string work_map = PathJoin(PathFromNative(config.config_paths[PATH_WORK_DIR]), PathFilename(source_map));
        std::string work_bsp = work_map;
        std::string work_lit = work_map;
        bool copy_lit = false;

        StrReplace(work_bsp, ".map", ".bsp");
        StrReplace(work_lit, ".map", ".lit");

        if (!Copy(source_map, work_map))
            return;

        if (stop_compiling) return;

        if (config.tool_flags & CONFIG_FLAG_QBSP_ENABLED) {
            std::string args = config.qbsp_args + " " + work_map;
            if (!RunTool("qbsp.exe", args))
                return;
        }
        if (stop_compiling) return;

        if (config.tool_flags & CONFIG_FLAG_LIGHT_ENABLED) {
            copy_lit = true;
            std::string args = config.light_args + " " + work_bsp;
            if (!RunTool("light.exe", args))
                return;
        }
        if (stop_compiling) return;

        if (config.tool_flags & CONFIG_FLAG_VIS_ENABLED) {
            std::string args = config.vis_args + " " + work_bsp;
            if (!RunTool("vis.exe", args))
                return;
        }
        if (stop_compiling) return;

        std::string out_bsp = PathJoin(PathFromNative(config.config_paths[PATH_OUTPUT_DIR]), PathFilename(work_bsp));
        std::string out_lit = PathJoin(PathFromNative(config.config_paths[PATH_OUTPUT_DIR]), PathFilename(work_lit));

        if (!Copy(work_bsp, out_bsp)) return;
        if (copy_lit && !Copy(work_lit, out_lit)) return;

        if (run_quake) {
            compile_status = "Running quake...";

            std::string args = config.quake_args;
            if (args.find("+map") == std::string::npos) {
                std::string map_arg = PathFilename(out_bsp);
                StrReplace(map_arg, ".bsp", "");

                args.append(" +map ");
                args.append(map_arg);
            }

            std::string cmd = PathFromNative(config.config_paths[PATH_ENGINE_EXE]);
            std::string pwd = PathDirectory(cmd);
            cmd.append(" ");
            cmd.append(args);
            ExecuteCompileProcess(cmd, pwd);
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
static void ReadToMutexQueue(T& obj, std::atomic_bool* stop, MutexQueue<char>& out)
{
    char c;
    while (obj.ReadChar(c)) {
        out.push(c);
        if (stop && *stop) return;
    }
}

static void ExecuteCompileProcess(const std::string& cmd, const std::string& pwd)
{
    SubProcess proc{ cmd, pwd };
    if (!proc.Good()) {
        PrintError(cmd.c_str());
        PrintError(": failed to open subprocess\n");
        return;
    }

    ReadToMutexQueue(proc, &stop_compiling, compile_output);
}

static void ExecuteShellCommand(const std::string& cmd, const std::string& pwd)
{
    ShellCommand proc{ cmd, pwd };
    ReadToMutexQueue(proc, nullptr, compile_output);
}

static void StartCompileJob(const Config& cfg, bool run_quake)
{
    console_auto_scroll = true;
    console_lock_scroll = true;
    ClearConsole();

    if (compiling) {
        stop_compiling = true;
    }

    compile_queue->AddWork(0, CompileJob{ cfg, run_quake });
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

static void HandleMapSourceChanged()
{
    map_file_watcher->SetPath(current_config.config_paths[PATH_MAP_SOURCE]);
}

static void SetConfigPath(ConfigPath path, const std::string& value)
{
    modified = true;
    current_config.config_paths[path] = value;
    if (path == PATH_MAP_SOURCE) {
        HandleMapSourceChanged();
    }
}

static void AddRecentConfig(const std::string& path)
{
    if (path.empty()) return;

    for (int i = 0; i < user_config.recent_configs.size(); i++) {
        if (path == user_config.recent_configs[i]) return;
    }

    if (user_config.recent_configs.size() >= CONFIG_RECENT_COUNT) {
        user_config.recent_configs.pop_front();
    }
    user_config.recent_configs.push_back(path);
}

static int FindPresetIndex(const std::string& name)
{
    for (std::size_t i = 0; i < user_config.tool_presets.size(); i++) {
        if (user_config.tool_presets[i].name == name) {
            return (int)i;
        }
    }
    return -1;
}

static bool ComparePresetToConfig(const ToolPreset& preset)
{
    if (preset.flags != current_config.tool_flags)
        return true;
    if (preset.qbsp_args != current_config.qbsp_args)
        return true;
    if (preset.light_args != current_config.light_args)
        return true;
    if (preset.vis_args != current_config.vis_args)
        return true;

    return false;
}

static void SaveConfig(const std::string& path)
{
    int pi = current_config.selected_preset_index - 1;
    if (pi >= 0) {
        current_config.selected_preset = user_config.tool_presets[pi].name;
    }
    else {
        current_config.selected_preset = "";
    }

    WriteConfig(current_config, path);
    user_config.loaded_config = path;
    last_loaded_config_name = current_config.config_name;
    WriteUserConfig(user_config);

    modified = false;
    console_auto_scroll = true;
    Print("Saved config: ");
    Print(path.c_str());
    Print("\n");
}

static bool LoadConfig(const std::string& path)
{
    if (!PathExists(path)) {
        console_auto_scroll = true;
        PrintError("File not found: ");
        PrintError(path.c_str());
        PrintError("\n");
        return false;
    }

    if (!IsExtension(path, ".cfg")) {
        console_auto_scroll = true;
        PrintError("Invalid config file: ");
        PrintError(path.c_str());
        PrintError("\n");
        return false;
    }

    current_config = ReadConfig(path);
    current_config.selected_preset_index = FindPresetIndex(current_config.selected_preset) + 1;
    if (current_config.selected_preset_index > 0) {
        if (ComparePresetToConfig(user_config.tool_presets[current_config.selected_preset_index - 1])) {
            modified_flags = true;
        }
    }

    user_config.loaded_config = path;
    last_loaded_config_name = current_config.config_name;
    WriteUserConfig(user_config);

    HandleMapSourceChanged();

    modified = false;
    console_auto_scroll = true;
    Print("Loaded config: ");
    Print(path.c_str());
    Print("\n");
    return true;
}

static void CopyPreset(const ToolPreset& preset)
{
    modified = ComparePresetToConfig(preset);
    current_config.qbsp_args = preset.qbsp_args;
    current_config.light_args = preset.light_args;
    current_config.vis_args = preset.vis_args;
    current_config.tool_flags = preset.flags;
}

static ToolPreset& GetPreset(int i)
{
    return user_config.tool_presets[i - 1];
}

static void AddBuiltinPresets()
{
    for (const auto& preset : builtin_presets) {
        if (FindPresetIndex(preset.name) == -1) {
            user_config.tool_presets.insert(user_config.tool_presets.begin(), preset);
        }
    }
}

static bool ShouldConfigPathBeDirectory(ConfigPath path)
{
    return (path == PATH_TOOLS_DIR) || (path == PATH_WORK_DIR) || (path == PATH_OUTPUT_DIR);
}

/*
===================
UI Event Handlers
===================
*/

static void HandleNewConfig()
{
    if (modified) {
        show_confirm_new_window = true;
        return;
    }

    AddRecentConfig(user_config.loaded_config);

    current_config = {};
    user_config.loaded_config = "";
    WriteUserConfig(user_config);
}

static void HandleLoadConfig()
{
    fb_callback = FB_LOAD_CONFIG;
    fb->SetTitle("Load Config");
    fb->SetFlags(0);
    fb->SetPwd(PathDirectory(user_config.loaded_config));
    fb->Open();
}

static void HandleLoadRecentConfig(int i)
{
    const std::string& path = user_config.recent_configs[i];
    if (!LoadConfig(path)) {
        user_config.recent_configs.erase(user_config.recent_configs.begin() + i);
    }
}

static void HandleSaveConfigAs()
{
    fb_callback = FB_SAVE_CONFIG;
    fb->SetTitle("Save Config As...");
    fb->SetFlags(ImGuiFileBrowserFlags_EnterNewFilename);
    fb->SetPwd(PathDirectory(user_config.loaded_config));
    fb->Open();
}

static void HandleSaveConfig()
{
    if (user_config.loaded_config.empty() || current_config.config_name != last_loaded_config_name) {
        HandleSaveConfigAs();
        return;
    }

    SaveConfig(user_config.loaded_config);
}

static void HandleCompileAndRun()
{
    StartCompileJob(current_config, true);
}

static void HandleCompileOnly()
{
    StartCompileJob(current_config, false);
}

static void HandleStopCompiling()
{
    if (compiling) {
        stop_compiling = true;
    }
}

static void HandleRun()
{
    Config job_config = current_config;
    job_config.tool_flags = 0;
    StartCompileJob(job_config, true);
}

static void HandleToolHelp(int tool_flag)
{
    console_auto_scroll = false;
    console_lock_scroll = false;
    ClearConsole();
    compile_queue->AddWork(0, HelpJob{ current_config.config_paths[PATH_TOOLS_DIR], tool_flag });
}

static void HandlePathSelect(ConfigPath path)
{
    fb_callback = (FileBrowserCallback)(FB_CONFIG_STR + path);
    bool should_be_dir = ShouldConfigPathBeDirectory(path);
    std::string pwd = PathFromNative(current_config.config_paths[path]);

    if (should_be_dir && PathIsDirectory(pwd))
        fb->SetPwd(pwd);
    else
        fb->SetPwd(PathDirectory(pwd));

    if (should_be_dir)
        fb->SetFlags(ImGuiFileBrowserFlags_SelectDirectory | ImGuiFileBrowserFlags_CreateNewDir);
    else
        fb->SetFlags(0);

    fb->SetTitle(config_paths_title[path]);
    fb->Open();
}

static void HandleFileBrowserCallback()
{
    switch (fb_callback) {
    case FB_LOAD_CONFIG: {
        std::string path = PathFromNative(fb->GetSelected().string());
        if (!IsExtension(path, ".cfg")) {
            console_auto_scroll = true;
            PrintError("Invalid config file: ");
            PrintError(path.c_str());
            PrintError("\n");
            return;
        }

        if (PathExists(path)) {
            if (path != user_config.loaded_config) {
                AddRecentConfig(user_config.loaded_config);
            }
            LoadConfig(path);
        }
    } break;

    case FB_SAVE_CONFIG: {
        std::string path = PathFromNative(fb->GetSelected().string());
        AddExtensionIfNone(path, ".cfg");

        if (path != user_config.loaded_config) {
            AddRecentConfig(user_config.loaded_config);
        }
        SaveConfig(path);
    } break;

    case FB_IMPORT_PRESET: {
        std::string path = PathFromNative(fb->GetSelected().string());
        if (!IsExtension(path, ".pre")) {
            console_auto_scroll = true;
            PrintError("Invalid preset file: ");
            PrintError(path.c_str());
            PrintError("\n");
            return;
        }

        if (PathExists(path)) {
            ToolPreset preset = ReadToolPreset(path);
            if (preset.name.empty()) {
                console_auto_scroll = true;
                PrintError("Invalid preset file: ");
                PrintError(path.c_str());
                PrintError("\n");
                return;
            }

            while (StrReplace(preset.name, "(built-in)", "")) {}

            user_config.last_import_preset_location = PathDirectory(path);
            user_config.tool_presets.push_back(preset);
            WriteUserConfig(user_config);

            show_preset_window = true;

            console_auto_scroll = true;
            Print("Preset imported: ");
            Print(path.c_str());
            Print("\n");
        }
    } break;

    case FB_EXPORT_PRESET: {
        std::string path = PathFromNative(fb->GetSelected().string());
        AddExtensionIfNone(path, ".pre");

        auto exp_preset = user_config.tool_presets[preset_to_export];
        while (StrReplace(exp_preset.name, "(built-in)", "")) {}
        WriteToolPreset(exp_preset, path);
        
        user_config.last_export_preset_location = PathDirectory(path);
        WriteUserConfig(user_config);

        show_preset_window = true;

        console_auto_scroll = true;
        Print("Preset exported: ");
        Print(path.c_str());
        Print("\n");
    } break;

    default:
        if (fb_callback >= FB_CONFIG_STR)
            SetConfigPath(
                (ConfigPath)(fb_callback - FB_CONFIG_STR),
                PathFromNative(fb->GetSelected().string())
            );
        break;
    }
}

static void HandleFileBrowserCancel()
{
    switch (fb_callback) {
    case FB_IMPORT_PRESET:
    case FB_EXPORT_PRESET:
        show_preset_window = true;
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

    current_config.selected_preset_index = i;
    CopyPreset(user_config.tool_presets[i - 1]);

    modified_flags = false;
}

static void HandleImportPreset()
{
    fb_callback = FB_IMPORT_PRESET;
    if (!user_config.last_import_preset_location.empty()) {
        fb->SetPwd(PathFromNative(user_config.last_import_preset_location));
    }
    else {
        fb->SetPwd();
    }
    fb->SetTitle("Import Tool Preset");
    fb->SetFlags(0);
    fb->Open();
}

static void HandleExportPreset(int real_index)
{
    preset_to_export = real_index;
    fb_callback = FB_EXPORT_PRESET;
    if (!user_config.last_export_preset_location.empty()) {
        fb->SetPwd(PathFromNative(user_config.last_export_preset_location));
    }
    else {
        fb->SetPwd();
    }
    fb->SetTitle("Export Tool Preset");
    fb->SetFlags(ImGuiFileBrowserFlags_EnterNewFilename);
    fb->Open();
}

static void PrintReadme()
{
    static std::string readme;

    if (readme.empty()) {
        if (!ReadFileText("q1compile_readme.txt", readme)) return;
    }

    console_auto_scroll = false;

    ClearConsole();
    Print(readme.c_str());
}

/*
====================
UI Drawing
====================
*/

static void DrawRecentConfigs()
{
    int size = user_config.recent_configs.size();
    for (int i = size - 1; i >= 0; i--) {
        std::string filename = PathFilename(user_config.recent_configs[i]);
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

            if (ImGui::MenuItem("Save Config", "Ctrl+S", nullptr)) {
                HandleSaveConfig();
            }

            if (ImGui::MenuItem("Save Config As...", "Ctrl+Shift+S", nullptr)) {
                HandleSaveConfigAs();
            }

            if (user_config.recent_configs.size() > 0) {
                ImGui::Separator();
                DrawRecentConfigs();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Quit", "Ctrl+Q", nullptr)) {
                app_running = false;
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

            if (ImGui::MenuItem("Manage presets...", "Ctrl+P", nullptr)) {
                show_preset_window = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About", "", nullptr)) {
                PrintReadme();
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

static bool DrawTextInput(const char* label, std::string& str, float spacing, ImGuiInputTextFlags flags = 0)
{
    ImGui::Text(label);
    ImGui::SameLine();
    DrawSpacing(spacing, 0);
    ImGui::SameLine();

    ImGui::PushID(label);
    bool changed = ImGui::InputTextWithHint("", label, &str, flags);
    ImGui::PopID();
    return changed;
}

static bool DrawFileInput(const char* label, ConfigPath path, float spacing)
{
    bool changed = DrawTextInput(label, current_config.config_paths[path], spacing);
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
    ImGui::PopID();

    return changed;
}

static void DrawPresetCombo()
{
    ImGui::Text("Preset: "); ImGui::SameLine();

    DrawSpacing(40, 0); ImGui::SameLine();

    std::string selected_preset_name = "None";
    if (current_config.selected_preset_index > 0) {
        selected_preset_name = GetPreset(current_config.selected_preset_index).name;

        if (modified_flags) {
            selected_preset_name.append(" (modified)");
        }
    }

    if (ImGui::BeginCombo("##presets", selected_preset_name.c_str())) {
        if (ImGui::Selectable("None", false)) {
            current_config.selected_preset_index = 0;
            CopyPreset({});
        }

        for (std::size_t i = 0; i < user_config.tool_presets.size(); i++) {
            auto& preset = user_config.tool_presets[i];
            bool selected = (i + 1) == current_config.selected_preset_index;

            if (ImGui::Selectable(preset.name.c_str(), selected)) {
                HandleSelectPreset(i + 1);
            }
        }

        DrawSeparator(2);

        if (ImGui::Selectable("Manage presets...", false)) {
            show_preset_window = true;
        }
        ImGui::EndCombo();
    }
}

static bool DrawToolParams(const char* label, int tool_flag, std::string& args, float spacing)
{
    bool changed = false;
    ImGui::PushID(tool_flag);

    if (tool_flag) {
        bool checked = current_config.tool_flags & tool_flag;
        if (ImGui::Checkbox(label, &checked)) {
            changed = true;

            if (checked) {
                current_config.tool_flags |= tool_flag;
            }
            else {
                current_config.tool_flags = current_config.tool_flags & ~tool_flag;
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

    if (ImGui::InputTextWithHint("", "command-line arguments", &args, flags)) {
        changed = true;
    }

    ImGui::PopID();

    return changed;
}

static void DrawConsoleView()
{
    static int num_entries;

    float height = (float)window_height - ImGui::GetCursorPosY() - 70.0f;
    height = std::fmaxf(330.0f, height);

    if (ImGui::BeginChild("ConsoleView", ImVec2{ (float)window_width - 28, height }, true)) {
    
        auto& ents = GetLogEntries();
        for (const auto& ent : ents) {
            ImVec4 col = ImVec4{ 0.9f, 0.9f, 0.9f, 1.0 };
            if (ent.level == LOG_ERROR) {
                col = ImVec4{ 1.0f, 0.0f, 0.0f, 1.0 };
            }
            ImGui::TextColored(col, ent.text.c_str());
        }

        bool should_scroll_down = (
            (num_entries != ents.size() && console_auto_scroll) || console_lock_scroll
        );
        if (should_scroll_down) {
            ImGui::SetScrollHereY();
        }

        num_entries = ents.size();
    }

    ImGui::EndChild();
}

static void DrawPresetWindow()
{
    static bool prev_show;

    if (show_preset_window) {
        if (!prev_show) {
            ImGui::OpenPopup("Manage Tools Presets");
        }

        ImGui::SetNextWindowSize({ window_width*0.5f, window_height * 0.7f }, ImGuiCond_Always);
        ImGui::SetNextWindowPos({ window_width * 0.5f, window_height * 0.5f }, ImGuiCond_Always, { 0.5f, 0.5f });

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (ImGui::BeginPopupModal("Manage Tools Presets", &show_preset_window, flags)) {
            static int newly_added_index = -1;
            int remove_index = -1;
            int dup_index = -1;

            if (ImGui::Button("Add Preset")) {
                newly_added_index = user_config.tool_presets.size();

                ToolPreset preset = {};
                preset.name = "New Preset";
                user_config.tool_presets.push_back(preset);
            }
            ImGui::SameLine();
            if (ImGui::Button("Import Preset")) {
                ImGui::CloseCurrentPopup();
                show_preset_window = false;
                HandleImportPreset();
            }

            DrawSpacing(0, 5);

            if (ImGui::BeginChild("Preset list")) {
                for (int i = 0; i < (int)user_config.tool_presets.size(); i++) {
                    auto& preset = user_config.tool_presets[i];

                    ImGui::PushID(i);

                    // If it's a newly added preset, set it open
                    if (newly_added_index != -1 && (i == newly_added_index)) {
                        ImGui::SetNextTreeNodeOpen(true);
                        newly_added_index = -1;
                    }

                    if (ImGui::TreeNode("", preset.name.c_str())) {
                        DrawSpacing(0, 5);

                        ImGuiInputTextFlags flags = preset.builtin ? ImGuiInputTextFlags_ReadOnly : 0;

                        DrawTextInput("Name: ", preset.name, 23, flags);

                        DrawPresetToolParams(preset, "QBSP", CONFIG_FLAG_QBSP_ENABLED, preset.qbsp_args, 14, flags);
                        DrawPresetToolParams(preset, "LIGHT", CONFIG_FLAG_LIGHT_ENABLED, preset.light_args, 7, flags);
                        DrawPresetToolParams(preset, "VIS", CONFIG_FLAG_VIS_ENABLED, preset.vis_args, 21, flags);
                        //DrawPresetToolParams(preset, "QUAKE", 0, preset.quake_args, 8);

                        ImGui::Text("Action: "); ImGui::SameLine();  DrawSpacing(10, 0); ImGui::SameLine();
                        if (ImGui::BeginCombo("##actions", "Actions...")) {
                            if (ImGui::Selectable("Copy")) {
                                dup_index = i;
                            }
                            if (ImGui::Selectable("Select")) {
                                HandleSelectPreset(i + 1);
                            }
                            if (ImGui::Selectable("Export")) {
                                ImGui::CloseCurrentPopup();
                                show_preset_window = false;
                                HandleExportPreset(i);
                            }
                            if (!preset.builtin) {
                                if (ImGui::Selectable("Remove")) {
                                    remove_index = i;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::TreePop();
                    }
                    ImGui::PopID();

                    DrawSeparator(5);
                }
            }
            ImGui::EndChild();

            if (remove_index != -1) {
                if (remove_index + 1 == current_config.selected_preset_index) {
                    current_config.selected_preset_index = 0;
                }

                user_config.tool_presets.erase(user_config.tool_presets.begin() + remove_index);
            }
            if (dup_index != -1) {
                auto dup_preset = user_config.tool_presets[dup_index];
                dup_preset.builtin = false;
                while (StrReplace(dup_preset.name, "(built-in)", "")) {}
                dup_preset.name.append(" (copy)");

                auto it = user_config.tool_presets.begin() + dup_index + 1;
                user_config.tool_presets.insert(it, dup_preset);

                newly_added_index = dup_index + 1;
            }

            ImGui::EndPopup();
        }

        // Just closed
        if (!show_preset_window) {
            WriteUserConfig(user_config);
        }
    }

    prev_show = show_preset_window;
}

static void DrawConfirmNewWindow()
{
    static bool prev_show;

    if (show_confirm_new_window) {
        if (!prev_show) {
            ImGui::OpenPopup("Save Changes");
        }

        bool clicked_btn = false;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (ImGui::BeginPopupModal("Save Changes", &show_confirm_new_window, flags)) {
            ImGui::Text("You have unsaved changes to your config, do you want to save?");
            DrawSeparator(5);

            if (ImGui::Button("Save")) {
                ImGui::CloseCurrentPopup();
                clicked_btn = true;
                show_confirm_new_window = false;
                HandleSaveConfig();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard")) {
                ImGui::CloseCurrentPopup();
                clicked_btn = true;
                show_confirm_new_window = false;
                modified = false;
                HandleNewConfig();
            }
            ImGui::EndPopup();
        }

        // Just closed
        if (!show_confirm_new_window && !clicked_btn) {
            modified = false;
            HandleNewConfig();
        }
    }

    prev_show = show_confirm_new_window;
}

static void DrawCredits()
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.5f, 0.5f, 0.5f, 1.0f });
    DrawSpacing(0, 12);
    ImGui::Text("Quake 1 Compile GUI (%s)", APP_VERSION);
    ImGui::PopStyleColor();
}

static void DrawMainContent()
{
    if (DrawTextInput("Config Name: ", current_config.config_name, 5)) modified = true;

    DrawSeparator(5);

    if (DrawFileInput("Tools Dir: ", PATH_TOOLS_DIR, 20)) modified = true;
    if (DrawFileInput("Work Dir: ", PATH_WORK_DIR, 27)) modified = true;
    if (DrawFileInput("Output Dir: ", PATH_OUTPUT_DIR, 13.5f)) modified = true;
    if (DrawFileInput("Engine Exe: ", PATH_ENGINE_EXE, 13.5f)) modified = true;
    if (DrawFileInput("Map Source: ", PATH_MAP_SOURCE, 13.5f)) modified = true;

    DrawSeparator(5);

    if (ImGui::Checkbox("Watch map file for changes and pre-compile", &current_config.watch_map_file)) {
        modified = true;
        map_file_watcher->SetEnabled(current_config.watch_map_file);
    }

    DrawSeparator(5);

    DrawPresetCombo();

    if (DrawToolParams("QBSP", CONFIG_FLAG_QBSP_ENABLED, current_config.qbsp_args, 45)) {
        modified = true;
        modified_flags = true;
    }
    if (DrawToolParams("LIGHT", CONFIG_FLAG_LIGHT_ENABLED, current_config.light_args, 38)) {
        modified = true;
        modified_flags = true;
    }
    if (DrawToolParams("VIS", CONFIG_FLAG_VIS_ENABLED, current_config.vis_args, 52)) {
        modified = true;
        modified_flags = true;
    }
    if (DrawToolParams("QUAKE", 0, current_config.quake_args, 38)) {
        modified = true;
    }

    DrawSeparator(5);

    ImGui::Text("Status: "); ImGui::SameLine();
    ImGui::InputText("##status", &compile_status, ImGuiInputTextFlags_ReadOnly);

    DrawSpacing(0, 10);

    DrawConsoleView();

    DrawCredits();

    DrawPresetWindow();

    DrawConfirmNewWindow();

    fb->Display();
    if (fb->HasSelected()) {
        HandleFileBrowserCallback();
        fb->Close();
    }
    else if (fb->HasRequestedCancel()) {
        HandleFileBrowserCancel();
        fb->Close();
    }
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
    ImGui::SetNextWindowSize(ImVec2{ (float)window_width, (float)window_height }, ImGuiCond_Always);
    if (ImGui::Begin("##main", nullptr, flags)) {
        if (ImGui::BeginChild("##content", ImVec2{ (float)window_width - 28, (float)window_height - 28 } )) {
            DrawMainContent();
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleVar(1);
    
}


/*
====================
Application lifetime functions
====================
*/

void qc_init()
{    
    app_running = true;

    ClearConsole();
    SetErrorLogFile("q1compile_err.log");

    map_file_watcher = std::make_unique<FileWatcher>("", 1.0f);
    compile_queue = std::make_unique<WorkQueue>(std::size_t{ 1 }, std::size_t{ 128 });
    fb = std::make_unique<ImGui::FileBrowser>();
    compile_status = "Doing nothing.";

    user_config = ReadUserConfig();
    for (auto& preset : user_config.tool_presets) {
        while (StrReplace(preset.name, "(built-in)", "")) {}
    }
    AddBuiltinPresets();
    if (!user_config.loaded_config.empty()) {
        if (!LoadConfig(user_config.loaded_config)) {
            user_config.loaded_config = "";
        }
    }

    PrintReadme();
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
            show_preset_window = true;
        }
        break;
    case 'Q':
        if (io.KeyCtrl) {
            app_running = false;
        }
        break;
    }
}

void qc_resize(unsigned int width, unsigned int height)
{
    window_width = width;
    window_height = height - 18;
}

bool qc_render()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (map_file_watcher->Update(dt)) {
        HandleCompileOnly();
    }

    while (!compile_output.empty()) {
        char c = compile_output.pop();
        Print(c);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.9f, 0.9f, 0.9f, 1.0f });
    DrawMenuBar();
    DrawMainWindow();
    ImGui::PopStyleColor(1);

    return app_running;
}

void qc_deinit()
{
    WriteUserConfig(user_config);
}