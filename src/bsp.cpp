#include "bsp.h"
#include "common.h"
#include <cstdio>

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

static void ReadLumpData(FILE* fh, const Header& header, int lump, std::vector<char>& data)
{
    data.resize(header.lumps[lump].file_length);
    fseek(fh, header.lumps[lump].file_offset, SEEK_SET);
    fread(data.data(), sizeof(char), data.size(), fh);
}

template <typename T>
static int ReadDataIntoVector(const std::vector<char>& data, std::vector<T>& vec)
{
    int num_elems = data.size() / sizeof(T);
    for (int i = 0; i < num_elems; i++) {
        vec.push_back(*reinterpret_cast<T*>(data.data() + (i*sizeof(T))));
    }
    return num_elems;
}

template <typename T, typename P>
static int ReadDataIntoVector(const std::vector<char>& data, std::vector<P>& vec, std::function<P(const T&)> fn)
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
    bsp->entities.clear();
    bsp->mip_textures.clear();
    bsp->vertices.clear();
    bsp->tex_infos.clear();
    bsp->faces.clear();
    bsp->lighting.clear();
    bsp->edges.clear();
    bsp->models.clear();

    std::vector<char> data;
    {
        ReadLumpData(fh, header, LUMP_ENTITIES, data);
        bsp->entities = std::string(data.data(), data.size());
    }
    {
        ReadLumpData(fh, header, LUMP_TEXTURES, data);
        int number_of_miptextures = *reinterpret_cast<int*>(data.data());
        int* offsets = reinterpret_cast<int*>(data.data()) + 1;
        for (int id = 0; id < number_of_miptextures; id++) {
            int offset = offsets[id];
            Miptexture miptex = *reinterpret_cast<Miptexture*>(data.data() + offset);
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
                return f;
            });
        }
        else {
            ReadDataIntoVector(data, bsp->faces);
        }
    }
    {
        ReadLumpData(fh, header, LUMP_LIGHTING, data);
        ReadDataIntoVector(data, bsp->lighting);
    }
    {
        ReadLumpData(fh, header, LUMP_EDGES, data);
        if (!is_bsp2) {
            ReadDataIntoVector<Edge29, Edge>(data, bsp->edges, [](const Edge29& edge) -> Edge {
                Edge e = {};
                e.v[0] = (unsigned int)edge.v[0];
                e.v[1] = (unsigned int)edge.v[1];
                return e;
            });
        }
        else {
            ReadDataIntoVector(data, bsp->edges);
        }
    }
    {
        ReadLumpData(fh, header, LUMP_MODELS, data);
        ReadDataIntoVector(data, bsp->models);
    }

    return "";
}

}