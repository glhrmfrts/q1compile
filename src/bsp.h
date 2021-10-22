#pragma once

// Code partially converted from Python code from: https://github.com/joshuaskelly/vgio/blob/master/vgio/quake/bsp/bsp29.py
// Everything is defined as BSP2 data, at parse-time we convert from vanilla BSP if needed.

#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include "lmath.h"

namespace bsp {

    // upper design bounds

#define	MAX_MAP_HULLS		4

#define	MAX_MAP_MODELS		256
#define	MAX_MAP_BRUSHES		4096
#define	MAX_MAP_ENTITIES	1024
#define	MAX_MAP_ENTSTRING	65536

#define	MAX_MAP_PLANES		32767
#define	MAX_MAP_NODES		32767 // because negative shorts are contents
#define	MAX_MAP_CLIPNODES	32767
//#define	MAX_MAP_LEAFS		80000 //johnfitz -- was 8192
#define	MAX_MAP_VERTS		65535
#define	MAX_MAP_FACES		65535
#define	MAX_MAP_MARKSURFACES 65535
#define	MAX_MAP_TEXINFO		4096
#define	MAX_MAP_EDGES		256000
#define	MAX_MAP_SURFEDGES	512000
#define	MAX_MAP_TEXTURES	512
#define	MAX_MAP_MIPTEX		0x200000
#define	MAX_MAP_LIGHTING	0x100000
#define	MAX_MAP_VISIBILITY	0x100000

#define	MAX_MAP_PORTALS		65536

// key / value pair sizes

#define	MAX_KEY		32
#define	MAX_VALUE	1024


#define	AMBIENT_WATER	0
#define	AMBIENT_SKY		1
#define	AMBIENT_SLIME	2
#define	AMBIENT_LAVA	3

#define	NUM_AMBIENTS			4		// automatic ambient sounds

#define	LUMP_ENTITIES	0
#define	LUMP_PLANES		1
#define	LUMP_TEXTURES	2
#define	LUMP_VERTEXES	3
#define	LUMP_VISIBILITY	4
#define	LUMP_NODES		5
#define	LUMP_TEXINFO	6
#define	LUMP_FACES		7
#define	LUMP_LIGHTING	8
#define	LUMP_CLIPNODES	9
#define	LUMP_LEAFS		10
#define	LUMP_MARKSURFACES 11
#define	LUMP_EDGES		12
#define	LUMP_SURFEDGES	13
#define	LUMP_MODELS		14

#define	HEADER_LUMPS	15

struct Lump
{
    uint32_t    file_offset;
    uint32_t    file_length;
};

struct Header
{
    int32_t version;
    Lump lumps[HEADER_LUMPS];
};

struct Model
{
    float		mins[3], maxs[3];
    float		origin[3];
    int32_t     head_node[MAX_MAP_HULLS];
    int32_t     vis_leafs; // not including the solid leaf 0
    uint32_t    first_face;
    uint32_t    num_faces;
};

struct Plane
{
    Vec3 normal;
    float distance;
    int32_t type;
};

struct Vertex
{
    Vec3 pos;
};

struct Node
{
    int32_t plane_number;
    int32_t children[2];

    float bounding_box_min[3];
    float bounding_box_max[3];

    uint32_t first_face;
    uint32_t num_faces;
};

struct ClipNode
{
    int32_t			plane_num;
    int32_t		    children[2];	// negative numbers are contents
};

struct Edge
{
    uint32_t v[2];
};

struct Edge29
{
    uint16_t v[2];
};

struct Face
{
    uint32_t		plane_number;
    int32_t         side;

    uint32_t    first_edge;		// we must support > 64k edges
    uint32_t    num_edges;
    uint32_t    tex_info;

    // lighting info
    char		styles[4];
    int32_t     light_ofs;		// start of [numstyles*surfsize] samples
};

struct Face29
{
    uint16_t		planenum;
    int16_t         side;

	uint32_t    firstedge;		// we must support > 64k edges
	uint16_t    numedges;
	uint16_t    texinfo;

// lighting info
	char		styles[4];
	int32_t		lightofs;		// start of [numstyles*surfsize] samples
};

struct Leaf
{
    int32_t     contents;
    int32_t     vis_ofs;				// -1 = no visibility info

    float		mins[3];			// for frustum culling
    float		maxs[3];

    uint32_t		first_mark_surface;
    uint32_t		num_mark_surfaces;

    char		        ambient_level[NUM_AMBIENTS];
};

struct MiptextureData
{
    char name[16];
    uint32_t width;
    uint32_t height;
    uint32_t offsets[4];
};

struct Miptexture
{
    MiptextureData data;
    std::vector<uint8_t> pixels;
};

struct TextureInfo
{
    Vec3 s;
    float s_offset;
    Vec3 t;
    float t_offset;
    uint32_t miptexture_number;
    uint32_t flags;
};

struct Bsp
{
    std::atomic_bool updating_data;
    size_t filename_hash;
    uint32_t modified_time;
    std::string filename;
    std::string version;
    std::string entities;
    int32_t lightmap_shift;
    std::vector<Plane> planes;
    std::vector<Miptexture> mip_textures;
    std::vector<Vertex> vertices;
    std::vector<int32_t> visibilites;
    std::vector<Node> nodes;
    std::vector<TextureInfo> tex_infos;
    std::vector<Face> faces;
    std::vector<RGB8> lighting;
    std::vector<ClipNode> clip_nodes;
    std::vector<Leaf> leafs;
    std::vector<int32_t> mark_surfaces;
    std::vector<Edge> edges;
    std::vector<int32_t> surf_edges;
    std::vector<Model> models;

    std::string ReadFromFile(const std::string& path);
};

}