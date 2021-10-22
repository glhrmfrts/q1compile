#pragma once

#include <d3d9.h>
#include <d3dcompiler.h>
#include <unordered_map>
#include "bsp.h"

namespace render {

#define MAX_SANITY_LIGHTMAPS (1u<<20)

#define LMBLOCK_WIDTH 256
#define LMBLOCK_HEIGHT 256

struct Vertex
{
    Vec3 pos;
    Vec2 uv;
    Vec2 uvlm;
    //Vec3 normal; No need for the normal because we're not doing RT lighting.
};

struct Triangle
{
    uint32_t v0, v1, v2;
};

struct DrawCall
{
    uint32_t start_tri;
    uint32_t num_tris;
    uint32_t texture_id;
    uint32_t lightmap_id;
};

struct Image
{
    uint32_t width, height;
    std::vector<RGBA8> pixels; // Assuming RGBA for now
    IDirect3DTexture9* texture;
};

struct Lightmap
{
    std::array<RGBA8, LMBLOCK_WIDTH * LMBLOCK_HEIGHT> data;
    IDirect3DTexture9* texture;
};

struct Surface
{
    bsp::Face* source_face;
    int extents[2];
    int texture_mins[2];
    float mins[2];
    float maxs[2];
};

struct BspRenderer
{
    size_t filename_hash;
    unsigned int modified_time;

    std::vector<Surface> surfaces;

    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Image> images;

    // key = texture_id | (lightmap_id << 16)
    std::unordered_map<int32_t, std::vector<DrawCall>> draw_calls;

    std::vector<Lightmap> lightmaps;
    int last_lightmap_allocated;
    int lightmap_allocated[LMBLOCK_WIDTH];

    IDirect3DVertexBuffer9* vertex_buffer;
    IDirect3DIndexBuffer9* index_buffer;

    ~BspRenderer() { Destroy(); }

    bool UploadData();

    std::tuple<int, int, int> CreateLightmap(int w, int h);

    void Render(const Vec3& cam_pos, const Vec3& cam_rot);

    void Destroy();
};

struct Renderer
{
    std::vector<std::unique_ptr<BspRenderer>> bsp_renderers;
    
    BspRenderer* GetOrCreateBspRenderer(const bsp::Bsp& b);

    void RenderBsp(const Vec3& cam_pos, const Vec3& cam_rot, const bsp::Bsp& b);
};

}