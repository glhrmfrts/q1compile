#pragma once

#include <d3d9.h>
#include <d3dcompiler.h>
#include <unordered_map>
#include "bsp.h"

namespace render {

struct Vertex
{
    Vec3 pos;
    Vec2 uv;
    //Vec3 normal; No need for the normal because we're not doing RT lighting.
};

struct Triangle
{
    unsigned int v0, v1, v2;
};

struct TriangleList
{
    std::vector<int> tri_indices;
};

struct RGBA
{
    unsigned char r,g,b,a;
};

struct Image
{
    unsigned int width, height;
    std::vector<RGBA> pixels; // Assuming RGBA for now
    IDirect3DTexture9* texture;
};

struct ShaderProgram
{
    bool Compile(const std::string& source_code);
};

struct BspRenderer
{
    size_t filename_hash;
    unsigned int modified_time;
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Image> images;

    // draw calls, image index -> triangle indices
    std::unordered_map<int, TriangleList> tri_lists;

    IDirect3DVertexBuffer9* vertex_buffer;
    IDirect3DIndexBuffer9* index_buffer;

    ~BspRenderer() { Destroy(); }

    bool UploadData();

    void Render();

    void Destroy();
};

struct Renderer
{
    std::vector<std::unique_ptr<BspRenderer>> bsp_renderers;
    
    BspRenderer* GetOrCreateBspRenderer(const bsp::Bsp& b);

    void RenderBsp(const bsp::Bsp& b);
};

}