#pragma once

#include <fstream>
#include <vector>
#include <map>
#include <string_view>

namespace map_file {

typedef unsigned int MapDiffFlags;

static constexpr MapDiffFlags MAP_DIFF_NONE = 0x0;
static constexpr MapDiffFlags MAP_DIFF_ENTS = 0x1;
static constexpr MapDiffFlags MAP_DIFF_LIGHTS = 0x2;
static constexpr MapDiffFlags MAP_DIFF_BRUSHES = 0x4;

struct MapEntity
{
    std::map<std::string_view, std::string_view> fields;
    std::vector<std::string_view> brush_content;
};

struct MapFile
{
    explicit MapFile(const std::string& path);

    std::string GetTBMod() const;

    std::string GetBrushContent() const;

    std::string GetEntityContent(const std::vector<std::string>& ignore_field_diff) const;

    std::string GetLightContent(
        const std::vector<std::string>& custom_worldspawn_light_fields,
        const std::vector<std::string>& custom_brush_light_fields,
        const std::vector<std::string>& custom_light_entities,
        const std::vector<std::string>& ignore_field_diff
    ) const;

    bool Good() const { return !_text.empty(); }

    std::string _text;
    std::vector<MapEntity> _entities;
};

MapDiffFlags GetDiffFlags(const MapFile& a, const MapFile& b,
    const std::vector<std::string>& custom_worldspawn_light_fields,
    const std::vector<std::string>& custom_brush_light_fields,
    const std::vector<std::string>& custom_light_entities,
    const std::vector<std::string>& ignore_field_diff
);

}