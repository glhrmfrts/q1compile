#include <shlobj.h>
#include <io.h>
#include <cassert>
#include <iostream>
#include <functional>
#include "common.h"
#include "path.h"

std::wstring Widen(const char* const text, const int size)
{
    if(!size) return {};
    /* WCtoMB counts the trailing \0 into size, which we have to cut */
    std::wstring result(MultiByteToWideChar(CP_UTF8, 0, text, size, nullptr, 0) - (size == -1 ? 1 : 0), 0);
    MultiByteToWideChar(CP_UTF8, 0, text, size, &result[0], result.size());
    return result;
}

std::string Narrow(const wchar_t* const text, const int size)
{
    if(!size) return {};
    /* WCtoMB counts the trailing \0 into size, which we have to cut */
    std::string result(WideCharToMultiByte(CP_UTF8, 0, text, size, nullptr, 0, nullptr, nullptr) - (size == -1 ? 1 : 0), 0);
    WideCharToMultiByte(CP_UTF8, 0, text, size, &result[0], result.size(), nullptr, nullptr);
    return result;
}

std::wstring Widen(const std::string& text)
{
    return Widen(text.data(), text.size());
}

std::string Narrow(const std::wstring& text)
{
    return Narrow(text.data(), text.size());
}

std::string PathFromNative(std::string path)
{
#ifdef _WIN32
    while (StrReplace(path, "\\", "/")) {}
#endif
    return path;
}

std::string PathToNative(std::string path)
{
#ifdef _WIN32
    StrReplace(path, "/", "\\");
#endif
    return path;
}

std::string PathDirectory(std::string filename)
{
    /* If filename is already a path, return it */
    if(!filename.empty() && filename.back() == '/')
        return filename.substr(0, filename.size()-1);

    std::size_t pos = filename.find_last_of('/');

    /* Filename doesn't contain any slash (no path), return empty string */
    if(pos == std::string::npos) return {};

    /* Return everything to last slash */
    return filename.substr(0, pos);
}

std::string PathFilename(std::string filename)
{
    std::size_t pos = filename.find_last_of('/');

    /* Return whole filename if it doesn't contain slash */
    if(pos == std::string::npos) return filename;

    /* Return everything after last slash */
    return filename.substr(pos+1);
}

bool SplitExtension(const std::string& filename, std::string& root, std::string& ext)
{
    /* Find the last dot and the last slash -- for file.tar.gz we want just
    .gz as an extension; for /etc/rc.conf/bak we don't want to split at the
    folder name. */
    const std::size_t pos = filename.find_last_of('.');
    const std::size_t lastSlash = filename.find_last_of('/');

    /* Empty extension if there's no dot or if the dot is not inside the
       filename */
    if(pos == std::string::npos || (lastSlash != std::string::npos && pos < lastSlash)) {
        root = filename;
        ext = "";
        return false;
    }

    /* If the dot at the start of the filename (/root/.bashrc), it's also an
       empty extension. Multiple dots at the start (/home/mosra/../..) classify
       as no extension as well. */
    std::size_t prev = pos;
    while(prev && filename[prev - 1] == '.') --prev;
    assert(pos < filename.size());
    if(prev == 0 || filename[prev - 1] == '/') {
        root = filename;
        ext = "";
        return false;
    }

    /* Otherwise it's a real extension */
    root = filename.substr(0, pos);
    ext = filename.substr(pos);
    return true;
}

std::string PathJoin(std::string path, std::string filename)
{
    /* Empty path */
    if(path.empty()) return filename;

    #ifdef _WIN32
    /* Absolute filename on Windows */
    if(filename.size() > 2 && filename[1] == ':' && filename[2] == '/')
        return filename;
    #endif

    /* Absolute filename */
    if(!filename.empty() && filename[0] == '/')
        return filename;

    /* Add trailing slash to path, if not present */
    if(path.back() != '/')
        return path + '/' + filename;

    return path + filename;
}

std::string ConfigurationDir(const std::string& app_name)
{
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    if(!SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return {};
    const std::string appdata{PathFromNative(Narrow(path))};
    return appdata.empty() ? std::string{} : PathJoin(appdata, app_name);
#endif
}

bool PathExists(const std::string& filename)
{
#ifdef _WIN32
    return GetFileAttributesW(Widen(filename).c_str()) != INVALID_FILE_ATTRIBUTES;
#endif
}

bool PathIsDirectory(const std::string& path)
{
#ifdef _WIN32
    const DWORD fileAttributes = GetFileAttributesW(Widen(path).data());
    return fileAttributes != INVALID_FILE_ATTRIBUTES && (fileAttributes & FILE_ATTRIBUTE_DIRECTORY);
#endif
}

bool Copy(const std::string& from, const std::string& to)
{
    std::FILE* const in = _wfopen(Widen(from).c_str(), L"rb");
    if(!in) {
        std::cerr << "Copy: can't open " << from;
        return false;
    }
    ScopeGuard close_in{ [in]() { std::fclose(in); } };

    std::FILE* const out = _wfopen(Widen(to).c_str(), L"wb");
    if(!out) {
        std::cerr << "Copy: can't open " << to;
        return false;
    }
    ScopeGuard close_out{ [out]() { std::fclose(out); } };

    char buffer[128*1024];
    std::size_t count;
    do {
        count = std::fread(buffer, 1, sizeof(buffer), in);
        std::fwrite(buffer, 1, count, out);
    } while(count);

    return true;
}

bool CreatePath(const std::string& path)
{
    if(path.empty()) return false;

    /* If path contains trailing slash, strip it */
    if(path.back() == '/')
        return CreatePath(path.substr(0, path.size()-1));

    /* If parent directory doesn't exist, create it */
    const std::string parentPath = PathDirectory(path);
    if(!parentPath.empty() && !PathExists(parentPath) && !CreatePath(parentPath)) return false;

    /* Create directory, return true if successfully created or already exists */

#ifdef _WIN32
    if(CreateDirectoryW(Widen(path).data(), nullptr) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        std::cerr << "Utility::Directory::mkpath(): error creating "
                    << path << ": "
                    << ErrorMessage(GetLastError());
        return false;
    }
    return true;
#endif
}

static std::size_t GetFileSize(std::FILE* const fh)
{
    std::fseek(fh, 0, SEEK_END);
    std::size_t size = std::ftell(fh);
    std::rewind(fh);
    return size;
}

bool ReadFileText(const std::string& path, std::string& str)
{
    std::FILE* const fh = _wfopen(Widen(path).c_str(), L"r");
    if (!fh) {
        std::cerr << "ReadFileText: can't open " << path;
        return false;
    }
    ScopeGuard fh_close{ [fh]() { std::fclose(fh); } };

    std::size_t size = GetFileSize(fh);
    char* buf = new char[size];
    ScopeGuard del_buf{ [buf]() { delete[] buf; } };

    std::size_t read = std::fread(buf, 1, size, fh);
    str = std::string{ buf, read };
    return true;
}

#ifdef _WIN32
static unsigned long long FileTimeToUint64(FILETIME *FileTime)
{
    ULARGE_INTEGER LargeInteger;
    LargeInteger.LowPart = FileTime->dwLowDateTime;
    LargeInteger.HighPart = FileTime->dwHighDateTime;
    return (unsigned long long)LargeInteger.QuadPart;
}

static unsigned long long NativeGetFileModifiedTime(const char* filename)
{
    FILETIME LastWriteTime = {};
    WIN32_FILE_ATTRIBUTE_DATA Data;
    if (GetFileAttributesEx(filename, GetFileExInfoStandard, &Data))
    {
        LastWriteTime = Data.ftLastWriteTime;
    }
    return FileTimeToUint64(&LastWriteTime);
}
#endif

unsigned long long GetFileModifiedTime(const std::string& path)
{
    return NativeGetFileModifiedTime(path.c_str());
}