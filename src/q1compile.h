#pragma once

#include "common.h"
#include "console.h"
#include "config.h"
#include "file_watcher.h"
#include "map_file.h"
#include "mutex_char_buffer.h"
#include "path.h"
#include "work_queue.h"

#define CONFIG_RECENT_COUNT 20

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

struct OpenConfigState {
    config::Config config;

    bool modified = false;
    bool modified_steps = false;

    std::string path;
    std::string last_loaded_config_name;

    int selected_preset_index;

    std::unique_ptr<map_file::MapFile>              map_file;
    std::unique_ptr<file_watcher::FileWatcher>      map_file_watcher;
    std::atomic_bool                                map_has_leak = false;
};

struct AppState {
    std::vector<std::unique_ptr<OpenConfigState>>   open_configs;
    OpenConfigState*                                current_config;
    int                                             current_config_index;
    config::UserConfig                              user_config;
    std::string                                     last_loaded_config_name;

    std::unique_ptr<work_queue::WorkQueue>          compile_queue;
    mutex_char_buffer::MutexCharBuffer              compile_output;
    std::atomic_bool                                compiling;
    std::atomic_bool                                stop_compiling;
    std::string                                     compile_status;
    bool                                            last_job_ran_quake;

    FileBrowserCallback                             fb_callback;
    std::string                                     fb_path;

    std::atomic_bool             console_auto_scroll = true;
    std::atomic_bool             console_lock_scroll = false;

    UnsavedChangesCallback       unsaved_changes_callback;

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

void qc_init(void* platform_data);

void qc_key_down(unsigned int key);

void qc_resize(unsigned int width, unsigned int height);

bool qc_render();

void qc_deinit();
