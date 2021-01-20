#pragma once

#include <string>

namespace file_watcher {

struct FileWatcher
{
    explicit FileWatcher(const std::string& path, float interval);

    void SetPath(const std::string& path);

    void SetEnabled(bool enabled);

    bool Update(float elapsed_time);

    std::string _path;
    unsigned long long _prev_modified_time;
    float _interval;
    float _timer;
    bool _enabled;
};

}