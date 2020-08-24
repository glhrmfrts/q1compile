#include <array>
#include <string_view>
#include "map_file.h"
#include "path.h"

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

MapFile::MapFile(const std::string& path)
{
    ReadFileText(path, _text);

    MapFileParser parser{ _text };
    _entities = std::move(parser._entities);
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