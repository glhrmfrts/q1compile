#include "render.h"

namespace render {

extern LPDIRECT3DDEVICE9 g_pd3dDevice;

constexpr DWORD g_vertex_fvf = D3DFVF_XYZ | D3DFVF_TEX1;

bool BspRenderer::UploadData()
{
    // Upload vertices
    {
        unsigned int size = vertices.size() * sizeof(Vertex);
        if (FAILED(g_pd3dDevice->CreateVertexBuffer(size, D3DUSAGE_WRITEONLY, g_vertex_fvf, D3DPOOL_DEFAULT, &vertex_buffer, NULL))) {
            return false;
        }

        void* data;
        if (FAILED(vertex_buffer->Lock(0, size, &data, 0))) {
            return false;
        }
        memcpy(data, vertices.data(), size);
        vertex_buffer->Unlock();
    }

    // Upload indices
    {
        unsigned int size = triangles.size() * sizeof(Triangle);
        if (FAILED(g_pd3dDevice->CreateIndexBuffer(size, D3DUSAGE_WRITEONLY, D3DFMT_INDEX32, D3DPOOL_DEFAULT, &index_buffer, NULL))) {
            return false;
        }

        void* data;
        if (FAILED(index_buffer->Lock(0, size, &data, 0))) {
            return false;
        }
        memcpy(data, (unsigned int*)triangles.data(), size);
        index_buffer->Unlock();
    }

    // Upload textures
    for (auto& image : images) {
        if (FAILED(g_pd3dDevice->CreateTexture(image.width, image.height, 1, 0, D3DFMT_A8B8G8R8, D3DPOOL_DEFAULT, &image.texture, NULL))) {
            return false;
        }

        D3DLOCKED_RECT locked_rect;
        RECT rect;
        rect.right = image.width;
        rect.bottom = image.height;
        rect.top = 0;
        rect.left = 0;
        if (FAILED(image.texture->LockRect(0, &locked_rect, &rect, 0))) {
            return false;
        }
        for (unsigned int y = 0; y < image.height; y++) {
            unsigned char* dest = (unsigned char*)locked_rect.pBits + y * locked_rect.Pitch;
            for (int x = 0; x < image.width; x++) {
                int idx = y * image.height + x;
                *dest++ = image.pixels[idx].a;
                *dest++ = image.pixels[idx].b;
                *dest++ = image.pixels[idx].g;
                *dest++ = image.pixels[idx].r;
            }
        }
        image.texture->UnlockRect(0);
    }

    return true;
}

void BspRenderer::Render()
{
    g_pd3dDevice->SetStreamSource(0, vertex_buffer, 0, sizeof(Vertex));
    g_pd3dDevice->SetFVF(g_vertex_fvf);

    for (int i = 0; i < (int)images.size(); i++) {
        auto& tri_list = tri_lists[i].tri_indices;
        g_pd3dDevice->SetTexture(0, images[i].texture);
        g_pd3dDevice->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        g_pd3dDevice->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        g_pd3dDevice->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);   //Ignored
        g_pd3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, tri_list.size()*3, tri_list[0]*3, tri_list.size());
    }
}

void BspRenderer::Destroy()
{
    for (auto& image : images) {
        image.texture->Release();
    }
    images.clear();
    index_buffer->Release();
    vertex_buffer->Release();
}

static RGBA g_pallete[] = { {0,0,0,0} };

BspRenderer* Renderer::GetOrCreateBspRenderer(const bsp::Bsp& bsp)
{
    auto br = std::make_unique<BspRenderer>();
    if (br->filename_hash == bsp.filename_hash && br->modified_time == bsp.modified_time) {
        return br.get();
    }

    // Again, code mainly converted from: https://github.com/joshuaskelly/vgio/blob/master/vgio/quake/bsp/bsp29.py

    for (const auto& miptex : bsp.mip_textures) {
        auto& image = br->images.emplace_back();
        image.width = miptex.width;
        image.height = miptex.height;
        image.pixels.resize(image.width * image.height);

        int offset = miptex.offsets[0];
        for (int y = 0; y < miptex.height; y++) {
            for (int x = 0; x < miptex.width; x++) {
                int idx = y * miptex.width + x;
                int palindex = miptex.pixels[(idx + offset)];
                image.pixels[idx] = g_pallete[palindex];
            }
        }
    }
    
    for (const auto& model : bsp.models) {
        for (int fi = model.first_face; fi < model.first_face + model.num_faces; fi++) {
            const auto& face = bsp.faces[fi];
            const auto& tinfo = bsp.tex_infos[face.tex_info];
            const auto& miptex = bsp.mip_textures[tinfo.miptexture_number];

            static std::vector<Vertex> verts;
            static std::vector<Triangle> tris;
            verts.clear();
            tris.clear();

            for (int ei = face.first_edge; ei < face.first_edge + face.num_edges; ei++) {
                const auto& edge = bsp.edges[ei];
                const auto& v0 = bsp.vertices[edge.v[0]];
                const auto& v1 = bsp.vertices[edge.v[1]];
                if (verts.size() == 0) {
                    verts.push_back(Vertex{v0.pos});
                }
                if (v1.pos != verts[0].pos) {
                    verts.push_back(Vertex{v1.pos});
                }
            }

            // Ignore degenerate faces
            if (verts.size() < 3) { continue; }

            //Vec3 normal = Cross(verts[0].pos - verts[1].pos, verts[0].pos - verts[2].pos);
            for (auto& v : verts) {
                float dot_vs = Dot(v.pos, tinfo.s);
                float dot_vt = Dot(v.pos, tinfo.t);

                v.uv = Vec2{dot_vs + tinfo.s_offset / miptex.width, -(dot_vt + tinfo.t_offset) / miptex.height};
                //v.normal = normal;
            }

            unsigned int start_index = br->vertices.size();
            unsigned int end_index = start_index + verts.size();
            unsigned int v0 = start_index;
            for (unsigned int index = start_index+1; index < end_index - 1; index++) {
                unsigned int v1 = index;
                unsigned int v2 = index + 1;
                tris.push_back({v0, v1, v2});
            }

            unsigned int start_tri = br->triangles.size();
            unsigned int end_tri = start_tri + tris.size();

            for (unsigned int tri_index = start_tri; tri_index < end_tri; tri_index++) {
                br->tri_lists[tinfo.miptexture_number].tri_indices.push_back(tri_index);
            }
            for (const auto& v : verts) {
                br->vertices.push_back(v);
            }
            for (const auto& t : tris) {
                br->triangles.push_back(t);
            }
        }
    }

    br->UploadData();
}

void Renderer::RenderBsp(const bsp::Bsp& bsp)
{
    auto br = GetOrCreateBspRenderer(bsp);
    if (!br) { return; }

    br->Render();
}

}