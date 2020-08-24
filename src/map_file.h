#pragma once

#include <fstream>
#include <vector>
#include <map>
#include <string_view>

struct MapEntity
{
    std::map<std::string_view, std::string_view> fields;
    std::vector<std::string_view> brush_content;
};

struct MapFile
{
    explicit MapFile(const std::string& path);

    std::string GetTBMod() const;

    bool Good() const { return !_text.empty(); }

    std::string _text;
    std::vector<MapEntity> _entities;
};