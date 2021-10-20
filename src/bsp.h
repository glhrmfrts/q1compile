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
    unsigned int file_offset;
    unsigned int file_length;
};

struct Header
{
    int version;
    Lump lumps[HEADER_LUMPS];
};

struct Model
{
    float		mins[3], maxs[3];
    float		origin[3];
    int			head_node[MAX_MAP_HULLS];
    int			vis_leafs; // not including the solid leaf 0
    int			first_face;
    int         num_faces;
};

struct Plane
{
    Vec3 normal;
    float distance;
    int type;
};

struct Vertex
{
    Vec3 pos;
};

struct Node
{
    int plane_number;
    int children[2];

    float bounding_box_min[3];
    float bounding_box_max[3];

    unsigned int first_face;
    unsigned int num_faces;
};

struct ClipNode
{
    int			plane_num;
    int		    children[2];	// negative numbers are contents
};

struct Edge
{
    unsigned int v[2];
};

struct Edge29
{
    unsigned short v[2];
};

struct Face
{
    int		    plane_number;
    int		    side;

    int			first_edge;		// we must support > 64k edges
    int		    num_edges;
    int		    tex_info;

    // lighting info
    char		styles[4];
    int			light_ofs;		// start of [numstyles*surfsize] samples
};

struct Face29
{
    short		planenum;
	short		side;

	int			firstedge;		// we must support > 64k edges
	short		numedges;
	short		texinfo;

// lighting info
	char		styles[4];
	int			lightofs;		// start of [numstyles*surfsize] samples
};

struct Leaf
{
    int			contents;
    int			vis_ofs;				// -1 = no visibility info

    float		mins[3];			// for frustum culling
    float		maxs[3];

    unsigned int		first_mark_surface;
    unsigned int		num_mark_surfaces;

    char		        ambient_level[NUM_AMBIENTS];
};

struct Miptexture
{
    char name[16];
    int width;
    int height;
    int offsets[4];
    std::vector<int> pixels;
};

struct TextureInfo
{
    Vec3 s;
    float s_offset;
    Vec3 t;
    float t_offset;
    int miptexture_number;
    int flags;
};

struct Bsp
{
    std::atomic_bool updating_data;
    size_t filename_hash;
    unsigned int modified_time;
    std::string filename;
    std::string version;
    std::string entities;

    std::vector<Plane> planes;
    std::vector<Miptexture> mip_textures;
    std::vector<Vertex> vertices;
    std::vector<int> visibilites;
    std::vector<Node> nodes;
    std::vector<TextureInfo> tex_infos;
    std::vector<Face> faces;
    std::vector<int> lighting;
    std::vector<ClipNode> clip_nodes;
    std::vector<Leaf> leafs;
    std::vector<int> mark_surfaces;
    std::vector<Edge> edges;
    std::vector<int> surf_edges;
    std::vector<Model> models;

    std::string ReadFromFile(const std::string& path);
};

}