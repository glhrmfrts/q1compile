#include <array>
#include <string_view>
#include "map_file.h"
#include "path.h"

namespace map_file {

struct MapFileParser
{
    explicit MapFileParser(std::string_view text)
    {
        _text = text;
        Parse();
    }

    void Parse()
    {
        for (;_offs < _text.size(); _offs++) {
            char c = _text[_offs];
            char cn = (_offs < _text.size()-1) ? _text[_offs+1] : 0;

            switch (_state) {
            case Default: {
                if (c == '/' && cn == '/') {
                    _state = Comment;
                    _offs++;
                }
                else if (c == '{') {
                    _state = Entity1;
                    _entities.push_back({});
                }
                break;
            }

            case Entity1: {
                if (c == '"') {
                    _state = FieldKey;
                    _field_begin = _offs + 1;
                }
                else if (c == '{') {
                    _state = Brushes;
                    _field_begin = _offs + 1;
                }
                else if (c == '}') {
                    _state = Default;
                }
                break;
            }

            case Entity2: {
                if (c == '"') {
                    _state = FieldValue;
                    _field_begin = _offs + 1;
                }
                break;
            }

            case FieldKey: {
                if (c == '"') {
                    _state = Entity2;
                    _field_key = std::string_view(_text.data()+_field_begin, _offs - _field_begin);
                }
                break;
            }

            case FieldValue: {
                if (c == '"') {
                    _state = Entity1;
                    _field_value = std::string_view(_text.data()+_field_begin, _offs - _field_begin);
                    _entities.back().fields[_field_key] = _field_value;
                }
                break;
            }

            case Brushes: {
                if (c == '}') {
                    _state = Entity1;
                    _entities.back().brush_content.push_back(std::string_view(_text.data() + _field_begin, _offs - _field_begin));
                }
                break;
            }

            case Comment:
                if (c == '\n') {
                    _state = Default;
                }
                break;
            }
        }
    }

    enum State
    {
        Default,
        Entity1,
        Entity2,
        FieldKey,
        FieldValue,
        Brushes,
        Comment,
    };

    std::string_view _text;
    std::string_view _field_key;
    std::string_view _field_value;
    std::vector<MapEntity> _entities;
    std::size_t _offs = 0;
    std::size_t _field_begin = 0;
    State _state = Default;
};

static bool Contains(std::string_view hay, std::string_view ned)
{
    return hay.find(ned) != std::string::npos;
}

static bool ShouldIgnoreFieldForDiff(std::string_view field, const std::vector<std::string>& ignore_field_diff)
{
    for (const auto& custom_field : ignore_field_diff) {
        if (field == custom_field) {
            return true;
        }
    }
    return (
        (field == "_tb_group") || (field == "_tb_id")
    );
}

static bool IsWorldspawnLightField(std::string_view field, const std::vector<std::string>& custom_worldspawn_light_fields)
{
    static const char* light_fields[] = {
        // Ambient Occlusion options
        "_dirt", "_dirtmode", "_dirtscale", "_dirtgain", "_dirtdepth", "_dirtangle",

        // Bounce lighting options
        "_bounce", "_bouncescale", "_bouncecolorscale", "_bouncestyled",

        // Sun options
        "_sunlight", "_sunlight_color", "_sunlight_mangle", "_anglescale", "_sunlight_dirt", "_sunlight_penumbra",
        "_sunlight2", "_sunlight2_color", "_sunlight2_dirt",
        "_sunlight3", "_sunlight3_color",

        // World lighting options
        "_minlight", "_minlight_color", "_minlight_dirt", "_range", "_dist", "_gamma", "_spotlightautofalloff"
    };

    const size_t count = sizeof(light_fields) / sizeof(const char*);
    for (size_t i = 0; i < count; i++) {
        if (field == light_fields[i]) {
            return true;
        }
        for (const auto& custom_field : custom_worldspawn_light_fields) {
            if (field == custom_field) {
                return true;
            }
        }
    }

    return false;
}

static bool IsBrushEntityLightField(std::string_view field, const std::vector<std::string>& custom_brush_light_fields)
{
    static const char* light_fields[] = {
        "_minlight", "_minlight_color", "_lightignore", "_minlight_exclude",
        "_shadow", "_shadowself", "_shadowworldonly", "_phong", "_phong_angle", "_phong_angle_concave", "_dirt"
    };

    const size_t count = sizeof(light_fields) / sizeof(const char*);
    for (size_t i = 0; i < count; i++) {
        if (field == light_fields[i]) {
            return true;
        }
        for (const auto& custom_field : custom_brush_light_fields) {
            if (field == custom_field) {
                return true;
            }
        }
    }

    return false;
}

static bool IsCustomLightEntity(std::string_view classname, const std::vector<std::string>& custom_light_entities)
{
    for (const auto& c : custom_light_entities) {
        if (classname == c) {
            return true;
        }
    }
    return false;
}

MapFile::MapFile(const std::string& path)
{
    path::ReadFileText(path, _text);

    MapFileParser parser{ _text };
    _entities = std::move(parser._entities);

    // look for layer entities
    for (auto& ent : _entities) {
        if (ent.fields["classname"] == "func_group" && ent.fields["_tb_type"] == "_tb_layer") {
            _layers.push_back(MapLayer{std::string{ent.fields["_tb_name"]}, std::string{ent.fields["_tb_id"]}});
        }
    }
}

std::string MapFile::GetTBMod() const {
    if (!_entities.size()) {
        return "";
    }

    auto it = _entities[0].fields.find("_tb_mod");
    if (it == _entities[0].fields.end()) {
        return "";
    }

    auto str = std::string(it->second);
    auto pos = str.find(';');
    if (pos != std::string::npos) {
        return str.substr(0, pos);
    }
    else {
        return str;
    }
}

std::string MapFile::GetBrushContent() const {
    std::string buf;
    for (const auto& ent : _entities) {
        for (const auto& content : ent.brush_content) {
            buf.append(content);
        }
    }
    return buf;
}

std::string MapFile::GetEntityContent(const std::vector<std::string>& ignore_field_diff) const {
    std::string buf;
    for (const auto& ent : _entities) {
        auto classname_it = ent.fields.find("classname");
        if (classname_it == ent.fields.end()) {
            continue;
        }

        if (!Contains(classname_it->second, "light")) {
            for (const auto& field : ent.fields) {
                if (!ShouldIgnoreFieldForDiff(field.first, ignore_field_diff)) {
                    buf.append(field.first);
                    buf.append(field.second);
                }
            }
        }
    }
    return buf;
}

std::string MapFile::GetLightContent(
    const std::vector<std::string>& custom_worldspawn_light_fields,
    const std::vector<std::string>& custom_brush_light_fields,
    const std::vector<std::string>& custom_light_entities,
    const std::vector<std::string>& ignore_field_diff
) const {
    std::string buf;
    for (const auto& ent : _entities) {
        auto classname_it = ent.fields.find("classname");
        if (classname_it == ent.fields.end()) {
            continue;
        }

        if ((Contains(classname_it->second, "light") && (ent.brush_content.size() == 0))
            || IsCustomLightEntity(classname_it->second, custom_light_entities)) {
            // Check light entity fields
            for (const auto& field : ent.fields) {
                if (!ShouldIgnoreFieldForDiff(field.first, ignore_field_diff)) {
                    buf.append(field.first);
                    buf.append(field.second);
                }
            }
        }

        if (ent.brush_content.size() > 0) {
            // Check light-related fields for brush entities
            for (const auto& field : ent.fields) {
                if (IsBrushEntityLightField(field.first, custom_brush_light_fields)) {
                    buf.append(field.first);
                    buf.append(field.second);
                }
            }
        }

        if (classname_it->second == "worldspawn") {
            // Check light-related fields for the worldspawn entity
            for (const auto& field : ent.fields) {
                if (IsWorldspawnLightField(field.first, custom_worldspawn_light_fields)) {
                    buf.append(field.first);
                    buf.append(field.second);
                }
            }
        }
    }
    return buf;
}

static std::string GetDiffFlagName(MapDiffFlags f)
{
    switch (f) {
    case MAP_DIFF_BRUSHES:
        return "brushes";
    case MAP_DIFF_ENTS:
        return "ents";
    case MAP_DIFF_LIGHTS:
        return "lights";
    }
    return "none";
}

static MapDiffFlags GetFlagForDiffContent(const std::string& ca, const std::string& cb, MapDiffFlags flag)
{
#if 0
    std::string fna = GetDiffFlagName(flag) + "_a.txt";
    std::string fnb = GetDiffFlagName(flag) + "_b.txt";
    WriteFileText(fna, ca);
    WriteFileText(fnb, cb);
#endif

    if (ca != cb) {
        return flag;
    }
    else {
        return MAP_DIFF_NONE;
    }
}

MapDiffFlags GetDiffFlags(const MapFile& a, const MapFile& b,
    const std::vector<std::string>& custom_worldspawn_light_fields,
    const std::vector<std::string>& custom_brush_light_fields,
    const std::vector<std::string>& custom_light_entities,
    const std::vector<std::string>& ignore_field_diff
)
{
    std::string a_light = a.GetLightContent(custom_worldspawn_light_fields, custom_brush_light_fields, custom_light_entities, ignore_field_diff);
    std::string b_light = b.GetLightContent(custom_worldspawn_light_fields, custom_brush_light_fields, custom_light_entities, ignore_field_diff);

    MapDiffFlags flags = MAP_DIFF_NONE;
    flags |= GetFlagForDiffContent(a.GetBrushContent(), b.GetBrushContent(), MAP_DIFF_BRUSHES);
    flags |= GetFlagForDiffContent(a.GetEntityContent(ignore_field_diff), b.GetEntityContent(ignore_field_diff), MAP_DIFF_ENTS);
    flags |= GetFlagForDiffContent(a_light, b_light, MAP_DIFF_LIGHTS);
    return flags;
}

}