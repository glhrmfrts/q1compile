#include "q1compile.h"
#include "render.h"
#include <glm/gtc/matrix_transform.hpp>

namespace render {

constexpr DWORD g_vertex_fvf = D3DFVF_XYZ | D3DFVF_TEX2;

static uint8_t quake_palette[] = // Quake palette - https://github.com/ericwa/ericw-tools/blob/7b33b12146390b9b91f37ab13cb04b8dc5f35fc2/light/imglib.cc
{
    0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,
    139,107,107,151,115,115,163,123,123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,
    55,0,75,59,7,87,67,7,95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,95,183,135,107,195,147,123,211,163,139,227,179,151,
    171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,107,87,71,95,75,59,83,63,
    51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,
    43,175,47,47,159,47,47,143,47,47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,59,183,155,55,199,195,55,231,227,87,127,191,255,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,243,147,255,247,199,255,255,255,159,91,83
};

static RGBA8 GetPaletteColor(unsigned int idx)
{
    if (idx > 255) {
        return RGBA8{ 0, 0, 0, 255 };
    }

    RGBA8 rgba = {};
    rgba.r = quake_palette[idx*3 + 0];
    rgba.g = quake_palette[idx*3 + 1];
    rgba.b = quake_palette[idx*3 + 2];
    rgba.a = 255;
    return rgba;
}

std::tuple<int, int, int> BspRenderer::CreateLightmap(int w, int h)
{   
    int x, y;
    int texnum = last_lightmap_allocated;

    for (texnum = last_lightmap_allocated; texnum < MAX_SANITY_LIGHTMAPS; texnum++) {
        if (texnum == lightmaps.size()) {
            auto& lightmap = lightmaps.emplace_back();
            memset(lightmap.data.data(), 0, sizeof(lightmap.data));
            memset(lightmap_allocated, 0, sizeof(lightmap_allocated));
        }

        int best = LMBLOCK_HEIGHT;

        for (int i = 0; i < LMBLOCK_WIDTH - w; i++)
        {
            int best2 = 0;

            int j = 0;
            for (j = 0; j < w; j++)
            {
                if (lightmap_allocated[i + j] >= best)
                    break;
                if (lightmap_allocated[i + j] > best2)
                    best2 = lightmap_allocated[i + j];
            }
            if (j == w)
            {	// this is a valid spot
                x = i;
                y = best = best2;
            }
        }

        if (best + h > LMBLOCK_HEIGHT)
            continue;

        for (int i = 0; i < w; i++)
            lightmap_allocated[x + i] = best + h;

        last_lightmap_allocated = texnum;
        return { texnum, x, y };
    }
    return { texnum, x, y };
}

void BspRenderer::SetBspData(const bsp::Bsp& bsp)
{
    ReleaseMemory();

    auto br = this;
    br->filename_hash = bsp.filename_hash;
    br->modified_time = bsp.modified_time;

    // Code mainly converted from: https://github.com/joshuaskelly/vgio/blob/master/vgio/quake/bsp/bsp29.py

    for (const auto& miptex : bsp.mip_textures) {
        auto& image = br->images.emplace_back();
        image.width = miptex.data.width;
        image.height = miptex.data.height;
        image.pixels.resize(image.width * image.height);

        for (int y = 0; y < miptex.data.height; y++) {
            for (int x = 0; x < miptex.data.width; x++) {
                int idx = y * miptex.data.width + x;
                unsigned int palindex = miptex.pixels[idx];
                image.pixels[idx] = GetPaletteColor(palindex);
            }
        }
    }

    // TODO: aggregate all faces of a texture into a single DrawCall
    
    int model_idx = 0;
    for (const auto& model : bsp.models) {
        model_idx += 1;
        
        for (int fi = model.first_face; fi < model.first_face + model.num_faces; fi++) {
            const auto& face = bsp.faces.at(fi);
            const auto& tinfo = bsp.tex_infos.at(face.tex_info);
            const auto& miptex = bsp.mip_textures.at(tinfo.miptexture_number);

            static std::vector<Vertex> verts;
            static std::vector<Triangle> tris;
            verts.clear();
            tris.clear();

            Vec3 s = tinfo.s;
            float ds = tinfo.s_offset;
            Vec3 t = tinfo.t;
            float dt = tinfo.t_offset;

            float mins[] = { std::numeric_limits<float>().max(), std::numeric_limits<float>().max() };
            float maxs[] = { -std::numeric_limits<float>().max(), -std::numeric_limits<float>().max() };

            for (int ei = face.first_edge; ei < face.first_edge + face.num_edges; ei++) {
                int surf_edge = bsp.surf_edges.at(ei);

                const auto& edge = bsp.edges.at(std::abs(surf_edge));
                auto v0 = bsp.vertices[edge.v[0]];
                auto v1 = bsp.vertices[edge.v[1]];
                if (surf_edge < 0) {
                    std::swap(v0, v1);
                }

                for (const auto& v : { v0, v1 }) {
                    for (const auto& vt : { std::tuple<Vec3, float, int>{s, ds, 0}, std::tuple<Vec3, float, int>{t, dt, 1} }) {
                        float val = ((double)v.pos[0] * (double)std::get<0>(vt)[0]) +
                            ((double)v.pos[1] * (double)std::get<0>(vt)[1]) +
                            ((double)v.pos[2] * (double)std::get<0>(vt)[2]) +
                            (double)std::get<1>(vt);

                        if (val < mins[std::get<2>(vt)]) {
                            mins[std::get<2>(vt)] = val;
                        }
                        if (val > maxs[std::get<2>(vt)]) {
                            maxs[std::get<2>(vt)] = val;
                        }
                    }
                }

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
                float dot_vs = glm::dot(v.pos, s);
                float dot_vt = glm::dot(v.pos, t);

                Vec2 uv = Vec2{(dot_vs + ds) / float(miptex.data.width), (dot_vt + dt) / float(miptex.data.height)};
                //v.normal = normal;
                v.uv = uv;
            }

            unsigned int start_index = br->vertices.size();
            unsigned int end_index = start_index + verts.size();
            unsigned int v0 = start_index;
            for (unsigned int index = start_index+1; index < end_index - 1; index++) {
                unsigned int v1 = index;
                unsigned int v2 = index + 1;
                tris.push_back({v0, v1, v2});
            }

            // Lightmap stuff
            int lightmap_id = 0;
            {
                int bmins[2];
                int bmaxs[2];
                int sextents[2];
                int stexturemins[2];
                int lmshift = bsp.lightmap_shift;
                int lmscale = 1 << lmshift;
                float maxextent = std::max(LMBLOCK_WIDTH, LMBLOCK_HEIGHT) * lmscale;

                for (int bi = 0; bi < 2; bi++)
                {
                    bmins[bi] = floor(mins[bi] / lmscale);
                    bmaxs[bi] = ceil(maxs[bi] / lmscale);

                    stexturemins[bi] = bmins[bi] * lmscale;
                    sextents[bi] = (bmaxs[bi] - bmins[bi]) * lmscale;

                    constexpr int TEX_SPECIAL = 1;
                    if (!(tinfo.flags & TEX_SPECIAL) && sextents[bi] > maxextent) //johnfitz -- was 512 in glquake, 256 in winquake
                    {
                        sextents[bi] = 1;
                    }
                }

                int smax = (sextents[0] >> lmshift) + 1;
                int tmax = (sextents[1] >> lmshift) + 1;
                int lightmap_s, lightmap_t;
                std::tie(lightmap_id, lightmap_s, lightmap_t) = br->CreateLightmap(smax, tmax);

                if (face.light_ofs != -1) {
                    int size = smax * tmax;
                    const RGB8* samples = bsp.lighting.data() + face.light_ofs;

                    constexpr int MAXLIGHTMAPS = 1; // TODO: support 16 lightmaps
                    constexpr uint8_t INVALID_LIGHTSTYLE = 0xff;

                    std::array<RGB32, LMBLOCK_WIDTH * LMBLOCK_HEIGHT> blocklights;
                    memset(blocklights.data(), 0, sizeof(blocklights));

                    for (int maps = 0; maps < MAXLIGHTMAPS && face.styles[maps] != INVALID_LIGHTSTYLE; maps++) {
                        uint32_t scale = 264;
                        RGB32* bl = blocklights.data();

                        for (int lmi = 0; lmi < size; lmi++) {
                            //*dest++ = { samples->r, samples->g, samples->b, 255 };

                            RGB32* out = bl;
                            out->r += (uint32_t)samples->r * scale;
                            out->g += (uint32_t)samples->g * scale;
                            out->b += (uint32_t)samples->b * scale;

                            bl++;
                            samples++;
                        }
                    }

                    RGB32* bl = blocklights.data();
                    RGBA8* dest = br->lightmaps[lightmap_id].data.data() + (lightmap_t * LMBLOCK_WIDTH + lightmap_s);

                    int stride = LMBLOCK_WIDTH - smax;

                    for (int li = 0; li < tmax; li++, dest += stride)
                    {
                        for (int lj = 0; lj < smax; lj++)
                        {
                            uint32_t r = (uint32_t)bl->r;
                            uint32_t g = (uint32_t)bl->g;
                            uint32_t b = (uint32_t)bl->b;

                            r = std::min(r >> 7, (uint32_t)255);
                            g = std::min(g >> 7, (uint32_t)255);
                            b = std::min(b >> 7, (uint32_t)255);

                            *dest = { uint8_t(r), uint8_t(g), uint8_t(b), 255 };

                            dest++;
                            bl++;
                        }
                    }
                }

                // Set the vertex lightmap coordinates
                for (auto& vert : verts) {
                    float u = glm::dot(vert.pos, s) + ds;
                    u -= stexturemins[0];
                    u += float(lightmap_s) * lmscale;
                    u += lmscale / 2.0f;
                    u /= LMBLOCK_WIDTH * lmscale;

                    float v = glm::dot(vert.pos, t) + dt;
                    v -= stexturemins[1];
                    v += float(lightmap_t) * lmscale;
                    v += lmscale / 2.0f;
                    v /= LMBLOCK_HEIGHT * lmscale;

                    vert.uvlm = { u, v };

                    //vert.uvlm = {float(lightmap_s) / LMBLOCK_WIDTH, float(lightmap_t) / LMBLOCK_HEIGHT};
                }
            }

            // Emit the draw call
            DrawCall dcall = {};
            dcall.start_tri = br->triangles.size();
            dcall.num_tris = tris.size();
            dcall.texture_id = tinfo.miptexture_number;
            dcall.lightmap_id = lightmap_id;

            int key = (tinfo.miptexture_number & 0xFFFF) | ((lightmap_id << 16) & 0xFFFF0000);
            br->draw_calls[key].push_back(dcall);

            for (const auto& v : verts) {
                br->vertices.push_back(v);
            }
            for (const auto& t : tris) {
                br->triangles.push_back(t);
            }
        }
    }

    gpu_data_needs_update = true;
}

bool BspRenderer::UploadGpuData()
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
        if (image.width == 0 || image.height == 0) { 
            textures.push_back(nullptr);
            continue;
        }

        IDirect3DTexture9* texture = nullptr;
        HRESULT hr = g_pd3dDevice->CreateTexture(image.width, image.height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, NULL);
        if (FAILED(hr)) {
            return false;
        }

        D3DLOCKED_RECT locked_rect;
        RECT rect;
        rect.right = image.width;
        rect.bottom = image.height;
        rect.top = 0;
        rect.left = 0;
        if (FAILED(texture->LockRect(0, &locked_rect, &rect, 0))) {
            return false;
        }
        for (unsigned int y = 0; y < image.height; y++) {
            unsigned char* dest = (unsigned char*)locked_rect.pBits + (y * locked_rect.Pitch);
            for (unsigned int x = 0; x < image.width; x++) {
                unsigned int idx = y * image.width + x;
                const auto& pix = image.pixels.at(idx);

                // TODO: wtf? I asked for ARGB

                *dest++ = pix.b;
                *dest++ = pix.g;
                *dest++ = pix.r;
                *dest++ = pix.a;
            }
        }
        texture->UnlockRect(0);
        textures.push_back(texture);
    }

    // Upload lightmaps
    for (auto& lm : lightmaps) {
        IDirect3DTexture9* texture = nullptr;
        HRESULT hr = g_pd3dDevice->CreateTexture(LMBLOCK_WIDTH, LMBLOCK_HEIGHT, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, NULL);
        if (FAILED(hr)) {
            return false;
        }

        D3DLOCKED_RECT locked_rect;
        RECT rect;
        rect.right = LMBLOCK_WIDTH;
        rect.bottom = LMBLOCK_HEIGHT;
        rect.top = 0;
        rect.left = 0;
        if (FAILED(texture->LockRect(0, &locked_rect, &rect, 0))) {
            return false;
        }
        for (unsigned int y = 0; y < LMBLOCK_HEIGHT; y++) {
            unsigned char* dest = (unsigned char*)locked_rect.pBits + (y * locked_rect.Pitch);
            for (unsigned int x = 0; x < LMBLOCK_WIDTH; x++) {
                unsigned int idx = y * LMBLOCK_WIDTH + x;
                const auto& pix = lm.data.at(idx);

                // TODO: wtf? I asked for ARGB

                *dest++ = pix.b;
                *dest++ = pix.g;
                *dest++ = pix.r;
                *dest++ = pix.a;
            }
        }
        texture->UnlockRect(0);
        lightmap_textures.push_back(texture);
    }

    return true;
}

void BspRenderer::Render(const Vec3& cam_pos, const Vec3& cam_rot)
{
    if (gpu_data_needs_update) {
        ReleaseGpuData();
        if (!UploadGpuData()) {
            return;
        }
        gpu_data_needs_update = false;
    }

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);

    Vec3 target_pos = cam_pos + cam_rot * 100.f;// { -100.0f, 0.0f, 50.0f };
    auto projection = glm::perspective(glm::radians(80.0f), 1280.0f / 760.0f, 0.1f, 2000.0f);
    auto view = glm::lookAt(cam_pos, target_pos, Vec3{ 0.0f, 0.0f, 1.0f });
    
    static D3DMATRIX world_matrix = { { { 1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f } } };
    
    D3DMATRIX projection_matrix;
    D3DMATRIX view_matrix;
    memcpy(&projection_matrix, &projection, sizeof(D3DMATRIX));
    memcpy(&view_matrix, &view, sizeof(D3DMATRIX));

    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &projection_matrix);
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &view_matrix);
    g_pd3dDevice->SetTransform(D3DTS_WORLD, &world_matrix);

    g_pd3dDevice->SetStreamSource(0, vertex_buffer, 0, sizeof(Vertex));
    g_pd3dDevice->SetIndices(index_buffer);
    g_pd3dDevice->SetFVF(g_vertex_fvf);

    for (const auto& it : draw_calls) {
        int texture_id = it.first & 0xFFFF;
        int lightmap_id = it.first >> 16 & 0xFFFF;
        const auto& dcs = it.second;
        if (dcs.empty()) { continue; }

        auto tex = textures[texture_id];
        if (!tex) { continue; }

        auto lmtex = lightmap_textures[lightmap_id];

        g_pd3dDevice->SetTexture(0, tex);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);   //Ignored

        g_pd3dDevice->SetTexture(1, lmtex);
        g_pd3dDevice->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_pd3dDevice->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_pd3dDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
        g_pd3dDevice->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_pd3dDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);

        for (const auto& dc : dcs) {
            HRESULT hr = g_pd3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, dc.num_tris * 3, dc.start_tri * 3, dc.num_tris);
            if (FAILED(hr)) {
                continue;
            }
        }
    }
}

void BspRenderer::ReleaseMemory()
{
    images.clear();
    lightmaps.clear();
    draw_calls.clear();
    vertices.clear();
    triangles.clear();
    last_lightmap_allocated = 0;
    memset(lightmap_allocated, 0, sizeof(lightmap_allocated));
}

void BspRenderer::ReleaseGpuData()
{
    if (index_buffer) { index_buffer->Release(); }
    if (vertex_buffer) { vertex_buffer->Release(); }
    for (auto& t : textures) {
        if (t) { t->Release(); }
    }
    for (auto& t : lightmap_textures) {
        if (t) { t->Release(); }
    }
    textures.clear();
    lightmap_textures.clear();
}

BspRenderer* Renderer::GetOrCreateBspRenderer(const bsp::Bsp& bsp)
{
    for (auto& br : bsp_renderers) {
        if (br->filename_hash == bsp.filename_hash) {
            if (br->modified_time != bsp.modified_time) {
                br->SetBspData(bsp);
            }
            return br.get();
        }
    }

    auto newbr = std::make_unique<BspRenderer>();
    newbr->SetBspData(bsp);
    bsp_renderers.push_back(std::move(newbr));
    return bsp_renderers.back().get();
}

void Renderer::RenderBsp(const Vec3& cam_pos, const Vec3& cam_rot, const bsp::Bsp& bsp)
{
    auto br = GetOrCreateBspRenderer(bsp);
    if (!br) { return; }

    br->Render(cam_pos, cam_rot);
}

}