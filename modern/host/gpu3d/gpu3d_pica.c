// The own 3D engine's rasteriser on the 3DS's GPU (PICA200, citro3d; docs/3ds.md T3.2): the polygon list the
// geometry engine made (gpu3d_geom.c) drawn at the DS's resolution, then the colour, depth and fog flags read back
// into the software rasteriser's buffers (gpu3d_raster.c), whose fog pass and the 2D renderer's composite follow as
// for its own pictures. Not pixel-exact: the GPU's rasterisation, interpolation and 8-bit colour arithmetic.
//   - textures: decoded on the CPU with the rasteriser's texel fetch (g3_texture_rgba) into RGBA8 in the GPU's tiled
//     layout, cached by address, size, format and palette until the VRAM under them changes (4 KB pieces)
//   - modulation and decal in the texture combiners; the colour premultiplied by alpha (a second stage), so that
//     blending over a transparent pixel keeps the polygon's own colour, as on the DS; alpha blended with max
//   - the translucent polygon ID rule in the stencil buffer's bits 0-6 (as gpu3d_wgpu.c), the fog flag in its bit 7:
//     opaque pixels set theirs, translucent ones clear it when they have none
//   - alpha test, depth test less or less-equal (attr bit 14), translucent depth update (attr bit 11)
//   - the Z-buffer (linear in screen space: z * w through the perspective divide) and the W-buffer (the GPU's W-buffer
//     mode: the depth interpolated perspective-correct, not divided)
// Not done: anti-aliasing (the GPU has no coverage), and what the game doesn't use (toon / highlight, shadows, edge marking, wireframe, the rear plane).

#include "gpu3d_internal.h"

#ifndef __3DS__

int g3p_init(void) { return 0; }
void g3p_render(const G3RenderState *r, const u8 *img, const u8 *pal, int texChanged, u32 *color, u32 *depth, u8 *flags) {
    (void)r, (void)img, (void)pal, (void)texChanged, (void)color, (void)depth, (void)flags;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>
#include <citro3d.h>

extern const u8 gpu3d_pica_shbin[];  // (the vertex shader, gpu3d_pica.v.pica: picasso and bin2s)
extern const u32 gpu3d_pica_shbin_size;

enum { W = 256, H = 192, IMG_SIZE = 0x80000, PAL_SIZE = 0x20000, PIECE = 0x1000 };
enum { MAX_VERTS = MAX_POLYGONS * 24 };  // (a polygon of up to 10 vertices as up to 8 triangles)

typedef struct {
    float pos[4];  // clip position: (x, y, -Z, 1) * W, or (x * W, y * W, -Z, W) for the W-buffer (depth 0 to -W)
    float col[4];  // colour (6 bits) and the polygon's alpha (5 bits), 0-1
    float tc[2];   // texture coordinates, 0-1 over the texture
} PVertex;

static DVLB_s *sDvlb;
static shaderProgram_s sProgram;
static C3D_RenderTarget *sTarget;
static PVertex *sVerts;
static u32 *sColorOut, *sDepthOut;  // the read-back pictures (linear memory)

// ---- the texture cache
enum { NTEX = 256 };
typedef struct {
    u32 key, pal;  // texparam without the wrap bits, texpal (palette formats)
    u32 ver;       // the newest VRAM piece version under it when it was decoded
    u32 used;      // the frame it was last used in
    int live;
    C3D_Tex tex;
} TexEntry;
static TexEntry sTex[NTEX];
static u32 sFrame;
// a version per 4 KB piece of the texture image and palette slots, bumped when the piece's bytes change
static u8 sImgCopy[IMG_SIZE], sPalCopy[PAL_SIZE];
static u32 sImgVer[IMG_SIZE / PIECE], sPalVer[PAL_SIZE / PIECE], sVer;

static void track(const u8 *src, u8 *copy, u32 *ver, u32 size) {
    for (u32 o = 0; o < size; o += PIECE)
        if (memcmp(copy + o, src + o, PIECE)) {
            memcpy(copy + o, src + o, PIECE);
            ver[o / PIECE] = ++sVer;
        }
}
static u32 newest(const u32 *ver, u32 size, u32 start, u32 len) {
    u32 v = 0;
    if (!len) return 0;
    for (u32 p = start / PIECE, n = 0; n <= len / PIECE + 1 && n < size / PIECE; p = (p + 1) % (size / PIECE), n++)
        if (ver[p] > v) v = ver[p];
    return v;
}
// the newest version of the VRAM a texture reads (GBATEK "DS 3D Texture Formats": the image, 4x4's second slot,
// the palette)
static u32 texture_version(u32 tp, u32 tpal) {
    int fmt = tp >> 26 & 7, w = 8 << (tp >> 20 & 7), h = 8 << (tp >> 23 & 7);
    static const u8 bits[8] = {0, 8, 2, 4, 8, 2, 8, 16};
    static const u16 colours[8] = {0, 32, 4, 16, 256, 0, 8, 0};
    u32 base = (tp & 0xffff) << 3 & (IMG_SIZE - 1);
    u32 v = newest(sImgVer, IMG_SIZE, base, (u32)(w * h * bits[fmt] / 8));
    if (fmt == 5) {  // (the 4x4 blocks' palette info in slot 1, and palettes anywhere)
        u32 slot1 = 0x20000 + ((base & 0x1ffff) >> 1) + ((base >> 17) == 2 ? 0x10000 : 0);
        u32 i = newest(sImgVer, IMG_SIZE, slot1, (u32)(w * h / 8));
        u32 p = newest(sPalVer, PAL_SIZE, 0, PAL_SIZE);
        return v > i ? (v > p ? v : p) : (i > p ? i : p);
    }
    if (colours[fmt]) {
        u32 pb = fmt == 2 ? (tpal & 0x1fff) << 3 : (tpal & 0x1fff) << 4;
        u32 p = newest(sPalVer, PAL_SIZE, pb & (PAL_SIZE - 1), colours[fmt] * 2u);
        if (p > v) v = p;
    }
    return v;
}

// row-major RGBA8 into the GPU's layout: 8x8 tiles, row by row, the texels in a tile in Morton (Z) order
static void tile(const u32 *src, u32 *dst, int w, int h) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int m = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
            dst[((y >> 3) * (w >> 3) + (x >> 3)) * 64 + m] = src[y * w + x];
        }
}

static C3D_Tex *texture(u32 tp, u32 tpal) {
    u32 key = tp & 0x3ff0ffff, pal = (tp >> 26 & 7) == 7 ? 0 : tpal & 0x1fff;
    u32 ver = texture_version(tp, tpal);
    TexEntry *e = 0, *lru = 0;
    for (int i = 0; i < NTEX; i++) {
        TexEntry *t = &sTex[i];
        if (t->live && t->key == key && t->pal == pal) { e = t; break; }
        if (!lru || !t->live || (lru->live && t->used < lru->used)) lru = t;
    }
    if (e && e->ver >= ver) {
        e->used = sFrame;
        return &e->tex;
    }
    int w = 8 << (tp >> 20 & 7), h = 8 << (tp >> 23 & 7);
    if (!e) {  // (a new one, in place of the one unused for longest)
        e = lru;
        if (e->live) C3D_TexDelete(&e->tex);
        e->live = 0;
        if (!C3D_TexInit(&e->tex, (u16)w, (u16)h, GPU_RGBA8)) return 0;
        C3D_TexSetFilter(&e->tex, GPU_NEAREST, GPU_NEAREST);
        e->live = 1, e->key = key, e->pal = pal;
    }
    u32 *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return 0;
    g3_texture_rgba(tp, tpal, rgba);
    tile(rgba, e->tex.data, w, h);
    free(rgba);
    C3D_TexFlush(&e->tex);
    e->ver = ver, e->used = sFrame;
    return &e->tex;
}

static GPU_TEXTURE_WRAP_PARAM wrap(u32 tp, int axis) {
    if (!(tp >> (16 + axis) & 1)) return GPU_CLAMP_TO_EDGE;
    return tp >> (18 + axis) & 1 ? GPU_MIRRORED_REPEAT : GPU_REPEAT;
}

// ---- set-up
int g3p_init(void) {
    if (sTarget) return 1;
    gspInit();  // (counted: the frontend's gfxInit may come before or after)
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4)) return 0;
    sTarget = C3D_RenderTargetCreate(W, H, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    sVerts = linearAlloc(MAX_VERTS * sizeof(PVertex));
    sColorOut = linearAlloc(W * H * 4);
    sDepthOut = linearAlloc(W * H * 4);
    sDvlb = DVLB_ParseFile((u32 *)gpu3d_pica_shbin, gpu3d_pica_shbin_size);
    if (!sTarget || !sVerts || !sColorOut || !sDepthOut || !sDvlb) return 0;
    shaderProgramInit(&sProgram);
    shaderProgramSetVsh(&sProgram, &sDvlb->DVLE[0]);
    return 1;
}

// ---- a frame
typedef struct {
    C3D_Tex *tex;
    u32 wrap;
    int mode;  // 0 colour only, 1 modulation, 2 decal
    int trans, blend, depthEq, depthWrite, fog, id, wbuffer;
} State;

static void apply(const State *s, const G3RenderState *r) {
    C3D_TexEnv *e = C3D_GetTexEnv(0);
    C3D_TexEnvInit(e);
    if (s->mode == 0) {
        C3D_TexEnvSrc(e, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
    } else if (s->mode == 1) {
        C3D_TexEnvSrc(e, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(e, C3D_Both, GPU_MODULATE);
    } else {  // decal: the texture over the vertex colour by the texel's alpha, the polygon's alpha
        C3D_TexEnvSrc(e, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_TEXTURE0);
        C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(e, C3D_RGB, GPU_INTERPOLATE);
        C3D_TexEnvSrc(e, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(e, C3D_Alpha, GPU_REPLACE);
    }
    e = C3D_GetTexEnv(1);  // the colour premultiplied by alpha
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_RGB, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(e, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(e, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
    C3D_TexEnvFunc(e, C3D_Alpha, GPU_REPLACE);
    if (s->tex) {
        C3D_TexSetWrap(s->tex, (GPU_TEXTURE_WRAP_PARAM)(s->wrap & 0xff), (GPU_TEXTURE_WRAP_PARAM)(s->wrap >> 8));
        C3D_TexBind(0, s->tex);
    }
    // alpha 0 never drawn; DISP3DCNT bit 2: only above ALPHA_TEST_REF (5 bits; the GPU's 8 keep the order)
    C3D_AlphaTest(true, GPU_GREATER, r->dispcnt & 4 ? (r->alphaRef * 255 + 15) / 31 : 0);
    // depth = -z / w, or -z for the W-buffer: the DS's 0-1, nearer smaller (not reversed as citro3d's own set-up: the
    // GPU computes depth in 24-bit floats, which are much finer near 0 than near 1, and the DS's depths are small)
    C3D_DepthMap(!s->wbuffer, -1.0f, 0.0f);
    C3D_DepthTest(true, s->depthEq ? GPU_LEQUAL : GPU_LESS, s->depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
    if (s->trans) {
        // not over its own ID (bits 0-6), which it then writes; the fog flag cleared unless it has fog
        C3D_StencilTest(true, GPU_NOTEQUAL, 0x40 | s->id, 0x7f, s->fog ? 0x7f : 0xff);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_MAX, GPU_ONE, s->blend ? GPU_ONE_MINUS_SRC_ALPHA : GPU_ZERO, GPU_ONE,
                       GPU_ONE);
    } else {
        C3D_StencilTest(true, GPU_ALWAYS, s->fog ? 0x80 : 0, 0xff, 0x80);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
}

static u32 rgb5_to_6(u32 c) { return c ? c * 2 + 1 : 0; }

void g3p_render(const G3RenderState *r, const u8 *img, const u8 *pal, int texChanged, u32 *color, u32 *depth, u8 *flags) {
    sFrame++;
    if (texChanged) {
        track(img, sImgCopy, sImgVer, IMG_SIZE);
        track(pal, sPalCopy, sPalVer, PAL_SIZE);
    }
    u32 cc = r->clearColor, ca = r->clearAttrHi & 31;
    u32 cr = rgb5_to_6(cc & 31), cg = rgb5_to_6(cc >> 5 & 31), cb = rgb5_to_6(cc >> 10 & 31);
    u32 a8 = ca * 255 / 31, cd = (u32)(r->clearDepth & 0x7fff) * 0x200 + ((r->clearDepth & 0x7fff) == 0x7fff ? 0x1ff : 0);

    C3D_FrameBegin(0);  // (not paced to the display: hw.c paces the frames)
    // (the colour premultiplied as the polygons')
    C3D_RenderTargetClear(sTarget, C3D_CLEAR_ALL,
                          (cr * 255 / 63 * a8 / 255) << 24 | (cg * 255 / 63 * a8 / 255) << 16 |
                              (cb * 255 / 63 * a8 / 255) << 8 | a8,
                          cd | (cc & 0x8000 ? 0x80u : 0) << 24);
    C3D_FrameDrawOn(sTarget);
    C3D_BindProgram(&sProgram);
    C3D_AttrInfo *ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 4);
    AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2);
    C3D_BufInfo *bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, sVerts, sizeof(PVertex), 3, 0x210);
    C3D_CullFace(GPU_CULL_NONE);      // (the geometry engine culled already)
    for (int i = 2; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));

    int nv = 0, first = 0;
    State cur;
    int have = 0;
    for (int k = 0; k < r->npoly; k++) {
        const G3Polygon *p = r->poly[k];
        if (p->nv < 3 || nv + (p->nv - 2) * 3 > MAX_VERTS) continue;
        State s;
        memset(&s, 0, sizeof s);
        int fmt = p->texparam >> 26 & 7;
        if (fmt && (r->dispcnt & 1)) s.tex = texture(p->texparam, p->texpal);
        s.mode = !s.tex ? 0 : (p->attr >> 4 & 3) == 1 ? 2 : 1;
        if (s.tex) s.wrap = (u32)wrap(p->texparam, 0) | (u32)wrap(p->texparam, 1) << 8;
        s.trans = p->translucent;
        s.blend = (r->dispcnt & 8) != 0;
        s.depthEq = (p->attr & 0x4000) != 0;
        s.depthWrite = !p->translucent || (p->attr & 0x800);
        s.fog = p->attr >> 15 & 1;
        s.id = (int)(p->attr >> 24 & 63);
        s.wbuffer = p->wbuffer;
        if (!have || memcmp(&s, &cur, sizeof s)) {
            if (nv > first) C3D_DrawArrays(GPU_TRIANGLES, first, nv - first);
            first = nv;
            apply(&s, r);
            cur = s;
            have = 1;
        }
        u32 pa = p->attr >> 16 & 31;
        float alpha = (float)(pa ? pa : 31) / 31.0f;
        float tw = s.tex ? 16.0f * (float)(8 << (p->texparam >> 20 & 7)) : 1.0f;
        float th = s.tex ? 16.0f * (float)(8 << (p->texparam >> 23 & 7)) : 1.0f;
        PVertex pv[10];
        for (int i = 0; i < p->nv; i++) {
            const G3Vertex *v = p->v[i];
            float w = (float)(p->w[i] > 0 ? p->w[i] : 1);
            float x = (float)v->hx / 16.0f / W * 2.0f - 1.0f, y = 1.0f - (float)v->hy / 16.0f / H * 2.0f;
            // (depth test "equal" passes within 0x200 on the DS, gpu3d_raster.c depth_pass: moved nearer by that, then
            // less or equal)
            float z = ((float)p->z[i] - (s.depthEq ? 0x200 : 0)) / 16777215.0f;
            z = z < 0.0f ? 0.0f : z > 1.0f ? 1.0f : z;
            PVertex *o = &pv[i];
            o->pos[0] = x * w, o->pos[1] = y * w, o->pos[2] = s.wbuffer ? -z : -z * w, o->pos[3] = w;
            for (int c = 0; c < 3; c++) o->col[c] = (float)(v->color[c] >> 3) / 63.0f;
            o->col[3] = alpha;
            // (v from the top: the GPU's v = 1 is the texture's first row in memory, as tile() lays it out)
            o->tc[0] = (float)v->tex[0] / tw, o->tc[1] = 1.0f - (float)v->tex[1] / th;
        }
        for (int i = 1; i + 1 < p->nv; i++) sVerts[nv++] = pv[0], sVerts[nv++] = pv[i], sVerts[nv++] = pv[i + 1];
    }
    if (nv > first) C3D_DrawArrays(GPU_TRIANGLES, first, nv - first);
    GSPGPU_FlushDataCache(sVerts, (u32)nv * sizeof(PVertex));
    // the pictures back to linear memory (the transfers wait for the drawing before them, and for themselves)
    u32 flags3 = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_SyncDisplayTransfer(sTarget->frameBuf.colorBuf, GX_BUFFER_DIM(W, H), sColorOut, GX_BUFFER_DIM(W, H), flags3);
    C3D_SyncDisplayTransfer(sTarget->frameBuf.depthBuf, GX_BUFFER_DIM(W, H), sDepthOut, GX_BUFFER_DIM(W, H), flags3);
    C3D_FrameEnd(0);
    GSPGPU_InvalidateDataCache(sColorOut, W * H * 4);
    GSPGPU_InvalidateDataCache(sDepthOut, W * H * 4);

    // into the rasteriser's formats: the colour back from premultiplied, 6 bits; alpha 5 bits; depth 24 bits. (A
    // reciprocal per alpha: the ARM11 has no divide instruction.)
    static u32 recip[256], alpha5[256];
    if (!recip[1])
        for (u32 a = 1; a < 256; a++) recip[a] = ((63u << 16) + a / 2) / a, alpha5[a] = (a * 31 + 127) / 255;
    u32 clear = cr | cg << 8 | cb << 16;
    for (int i = 0; i < W * H; i++) {
        u32 px = sColorOut[i], a = px & 0xff, c = clear;
        if (a) {
            u32 k = recip[a], r = ((px >> 24) * k + 0x8000) >> 16, g = ((px >> 16 & 0xff) * k + 0x8000) >> 16,
                b = ((px >> 8 & 0xff) * k + 0x8000) >> 16;
            c = (r > 63 ? 63 : r) | (g > 63 ? 63 : g) << 8 | (b > 63 ? 63 : b) << 16;
        }
        color[i] = c | alpha5[a] << 24;
        u32 d = sDepthOut[i];
        depth[i] = d & 0xffffff;
        flags[i] = (u8)(d >> 31);
    }
}

#endif
