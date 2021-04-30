#include "file_watcher.h"
#include "path.h"
#include "console.h"

namespace file_watcher {

FileWatcher::FileWatcher(const std::string& path, float interval)
    : _path{ path }, _interval{ interval }, _enabled{ !path.empty() }, _timer{ interval }
{
    _prev_modified_time = path::GetFileModifiedTime(_path);
}

void FileWatcher::SetPath(const std::string& path)
{
    _path = path;
    _prev_modified_time = path::GetFileModifiedTime(_path);
}

void FileWatcher::SetEnabled(bool enabled)
{
    _enabled = enabled && !_path.empty();
    if (enabled) _prev_modified_time = path::GetFileModifiedTime(_path);
}

bool FileWatcher::Update(float elapsed_time)
{
    if (!_enabled) return false;

    bool changed = false;

    if (_timer <= 0.0f) {
        unsigned long long mod_time = path::GetFileModifiedTime(_path);
        if (mod_time != _prev_modified_time) {
            _prev_modified_time = mod_time;
            changed = true;
        }
        _timer = _interval;
    }

    _timer -= elapsed_time;
    return changed;
}

}