// Host build: the screens composited and shown by the 3DS's GPU (pica_display.h; docs/3ds.md T3.3). The 2D
// renderer gives each line's layers - BG0-3 and the OBJs as pixels, the windows' layer masks applied - and its
// compositing state (gpu2d.h Gpu2dLayers); the GPU puts them together per run of lines with the same state, as
// gpu2d.c compose_line does on the CPU:
//   - the backdrop, then the layers from the back (priority 3) to the front, the OBJs of a priority in front of its
//     BGs (one OBJ texture, its priority in the alpha: an alpha test per priority);
//   - the stencil buffer says per pixel whether the top layer so far is a 2nd target of the colour effects (bit 0),
//     a 1st target (bit 1), and whether the windows disable the effects there (bit 2, from Gpu2dLayers.fx);
//   - alpha blending (BLDCNT effect 1: a 1st target over a 2nd one, eva and evb as the constant blend factors), the
//     3D layer over a 2nd target by its own alpha, then brightness up or down (effect 2, 3) over the 1st targets on
//     top, and master brightness.
// Then each engine's picture is drawn on its screen. Differences from the DS: the colours in the GPU's 8 bits and
// rounding; a 2nd target that was itself blended counts with its blended colour (the DS takes the layer's own);
// semi-transparent and bitmap OBJs (the game uses neither) are drawn opaque.

#include "pica_display.h"

#ifndef __3DS__

int pdisp_init(void) { return 0; }
void pdisp_attach_screens(void) {}
struct Gpu2dLayers *pdisp_layers(void) { return 0; }
void pdisp_frame(void *texture3d, const uint32_t *picture3d) { (void)texture3d, (void)picture3d; }
const uint32_t *pdisp_screen(int screen) { (void)screen; return 0; }

#else

#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <citro3d.h>

#include "gpu2d/gpu2d.h"

extern const u8 pica_display_shbin[];  // (the vertex shader, pica_display.v.pica: picasso and bin2s)
extern const u32 pica_display_shbin_size;

enum { W = 256, H = 192, TEX = 256, MAX_VERTS = 0x8000 };

typedef struct {
    float pos[3];  // pixels
    float col[4];
    float tc[2];
} DVertex;

static DVLB_s *sDvlb;
static shaderProgram_s sProgram;
static int sUProjection;
static DVertex *sV;
static int sNV;

static Gpu2dLayers sLayers;            // its pixel buffers in linear memory (the GPU's transfers read them)
static C3D_Tex sBg[2][4], sObj[2], sFx[2], s3d;
static u32 *s3dStage;                  // the 3D picture as RGBA8, row by row
static C3D_Tex *s3dTex;                // this frame's 3D picture: the GPU's (premultiplied) or s3d
static int s3dPremultiplied;
static C3D_Tex sComp[2];               // each engine's picture
static C3D_RenderTarget *sCompTarget[2], *sScreen[2];
static u32 *sReadback;
static uint32_t sScreenPx[2][W * H];
static int sScreenRead[2];

static const u32 kLinearToTiled = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
                                  GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
static const u32 kTiledToLinear = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                  GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO) |
                                  GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                  GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8);

static int tex(C3D_Tex *t, GPU_TEXCOLOR fmt) {
    if (!C3D_TexInit(t, TEX, TEX, fmt)) return 0;
    C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return 1;
}

int pdisp_init(void) {
    if (sV) return 1;
    gspInit();  // (counted, as gpu3d_pica.c: C3D_Init there, or here first)
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
    sV = linearAlloc(MAX_VERTS * sizeof(DVertex));
    sReadback = linearAlloc(TEX * TEX * 4);
    s3dStage = linearAlloc(W * H * 4);
    sDvlb = DVLB_ParseFile((u32 *)pica_display_shbin, pica_display_shbin_size);
    if (!sV || !sReadback || !s3dStage || !sDvlb || !tex(&s3d, GPU_RGBA8)) return 0;
    shaderProgramInit(&sProgram);
    shaderProgramSetVsh(&sProgram, &sDvlb->DVLE[0]);
    sUProjection = shaderInstanceGetUniformLocation(sProgram.vertexShader, "projection");
    for (int e = 0; e < 2; e++) {
        for (int n = 0; n < 4; n++)
            if (!(sLayers.bg[e][n] = linearAlloc(W * H * 2)) || !tex(&sBg[e][n], GPU_RGBA5551)) return 0;
        if (!(sLayers.obj[e] = linearAlloc(W * H * 4)) || !tex(&sObj[e], GPU_RGBA8)) return 0;
        if (!(sLayers.fx[e] = linearAlloc(W * H * 2)) || !tex(&sFx[e], GPU_RGBA5551)) return 0;
        memset(sLayers.bg[e][0], 0, W * H * 2);
        if (!C3D_TexInitVRAM(&sComp[e], TEX, TEX, GPU_RGBA8)) return 0;
        C3D_TexSetFilter(&sComp[e], GPU_NEAREST, GPU_NEAREST);
        sCompTarget[e] = C3D_RenderTargetCreateFromTex(&sComp[e], GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
        if (!sCompTarget[e]) return 0;
    }
    return 1;
}

void pdisp_attach_screens(void) {
    if (!sV || sScreen[0]) return;
    const u32 out = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    sScreen[0] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);
    sScreen[1] = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, -1);
    if (sScreen[0]) C3D_RenderTargetSetOutput(sScreen[0], GFX_TOP, GFX_LEFT, out);
    if (sScreen[1]) C3D_RenderTargetSetOutput(sScreen[1], GFX_BOTTOM, GFX_LEFT, out);
}

struct Gpu2dLayers *pdisp_layers(void) { return sV ? &sLayers : 0; }

// ---- drawing
static void projection(float w, float h, int tilt) {
    C3D_Mtx m;
    if (tilt) Mtx_OrthoTilt(&m, 0.0f, w, h, 0.0f, 0.0f, 1.0f, true);  // (pixels, y down, on a turned 3DS screen)
    else Mtx_Ortho(&m, 0.0f, w, h, 0.0f, 0.0f, 1.0f, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, sUProjection, &m);
}
// the fragment colour: the texture's or the vertex colour
static void source(C3D_Tex *t) {
    C3D_TexEnv *e = C3D_GetTexEnv(0);
    C3D_TexEnvInit(e);
    C3D_TexEnvSrc(e, C3D_Both, t ? GPU_TEXTURE0 : GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(e, C3D_Both, GPU_REPLACE);
    if (t) C3D_TexBind(0, t);
}
static void opaque(void) { C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO); }
static void colour_writes(int on) { C3D_DepthTest(false, GPU_ALWAYS, on ? GPU_WRITE_COLOR : (GPU_WRITEMASK)0); }
// a rectangle of pixels [x0, x1) x [y0, y1), the texture's u from u0 to u1 and its rows from y0 + dv (the
// GPU's texture v runs from the bottom: v = 1 is the first row)
static void rect(float x0, float y0, float x1, float y1, float u0, float u1, float dv, const float col[4]) {
    if (sNV + 6 > MAX_VERTS || x1 <= x0 || y1 <= y0) return;
    float v0 = 1.0f - (y0 + dv) / TEX, v1 = 1.0f - (y1 + dv) / TEX;
    static const float none[4] = {0, 0, 0, 0};
    if (!col) col = none;
    DVertex q[4] = {{{x0, y0, 0.5f}, {0}, {u0, v0}}, {{x1, y0, 0.5f}, {0}, {u1, v0}},
                    {{x1, y1, 0.5f}, {0}, {u1, v1}}, {{x0, y1, 0.5f}, {0}, {u0, v1}}};
    for (int i = 0; i < 4; i++) memcpy(q[i].col, col, sizeof q[i].col);
    DVertex *d = sV + sNV;
    d[0] = q[0], d[1] = q[1], d[2] = q[2], d[3] = q[0], d[4] = q[2], d[5] = q[3];
    C3D_DrawArrays(GPU_TRIANGLES, sNV, 6);
    sNV += 6;
}
static void bgr555(u16 c, float a, float out[4]) {
    out[0] = (float)(c & 31) / 31.0f, out[1] = (float)(c >> 5 & 31) / 31.0f, out[2] = (float)(c >> 10 & 31) / 31.0f;
    out[3] = a;
}
static void stencil_write(u32 bits, u32 mask) {
    C3D_StencilTest(true, GPU_ALWAYS, (int)bits, 0xff, (int)mask);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
}
static void stencil_test(GPU_TESTFUNC f, u32 ref, u32 mask) {
    C3D_StencilTest(true, f, (int)ref, (int)mask, 0);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
}

// one layer of a run of lines: l 0-3 BG (BG0 the 3D layer when is3d), 4 the OBJs of priority prio
static void layer(int engine, const Gpu2dLineState *s, int y0, int y1, int l, int prio, int is3d) {
    u32 bld = s->bldcnt, effect = bld >> 6 & 3, first = bld >> l & 1, second = bld >> (8 + l) & 1;
    u32 eva = s->bldalpha & 31, evb = s->bldalpha >> 8 & 31;
    eva = eva > 16 ? 16 : eva, evb = evb > 16 ? 16 : evb;
    C3D_Tex *t = l == 4 ? &sObj[engine] : l == 0 && is3d ? s3dTex : &sBg[engine][l];
    source(t);
    if (l == 4) C3D_AlphaTest(true, GPU_EQUAL, (prio + 1) * 32);
    else C3D_AlphaTest(true, GPU_GREATER, 0);
    float x0 = 0, x1 = W, u0 = 0, u1 = (float)W / TEX;
    if (l == 0 && is3d) {  // (the 3D layer scrolled by BG0HOFS; nothing where it is scrolled away)
        x0 = s->hofs3d < 0 ? (float)-s->hofs3d : 0, x1 = s->hofs3d > 0 ? (float)(W - s->hofs3d) : W;
        u0 = (x0 + s->hofs3d) / TEX, u1 = (x1 + s->hofs3d) / TEX;
    }
    u32 bits = second | first << 1;
    int blend3d = l == 0 && is3d, alpha = !blend3d && first && effect == 1;
    if (blend3d || alpha) {
        // over a 2nd target: blended (the 3D layer by its alpha; else eva / evb, where the windows allow effects)
        u32 mask = blend3d ? 1 : 5;
        stencil_test(GPU_EQUAL, 1, mask);
        if (blend3d) {
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, s3dPremultiplied ? GPU_ONE : GPU_SRC_ALPHA,
                           GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
        } else {
            // (the constant colour's red, green and blue are eva / 16, its alpha evb / 16: R in the low byte)
            u32 a = eva * 255 / 16, b = evb * 255 / 16;
            C3D_BlendingColor(a | a << 8 | a << 16 | b << 24);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_COLOR, GPU_CONSTANT_ALPHA, GPU_ONE, GPU_ZERO);
        }
        rect(x0, (float)y0, x1, (float)y1, u0, u1, 0, 0);
        stencil_test(GPU_NOTEQUAL, 1, mask);  // (elsewhere: as it is; the GPU's premultiplied 3D picture over what is
        // below, as its colour alone is darkened where its alpha isn't full)
        if (!(blend3d && s3dPremultiplied)) opaque();
        rect(x0, (float)y0, x1, (float)y1, u0, u1, 0, 0);
        colour_writes(0);  // (then the stencil says what this layer is, where it is)
        stencil_write(bits, 3);
        rect(x0, (float)y0, x1, (float)y1, u0, u1, 0, 0);
        colour_writes(1);
        return;
    }
    stencil_write(bits, 3);
    opaque();
    rect(x0, (float)y0, x1, (float)y1, u0, u1, 0, 0);
}

static void run(int engine, const Gpu2dLineState *s, int y0, int y1) {
    float white[4] = {1, 1, 1, 1}, col[4];
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    source(0);
    opaque();
    if (s->white) {
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
        rect(0, (float)y0, W, (float)y1, 0, 0, 0, white);
        return;
    }
    u32 bld = s->bldcnt, effect = bld >> 6 & 3, dispcnt = s->dispcnt;
    int is3d = engine == 0 && (dispcnt & 8);
    // the backdrop: 1st / 2nd target as BLDCNT bits 5 and 13; the effects allowed (bit 2 clear)
    bgr555(s->backdrop, 1, col);
    stencil_write((bld >> 13 & 1) | (bld >> 5 & 1) << 1, 7);
    rect(0, (float)y0, W, (float)y1, 0, 0, 0, col);
    if (dispcnt & 0xe000) {  // where the windows disable the effects: stencil bit 2
        source(&sFx[engine]);
        C3D_AlphaTest(true, GPU_GREATER, 0);
        colour_writes(0);
        stencil_write(4, 4);
        rect(0, (float)y0, W, (float)y1, 0, (float)W / TEX, 0, 0);
        colour_writes(1);
    }
    for (int p = 3; p >= 0; p--) {
        for (int n = 3; n >= 0; n--)
            if ((dispcnt & 0x100u << n) && s->prio[n] == p) layer(engine, s, y0, y1, n, p, is3d);
        if (dispcnt & 0x1000) layer(engine, s, y0, y1, 4, p, is3d);
    }
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    source(0);
    u32 evy = s->bldy & 31;
    evy = evy > 16 ? 16 : evy;
    if ((effect == 2 || effect == 3) && evy) {  // brightness up / down where a 1st target is on top
        float c = effect == 2 ? 1.0f : 0.0f, f[4] = {c, c, c, (float)evy / 16.0f};
        stencil_test(GPU_EQUAL, 2, 6);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
        rect(0, (float)y0, W, (float)y1, 0, 0, 0, f);
    }
    u32 mb = s->masterBright, mode = mb >> 14, k = mb & 31;
    if ((mode == 1 || mode == 2) && k) {  // master brightness
        float c = mode == 1 ? 1.0f : 0.0f, f[4] = {c, c, c, (float)(k > 16 ? 16 : k) / 16.0f};
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
        rect(0, (float)y0, W, (float)y1, 0, 0, 0, f);
    }
}

static void upload(void *src, C3D_Tex *t, u32 bytes, GX_TRANSFER_FORMAT fmt) {
    GSPGPU_FlushDataCache(src, bytes);
    C3D_SyncDisplayTransfer(src, GX_BUFFER_DIM(W, H), t->data, GX_BUFFER_DIM(W, H),
                            kLinearToTiled | GX_TRANSFER_IN_FORMAT(fmt) | GX_TRANSFER_OUT_FORMAT(fmt));
}

void pdisp_frame(void *texture3d, const uint32_t *picture3d) {
    sScreenRead[0] = sScreenRead[1] = 0;
    s3dTex = texture3d ? (C3D_Tex *)texture3d : &s3d;
    s3dPremultiplied = texture3d != 0;
    // the layers this frame's lines show, to the GPU's textures; the 3D picture as RGBA8 (its colour straight)
    u32 used[2] = {0, 0};
    for (int e = 0; e < 2; e++)
        for (int y = 0; y < H; y++)
            if (!sLayers.line[e][y].white) used[e] |= sLayers.line[e][y].dispcnt;
    C3D_FrameBegin(0);
    for (int e = 0; e < 2; e++) {
        for (int n = 0; n < 4; n++)
            if (used[e] & 0x100u << n) upload(sLayers.bg[e][n], &sBg[e][n], W * H * 2, GX_TRANSFER_FMT_RGB5A1);
        if (used[e] & 0x1000) upload(sLayers.obj[e], &sObj[e], W * H * 4, GX_TRANSFER_FMT_RGBA8);
        if (used[e] & 0xe000) upload(sLayers.fx[e], &sFx[e], W * H * 2, GX_TRANSFER_FMT_RGB5A1);
    }
    if (!texture3d && picture3d && (used[0] & 8)) {
        static u32 a8[32];
        if (!a8[31])
            for (u32 a = 0; a < 32; a++) a8[a] = a * 255 / 31;
        for (int i = 0; i < W * H; i++) {
            u32 c = picture3d[i], r = c & 63, g = c >> 8 & 63, b = c >> 16 & 63;
            s3dStage[i] = (r << 2 | r >> 4) << 24 | (g << 2 | g >> 4) << 16 | (b << 2 | b >> 4) << 8 | a8[c >> 24 & 31];
        }
        upload(s3dStage, &s3d, W * H * 4, GX_TRANSFER_FMT_RGBA8);
    }
    sNV = 0;
    C3D_BindProgram(&sProgram);
    C3D_AttrInfo *ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 4);
    AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2);
    C3D_BufInfo *bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, sV, sizeof(DVertex), 3, 0x210);
    C3D_CullFace(GPU_CULL_NONE);
    for (int i = 1; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_DepthMap(true, -1.0f, 0.0f);
    // each engine's picture, run by run of lines with the same state
    for (int e = 0; e < 2; e++) {
        C3D_RenderTargetClear(sCompTarget[e], C3D_CLEAR_ALL, 0, 0);
        C3D_FrameDrawOn(sCompTarget[e]);
        colour_writes(1);
        projection(TEX, TEX, 0);
        const Gpu2dLineState *L = sLayers.line[e];
        for (int y0 = 0, y1; y0 < H; y0 = y1) {
            for (y1 = y0 + 1; y1 < H && !memcmp(&L[y1], &L[y0], sizeof *L); y1++) {}
            run(e, &L[y0], y0, y1);
        }
    }
    // to the screens, 1:1 in their middle
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    opaque();
    for (int s = 0; s < 2 && sScreen[0]; s++) {
        int e = s == 0 ? sLayers.topEngine : 1 - sLayers.topEngine;
        float sw = s == 0 ? 400.0f : 320.0f, x0 = (sw - W) / 2, y0 = (240 - H) / 2;
        C3D_RenderTargetClear(sScreen[s], C3D_CLEAR_ALL, 0x000000ff, 0);
        C3D_FrameDrawOn(sScreen[s]);
        projection(sw, 240.0f, 1);
        source(&sComp[e]);
        rect(x0, y0, x0 + W, y0 + H, 0, (float)W / TEX, -y0, 0);
    }
    GSPGPU_FlushDataCache(sV, (u32)sNV * sizeof(DVertex));
    C3D_FrameEnd(0);
}

const uint32_t *pdisp_screen(int screen) {
    int s = screen & 1, e = s == 0 ? sLayers.topEngine : 1 - sLayers.topEngine;
    if (!sV) return sScreenPx[s];
    if (!sScreenRead[s]) {
        C3D_SyncDisplayTransfer((u32 *)sComp[e].data, GX_BUFFER_DIM(TEX, TEX), sReadback, GX_BUFFER_DIM(TEX, TEX),
                                kTiledToLinear);
        GSPGPU_InvalidateDataCache(sReadback, TEX * TEX * 4);
        for (int i = 0; i < W * H; i++) sScreenPx[s][i] = sReadback[i] >> 8;  // (RGBA8: r << 24 | g << 16 | b << 8)
        sScreenRead[s] = 1;
    }
    return sScreenPx[s];
}

#endif
