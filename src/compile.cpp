#include "common.h"
#include "compile.h"
#include "config.h"
#include "console.h"
#include "map_file.h"
#include "path.h"
#include "q1compile.h"
#include "shell_command.h"
#include "sub_process.h"

extern AppState* g_app;

namespace compile {

/*
=================
Async jobs
=================
*/

static void ExecuteCompileCommand(const std::string& cmd, const std::string& pwd, bool suppress_output = false);

static void ExecuteCompileProcess(const std::string& cmd, const std::string& pwd, bool suppress_output = false);

static void HandleFileBrowserCallback();

static void ReportCopy(const std::string& from_path, const std::string& to_path);

static config::ToolPreset GetMapDiffArgs(const map_file::MapFile& map_a, const map_file::MapFile& map_b);

static std::string ReplaceCompileVars(const std::string& args, const config::Config& cfg);

struct CompileJob
{
    config::Config config;
    bool run_quake;
    bool ignore_diff;

    bool RunTool(const std::string& exe, const std::string& args)
    {
        std::string cmd = path::Join(path::FromNative(config.config_paths[config::PATH_TOOLS_DIR]), exe);
        if (!path::Exists(cmd)) {
            console::PrintError(exe.c_str());
            console::PrintError(" not found, is the tools directory right?");
            return false;
        }

        g_app->compile_status = exe + " " + args;

        cmd.append(" ");
        cmd.append(args);
        ExecuteCompileProcess(cmd, "");
        return true;
    }

    void operator()()
    {
        g_app->compile_status = "Preparing to compile...";

        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto time_begin = std::chrono::system_clock::now();

        std::string source_map = path::FromNative(config.config_paths[config::PATH_MAP_SOURCE]);
        std::string work_map = path::Join(path::FromNative(config.config_paths[config::PATH_WORK_DIR]), path::Filename(source_map));
        if (source_map == work_map) {
            g_app->compile_status = "Stopped.";
            console::PrintError("ERROR: 'Work Dir' is the same as the map source directory, there's a risk of messing with your files, compilation will not proceed!\n");
            console::Print("Please set the 'Work Dir' to somewhere different.\n");
            console::Print("If you don't care about this setting, use the menu 'Compile -> Reset Work Dir' or press 'Ctrl + Shift + W'.\n");
            return;
        }

        std::string work_bsp = work_map;
        std::string work_lit = work_map;
        bool copy_bsp = false;
        bool copy_lit = false;
        common::StrReplace(work_bsp, ".map", ".bsp");
        common::StrReplace(work_lit, ".map", ".lit");

        std::string out_bsp = path::Join(path::FromNative(config.config_paths[config::PATH_OUTPUT_DIR]), path::Filename(work_bsp));
        std::string out_lit = path::Join(path::FromNative(config.config_paths[config::PATH_OUTPUT_DIR]), path::Filename(work_lit));
        if (work_bsp == out_bsp) {
            g_app->compile_status = "Stopped.";
            console::PrintError("ERROR: 'Work Dir' is the same as the 'Output Dir', there's a risk of messing with your files, compilation will not proceed!\n");
            console::Print("Please set the 'Work Dir' to somewhere different.\n");
            console::Print("If you don't care about this setting, use the menu 'Compile -> Reset Work Dir' or press 'Ctrl + Shift + W'.\n");
            return;
        }

        auto prev_map_file = std::unique_ptr<map_file::MapFile>(g_app->map_file.release());
        g_app->map_file = std::make_unique<map_file::MapFile>(source_map);
        
        g_app->map_has_leak = false;
        g_app->compiling = true;
        g_app->compile_status = "Copying source file to work dir...";
        common::ScopeGuard end{ [work_bsp, source_map]() {
            g_app->compiling = false;
            g_app->stop_compiling = false;
            g_app->console_lock_scroll = false;

            {
                // copy .pts file if generated
                std::string work_pts = work_bsp;
                while (common::StrReplace(work_pts, ".bsp", ".pts")) {}

                if (path::Exists(work_pts)) {
                    std::string source_pts = source_map;
                    while (common::StrReplace(source_pts, ".map", ".pts")) {}
                    if (path::Copy(work_pts, source_pts)) {
                        ReportCopy(work_pts, source_pts);
                        path::Remove(work_pts);
                        g_app->map_has_leak = true;
                    }
                }
            }

            if (g_app->compile_status.find("Finished") == std::string::npos) {
                g_app->compile_status = "Stopped.";
            }
        } };

        auto ReportCompileParams = [this](const std::string& name) {
            /*
            g_app->compile_output.append(name);
            if (config.tool_flags & CONFIG_FLAG_QBSP_ENABLED)
                g_app->compile_output.append(" qbsp=true");
            else
                g_app->compile_output.append(" qbsp=false");

            if (config.tool_flags & CONFIG_FLAG_LIGHT_ENABLED)
                g_app->compile_output.append(" light=true");
            else
                g_app->compile_output.append(" light=false");

            if (config.tool_flags & CONFIG_FLAG_VIS_ENABLED)
                g_app->compile_output.append(" vis=true");
            else
                g_app->compile_output.append(" vis=false");

            g_app->compile_output.append("\n");
            */
        };
        ReportCompileParams("Starting to compile:");

        if (!g_app->map_file->Good()) {
            g_app->compile_output.append("Could not read map file!\n");
        }
        else {
            if (prev_map_file && g_app->current_config.watch_map_file && g_app->current_config.auto_apply_onlyents && !this->ignore_diff) {
                g_app->compile_output.append("Doing map diff...\n");

                config::ToolPreset diff_pre = GetMapDiffArgs(*prev_map_file, *g_app->map_file);

                std::vector<config::CompileStep> new_steps;
                for (const auto& step : diff_pre.steps) {
                    auto orig_step = config::FindCompileStep(config.steps, step.type);
                    if (orig_step) {
                        new_steps.push_back(config::CompileStep{
                            step.type,
                            orig_step->cmd,
                            step.args + " " + orig_step->args,
                            step.enabled,
                            step.flags
                        });
                    }
                    else {
                        new_steps.push_back(step);
                    }
                }
                for (size_t i = 0; i < config.steps.size(); i++) {
                    const auto& step = config.steps[i];
                    if (step.type == config::COMPILE_CUSTOM) {
                        new_steps.insert(new_steps.begin() + i, step);
                    }
                }
                config.steps = std::move(new_steps);

                ReportCompileParams("Map diff results:");

                auto qbsp_step = config::FindCompileStep(config.steps, config::COMPILE_QBSP);
                auto light_step = config::FindCompileStep(config.steps, config::COMPILE_LIGHT);

                if (qbsp_step) g_app->compile_output.append("Diff QBSP args: " + qbsp_step->args + "\n");
                if (light_step) g_app->compile_output.append("Diff LIGHT args: " + light_step->args + "\n");
            }
        }

        if (path::Copy(source_map, work_map)) {
            ReportCopy(source_map, work_map);
        }
        else {
            return;
        }

        if (g_app->stop_compiling) return;

        // Execute compile steps
        for (const auto& step : config.steps) {
            if (g_app->stop_compiling) return;
            if (!step.enabled) continue;

            if (step.type == config::COMPILE_CUSTOM) {
                auto cmd = ReplaceCompileVars(step.cmd, config);
                g_app->compile_output.append("Starting: " + cmd + "\n");
                g_app->compile_status = cmd;
                ExecuteCompileCommand(cmd, "");
                g_app->compile_output.append("Finished: " + cmd + "\n");
            }
            else {
                // Define step args
                std::string args = ReplaceCompileVars(step.args, config);
                switch (step.type) {
                case config::COMPILE_QBSP:
                    args += " " + work_map;
                    copy_bsp = true;
                    break;

                case config::COMPILE_LIGHT:
                    args += " " + work_bsp;
                    copy_lit = true;
                    break;

                case config::COMPILE_VIS:
                    args += " " + work_bsp;
                    break;
                }

                g_app->compile_output.append("Starting: " + step.cmd + " " + args + "\n");
                if (!RunTool(step.cmd, args)) return;
                g_app->compile_output.append("Finished: " + step.cmd + " " + args + "\n");
                g_app->compile_output.append("------------------------------------------------\n");
            }
        }

        if (copy_bsp && path::Exists(work_bsp)) {
            if (path::Copy(work_bsp, out_bsp)) {
                ReportCopy(work_bsp, out_bsp);
            }
            else {
                return;
            }
        }
        
        if (copy_lit && path::Exists(work_lit)) {
            if (path::Copy(work_lit, out_lit)) {
                ReportCopy(work_lit, out_lit);
            }
        }

        if (copy_lit || copy_bsp) g_app->compile_output.append("------------------------------------------------\n");

        auto time_end = std::chrono::system_clock::now();
        auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_begin);
        float secs = time_elapsed.count() / 1000.0f;
        g_app->compile_status = "Finished in " + std::to_string(secs) + " seconds.";
        g_app->compile_output.append(g_app->compile_status);
        g_app->compile_output.append("\n\n");

        if (run_quake) {
            g_app->compile_output.append("------------------------------------------------\n");

            std::string args = config.quake_args;

            if (g_app->current_config.use_map_mod && g_app->map_file.get() && (args.find("-game") == std::string::npos)) {
                args.append(" -game ");
                args.append(g_app->map_file->GetTBMod());
            }

            if (args.find("+map") == std::string::npos) {
                std::string map_arg = path::Filename(out_bsp);
                common::StrReplace(map_arg, ".bsp", "");

                args.append(" +map ");
                args.append(map_arg);
            }

            g_app->compile_status = "Running quake with command-line: " + args;

            g_app->compile_output.append(g_app->compile_status);
            g_app->compile_output.append("\n");

            std::string cmd = path::FromNative(config.config_paths[config::PATH_ENGINE_EXE]);
            std::string pwd = path::Directory(cmd);
            cmd.append(" ");
            cmd.append(args);

            ExecuteCompileProcess(cmd, pwd, !config.quake_output_enabled);
        }
    }
};

struct HelpJob
{
    config::Config cfg;
    config::CompileStepType cstype;

    void operator()()
    {
        std::string cmd;
        switch (cstype) {
        case config::COMPILE_QBSP:
        case config::COMPILE_LIGHT:
        case config::COMPILE_VIS:
            cmd = config::FindCompileStep(cfg.steps, cstype)->cmd;
            break;
        default:
            return;
        }

        std::string proc = path::Join(path::FromNative(cfg.config_paths[config::PATH_TOOLS_DIR]), cmd);
        if (!path::Exists(proc)) {
            console::PrintError(cmd.c_str());
            console::PrintError(" not found, is the tools directory right?\n");
            return;
        }

        ExecuteCompileProcess(proc, "");
    }
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

static void ExecuteCompileCommand(const std::string& cmd, const std::string& pwd, bool suppress_output)
{
    shell_command::ShellCommand proc{ cmd, pwd };
    if (!proc.Good()) {
        console::PrintError(cmd.c_str());
        console::PrintError(": failed to execute command\n");
        return;
    }

    auto output = &g_app->compile_output;
    if (suppress_output) {
        output = nullptr;
    }

    console::SetPrintToFile(false);
    ReadToMutexCharBuffer(proc, &g_app->stop_compiling, output);
    console::SetPrintToFile(true);
}

static void ExecuteCompileProcess(const std::string& cmd, const std::string& pwd, bool suppress_output)
{
    sub_process::SubProcess proc{ cmd, pwd };
    if (!proc.Good()) {
        console::PrintError(cmd.c_str());
        console::PrintError(": failed to open subprocess\n");
        return;
    }

    auto output = &g_app->compile_output;
    if (suppress_output) {
        output = nullptr;
    }

    console::SetPrintToFile(false);
    ReadToMutexCharBuffer(proc, &g_app->stop_compiling, output);
    console::SetPrintToFile(true);
}

static void ReportCopy(const std::string& from_path, const std::string& to_path)
{
    g_app->compile_output.append("Copied ");
    g_app->compile_output.append(from_path);
    g_app->compile_output.append(" to ");
    g_app->compile_output.append(to_path);
    g_app->compile_output.append("\n");
}

static config::ToolPreset GetMapDiffArgs(const map_file::MapFile& map_a, const map_file::MapFile& map_b)
{
    config::ToolPreset pre = {};
    auto flags = map_file::GetDiffFlags(map_a, map_b);

    if (flags & map_file::MAP_DIFF_BRUSHES) {
        pre.steps.push_back(config::CompileStep{config::COMPILE_QBSP, "qbsp.exe", "", true, 0});
        pre.steps.push_back(config::CompileStep{config::COMPILE_LIGHT, "light.exe", "", true, 0});
        pre.steps.push_back(config::CompileStep{config::COMPILE_VIS, "vis.exe", "", true, 0});
    }
    else if (flags & map_file::MAP_DIFF_LIGHTS) {
        pre.steps.push_back(config::CompileStep{config::COMPILE_QBSP, "qbsp.exe", "-onlyents", true, 0});
        pre.steps.push_back(config::CompileStep{config::COMPILE_LIGHT, "light.exe", "", true, 0});
    }
    else if (flags & map_file::MAP_DIFF_ENTS) {
        pre.steps.push_back(config::CompileStep{config::COMPILE_QBSP, "qbsp.exe", "-onlyents", true, 0});
    }
    
    return pre;
}

static std::string ReplaceCompileVars(const std::string& args, const config::Config& cfg)
{
    return args;
}

void StartCompileJob(const config::Config& cfg, bool run_quake, bool ignore_diff)
{
    g_app->console_auto_scroll = true;
    g_app->console_lock_scroll = true;
    console::ClearConsole();

    if (g_app->compiling) {
        g_app->stop_compiling = true;
    }

    g_app->last_job_ran_quake = run_quake;
    EnqueueCompileJob(cfg, run_quake, ignore_diff);
}

void StartHelpJob(config::CompileStepType cstype)
{
    g_app->compile_queue->AddWork(0, HelpJob{ g_app->current_config, cstype });
}

void EnqueueCompileJob(const config::Config& cfg, bool run_quake, bool ignore_diff)
{
    g_app->compile_queue->AddWork(0, CompileJob{ cfg, run_quake, ignore_diff });
}

}