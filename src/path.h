#pragma once

#include <string>

std::string qc_GetAppDir();

std::string qc_GetTempDir();

std::string qc_GetUserName();

std::string PathFromNative(std::string path);

std::string PathToNative(std::string path);

std::string PathDirectory(std::string path);

std::string PathFilename(std::string path);

std::string PathJoin(std::string s1, std::string s2);

std::string ConfigurationDir(const std::string& app_name);

bool SplitExtension(const std::string& filename, std::string& root, std::string& ext);

bool PathExists(const std::string& path);

bool PathIsDirectory(const std::string& path);

bool Copy(const std::string& from, const std::string& to);

bool CreatePath(const std::string& path);

bool RemovePath(const std::string& path);

bool ReadFileText(const std::string& path, std::string& str);

bool WriteFileText(const std::string& path, const std::string& str);

unsigned long long GetFileModifiedTime(const std::string& path);