#pragma once

#include <string>

namespace path {

std::string qc_GetAppDir();

std::string qc_GetTempDir();

std::string qc_GetUserName();

std::string FromNative(std::string path);

std::string ToNative(std::string path);

std::string Directory(std::string path);

std::string Filename(std::string path);

std::string Join(std::string s1, std::string s2);

std::string ConfigurationDir(const std::string& app_name);

bool SplitExtension(const std::string& filename, std::string& root, std::string& ext);

bool Exists(const std::string& path);

bool IsDirectory(const std::string& path);

bool Copy(const std::string& from, const std::string& to);

bool Create(const std::string& path);

bool Remove(const std::string& path);

bool ReadFileText(const std::string& path, std::string& str);

bool WriteFileText(const std::string& path, const std::string& str);

unsigned long long GetFileModifiedTime(const std::string& path);

std::size_t GetFileSize(const std::string& path);

}