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

        if (config.flags & CONFIG_FLAG_QBSP_ENABLED) {
            std::string args = config.qbsp_args + " " + work_map;
            if (!RunTool("qbsp.exe", args))
                return;
        }
        if (stop_compiling) return;

        if (config.flags & CONFIG_FLAG_LIGHT_ENABLED) {
            copy_lit = true;
            std::string args = config.light_args + " " + work_bsp;
            if (!RunTool("light.exe", args))
                return;
        }
        if (stop_compiling) return;

        if (config.flags & CONFIG_FLAG_VIS_ENABLED) {
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

static void HandleMapSourceChanged()
{
    map_file_watcher->SetPath(current_config.config_paths[PATH_MAP_SOURCE]);
}

static void SetConfigPath(ConfigPath path, const std::string& value)
{
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

static void SaveConfig(const std::string& path)
{
    WriteConfig(current_config, path);
    user_config.loaded_config = path;
    last_loaded_config_name = current_config.config_name;
    WriteUserConfig(user_config);

    Print("Saved config: ");
    Print(path.c_str());
    Print("\n");
}

static void LoadConfig(const std::string& path)
{
    current_config = ReadConfig(path);
    user_config.loaded_config = path;
    last_loaded_config_name = current_config.config_name;
    WriteUserConfig(user_config);

    Print("Loaded config: ");
    Print(path.c_str());
    Print("\n");
}

/*
===================
UI Event Handlers
===================
*/

static void HandleNewConfig()
{
    AddRecentConfig(user_config.loaded_config);

    current_config = {};
    user_config.loaded_config = "";
}

static void HandleLoadConfig()
{
    fb_callback = FB_LOAD_CONFIG;
    fb->SetPwd(PathDirectory(user_config.loaded_config));
    fb->Open();
}

static void HandleLoadRecentConfig(int i)
{
    const std::string& path = user_config.recent_configs[i];
    if (PathExists(path)) {
        LoadConfig(path);
    }
}

static void HandleSaveConfigAs()
{
    fb_callback = FB_SAVE_CONFIG;
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

static void HandleRun()
{
    Config job_config = current_config;
    job_config.flags = 0;
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
    std::string pwd = PathFromNative(current_config.config_paths[path]);
    if (PathIsDirectory(pwd))
        fb->SetPwd(pwd);
    else
        fb->SetPwd(PathDirectory(pwd));

    fb->Open();
}

static void HandleFileBrowserCallback()
{
    switch (fb_callback) {
    case FB_LOAD_CONFIG: {
        std::string path = PathFromNative(fb->GetSelected().string());
        if (PathExists(path)) {
            if (path != user_config.loaded_config) {
                AddRecentConfig(user_config.loaded_config);
            }
            LoadConfig(path);
        }
    } break;

    case FB_SAVE_CONFIG: {
        std::string path = PathFromNative(fb->GetSelected().string());
        if (path != user_config.loaded_config) {
            AddRecentConfig(user_config.loaded_config);
        }
        SaveConfig(path);
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
                HandleSaveConfigAs();
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

            ImGui::Separator();

            if (ImGui::MenuItem("Run", "Ctrl+R", nullptr)) {
                HandleRun();
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

static bool DrawTextInput(const char* label, std::string& str, float spacing)
{
    ImGui::Text(label);
    ImGui::SameLine();
    DrawSpacing(spacing, 0);
    ImGui::SameLine();

    ImGui::PushID(label);
    bool changed = ImGui::InputTextWithHint("", label, &str);
    ImGui::PopID();
    return changed;
}

static void DrawFileInput(const char* label, ConfigPath path, float spacing)
{
    if (DrawTextInput(label, current_config.config_paths[path], spacing)) {
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
}

static void DrawToolParams(const char* label, int tool_flag, std::string& args, float spacing)
{
    ImGui::PushID(tool_flag);

    if (tool_flag) {
        bool checked = current_config.flags & tool_flag;
        if (ImGui::Checkbox(label, &checked)) {
            if (checked) {
                current_config.flags |= tool_flag;
            }
            else {
                current_config.flags = current_config.flags & ~tool_flag;
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

    ImGui::InputTextWithHint("", "command-line arguments", &args);

    if (tool_flag) {
        ImGui::SameLine();
        if (ImGui::Button("Help")) {
            HandleToolHelp(tool_flag);
        }
    }

    ImGui::PopID();
}

static void DrawConsoleView()
{
    static int num_entries;

    if (ImGui::BeginChild("ConsoleView", ImVec2{ 996, 310 }, true)) {
    
        auto& ents = GetLogEntries();
        for (const auto& ent : ents) {
            ImVec4 col = ImVec4{ 0.8f, 0.8f, 0.8f, 1.0 };
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

static void DrawMainContent()
{
    DrawTextInput("Config Name: ", current_config.config_name, 5);

    DrawSeparator(5);

    DrawFileInput("Tools Dir: ",  PATH_TOOLS_DIR,  20);
    DrawFileInput("Work Dir: ",   PATH_WORK_DIR,   27);
    DrawFileInput("Output Dir: ", PATH_OUTPUT_DIR, 13.5f);
    DrawFileInput("Engine Exe: ", PATH_ENGINE_EXE, 13.5f);
    DrawFileInput("Map Source: ", PATH_MAP_SOURCE, 13.5f);

    DrawSeparator(5);

    bool watch_map_file = current_config.flags & CONFIG_FLAG_WATCH_MAP_FILE;
    if (ImGui::Checkbox("Watch map file for changes and pre-compile", &watch_map_file)) {
        if (watch_map_file) {
            current_config.flags |= CONFIG_FLAG_WATCH_MAP_FILE;
        }
        else {
            current_config.flags = current_config.flags & ~CONFIG_FLAG_WATCH_MAP_FILE;
        }

        map_file_watcher->SetEnabled(watch_map_file);
    }

    DrawSeparator(5);

    DrawToolParams("QBSP",  CONFIG_FLAG_QBSP_ENABLED, current_config.qbsp_args, 45);
    DrawToolParams("LIGHT", CONFIG_FLAG_LIGHT_ENABLED, current_config.light_args, 38);
    DrawToolParams("VIS",   CONFIG_FLAG_VIS_ENABLED, current_config.vis_args, 52);
    DrawToolParams("QUAKE", 0, current_config.quake_args, 38);

    DrawSeparator(5);

    ImGui::Text("Status: "); ImGui::SameLine();
    ImGui::InputText("##status", &compile_status, ImGuiInputTextFlags_ReadOnly);

    DrawSpacing(0, 10);

    DrawConsoleView();

    fb->Display();
    if (fb->HasSelected()) {
        HandleFileBrowserCallback();
        fb->Close();
    }
    else if (fb->HasRequestedCancel()) {
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
    ImGui::SetNextWindowSize(ImVec2{ WINDOW_WIDTH, WINDOW_HEIGHT }, ImGuiCond_Always);
    if (ImGui::Begin("##main", nullptr, flags)) {
        if (ImGui::BeginChild("##content", ImVec2{ 996, 740 } )) {
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
    ClearConsole();

    map_file_watcher = std::make_unique<FileWatcher>("", 1.0f);
    compile_queue = std::make_unique<WorkQueue>(std::size_t{ 1 }, std::size_t{ 128 });
    fb = std::make_unique<ImGui::FileBrowser>();
    compile_status = "Doing nothing.";

    user_config = ReadUserConfig();
    if (!user_config.loaded_config.empty()) {
        current_config = ReadConfig(user_config.loaded_config);
        map_file_watcher->SetPath(current_config.config_paths[PATH_MAP_SOURCE]);
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
    }
}

void qc_render()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (map_file_watcher->Update(dt)) {
        HandleCompileOnly();
    }

    while (!compile_output.empty()) {
        char c = compile_output.pop();
        Print(c);
    }
    DrawMenuBar();
    DrawMainWindow();
}

void qc_deinit()
{
}