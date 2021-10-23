#include "bsp.h"
#include "common.h"
#include <cstdio>
#include "path.h"

#define BSP_VERSION 29

namespace bsp {

static bool IsBspFile(FILE* fh)
{
    fseek(fh, 0, SEEK_SET);

    int version;
    fread(&version, sizeof(int), 1, fh);

    fseek(fh, 0, SEEK_SET);
    return version == BSP_VERSION;
}

static bool IsBsp2File(FILE* fh)
{
    fseek(fh, 0, SEEK_SET);

    char version[4];
    fread(version, sizeof(version), 1, fh);

    fseek(fh, 0, SEEK_SET);
    return !memcmp(version, "BSP2", sizeof(version));
}

static void ReadLumpData(FILE* fh, const Header& header, int lump, std::vector<uint8_t>& data)
{
    data.resize(header.lumps[lump].file_length);
    fseek(fh, header.lumps[lump].file_offset, SEEK_SET);
    fread(data.data(), sizeof(uint8_t), data.size(), fh);
}

template <typename T>
static int ReadDataIntoVector(uint8_t* data, size_t size, std::vector<T>& vec)
{
    int num_elems = size / sizeof(T);
    for (int i = 0; i < num_elems; i++) {
        vec.push_back(*reinterpret_cast<T*>(data + (i * sizeof(T))));
    }
    return num_elems;
}

template <typename T>
static int ReadDataIntoVector(std::vector<uint8_t>& data, std::vector<T>& vec)
{
    return ReadDataIntoVector(data.data(), data.size(), vec);
}

template <typename T, typename P>
static int ReadDataIntoVector(std::vector<uint8_t>& data, std::vector<P>& vec, std::function<P(const T&)> fn)
{
    int num_elems = data.size() / sizeof(T);
    for (int i = 0; i < num_elems; i++) {
        vec.push_back(fn(*reinterpret_cast<T*>(data.data() + (i*sizeof(T)))));
    }
    return num_elems;
}

std::string Bsp::ReadFromFile(const std::string& path)
{
    FILE* fh = fopen(path.c_str(), "rb");
    if (fh == NULL) { return "Could not read file"; }
    common::ScopeGuard _{ [&]() { fclose(fh); } };

    bool is_bsp2 = false;
    if (!IsBspFile(fh)) {
        if (!IsBsp2File(fh)) {
            return "Not a valid bsp file";
        }
        is_bsp2 = true;
    }

    Header header;
    fread(&header, sizeof(header), 1, fh);

    auto bsp = this;
    bsp->filename = path;
    bsp->filename_hash = std::hash<std::string>{}(path);
    bsp->modified_time = path::GetFileModifiedTime(path);
    bsp->entities.clear();
    bsp->mip_textures.clear();
    bsp->vertices.clear();
    bsp->tex_infos.clear();
    bsp->faces.clear();
    bsp->lighting.clear();
    bsp->edges.clear();
    bsp->models.clear();
    bsp->surf_edges.clear();
    bsp->nodes.clear();
    bsp->clip_nodes.clear();
    bsp->mark_surfaces.clear();
    bsp->planes.clear();

    std::vector<uint8_t> data;
    {
        ReadLumpData(fh, header, LUMP_ENTITIES, data);
        bsp->entities = std::string((const char*)data.data(), data.size());
    }
    {
        ReadLumpData(fh, header, LUMP_TEXTURES, data);
        int32_t number_of_miptextures = *reinterpret_cast<int32_t*>(data.data());
        int32_t* offsets = reinterpret_cast<int32_t*>(data.data()) + 1;
        for (int id = 0; id < number_of_miptextures; id++) {
            int offset = offsets[id];
            if (offset == -1) { 
                bsp->mip_textures.push_back(Miptexture{});
                continue;
            }

            MiptextureData* miptex_data = reinterpret_cast<MiptextureData*>(data.data() + offset);
            uint8_t* pixels = reinterpret_cast<uint8_t*>(miptex_data + 1);
            int pixels_size = miptex_data->width * miptex_data->height * 85 / 64;

            Miptexture miptex = {};
            miptex.data = *miptex_data;
            for (int i = 0; i < pixels_size; i++) {
                miptex.pixels.push_back(*pixels++);
            }

            bsp->mip_textures.push_back(miptex);
        }
    }
    {
        ReadLumpData(fh, header, LUMP_VERTEXES, data);
        ReadDataIntoVector(data, bsp->vertices);
    }
    {
        ReadLumpData(fh, header, LUMP_TEXINFO, data);
        ReadDataIntoVector(data, bsp->tex_infos);
    }
    {
        ReadLumpData(fh, header, LUMP_FACES, data);
        if (!is_bsp2) {
            ReadDataIntoVector<Face29, Face>(data, bsp->faces, [](const Face29& face) -> Face {
                Face f = {};
                f.first_edge = face.firstedge;
                f.num_edges = (int)face.numedges;
                f.plane_number = (int)face.planenum;
                f.side = (int)face.side;
                f.tex_info = (int)face.texinfo;
                f.light_ofs = face.lightofs;
                memcpy(f.styles, face.styles, sizeof(f.styles));
                return f;
            });
        }
        else {
            ReadDataIntoVector(data, bsp->faces);
        }
    }
    {
        // TODO: support .lit files

        std::vector<uint8_t> raw_lighting;

        ReadLumpData(fh, header, LUMP_LIGHTING, data);
        ReadDataIntoVector(data, raw_lighting);

        std::string lit_filename = filename;
        while (common::StrReplace(lit_filename, ".bsp", ".lit"));

        bool has_lit_data = false;
        if (path::Exists(lit_filename)) {
            std::vector<uint8_t> lit_data;
            path::ReadFileBytes(lit_filename, lit_data);
            if (!memcmp(lit_data.data(), "QLIT", 4)) {
                int32_t version = *(reinterpret_cast<int32_t*>(lit_data.data()) + 1);
                if (version == 1) {
                    if (header.lumps[LUMP_LIGHTING].file_length * 3 == lit_data.size() - sizeof(int32_t)*2) {
                        ReadDataIntoVector(lit_data.data()+sizeof(int32_t)*2, lit_data.size() - sizeof(int32_t) * 2, bsp->lighting);
                        has_lit_data = true;
                    }
                }
            }
        }

        if (!has_lit_data) {
            for (auto i : raw_lighting) { bsp->lighting.push_back(RGB8{ i, i, i }); }
        }

        lightmap_shift = 4;
    }
    {
        ReadLumpData(fh, header, LUMP_EDGES, data);
        if (!is_bsp2) {
            ReadDataIntoVector<Edge29, Edge>(data, bsp->edges, [](const Edge29& edge) -> Edge {
                Edge e = {};
                e.v[0] = (uint32_t)edge.v[0];
                e.v[1] = (uint32_t)edge.v[1];
                return e;
            });
        }
        else {
            ReadDataIntoVector(data, bsp->edges);
        }
    }
    {
        ReadLumpData(fh, header, LUMP_SURFEDGES, data);
        ReadDataIntoVector(data, bsp->surf_edges);
    }
    {
        ReadLumpData(fh, header, LUMP_MODELS, data);
        ReadDataIntoVector(data, bsp->models);
    }

    return "";
}

}