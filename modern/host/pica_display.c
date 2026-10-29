// Host build: the screens composited and shown by the 3DS's GPU (pica_display.h; docs/3ds.md T3.3). The 2D
// renderer gives each line's state and the layers it draws itself (gpu2d.h Gpu2dLayers); the GPU draws the others
// from VRAM and puts everything together per run of lines with the same state, as gpu2d.c compose_line does:
//   - the BGs the GPU draws (Gpu2dLineState.gpuBg: text BGs in the standard palette, affine BGs) are textures of the
//     whole BG, whose 8x8 tiles are the GPU's 8x8 texture tiles: a tile is decoded again only when its map entry,
//     its tile data or the palette changed; drawn scrolled (text) or through the affine transformation;
//   - the other BGs and the OBJs come as pixels (the windows applied), uploaded each frame; the OBJs in one texture,
//     their priority in the alpha (an alpha test per priority);
//   - the backdrop, then the layers from the back (priority 3) to the front, the OBJs of a priority in front of its
//     BGs; the stencil buffer says per pixel whether the top layer so far is a 2nd target of the colour effects (bit
//     0) or a 1st target (bit 1), whether the windows disable the effects there (bit 2), which window the pixel is in
//     (bits 3-4, from Gpu2dLayers.region: each layer drawn where its window shows it), and marks a blended pixel
//     while a layer is drawn (bit 5);
//   - alpha blending (BLDCNT effect 1: a 1st target over a 2nd one, eva and evb as the constant blend factors), the
//     3D layer over a 2nd target by its own alpha, then brightness up or down (effect 2, 3) over the 1st targets on
//     top, and master brightness.
// Then each engine's picture is drawn on its screen. Differences from the DS: the colours in the GPU's 8 bits and
// rounding; a 2nd target that was itself blended counts with its blended colour (the DS takes the layer's own);
// semi-transparent and bitmap OBJs (the game uses neither) are drawn opaque; a BG's VRAM and palette as at the
// frame's end (an H-blank change of them in the frame shows in the whole frame).

#include "pica_display.h"
#include "prof.h"

#ifndef __3DS__

int pdisp_init(void) { return 0; }
void pdisp_attach_screens(void) {}
struct Gpu2dLayers *pdisp_layers(void) { return 0; }
void pdisp_frame(void *texture3d, const uint32_t *picture3d) { (void)texture3d, (void)picture3d; }
const uint32_t *pdisp_screen(int screen) { (void)screen; return 0; }
void pdisp_submit(int kind, void (*job)(void *), void *arg) { (void)kind, job(arg); }
void pdisp_wait(int kind, int older) { (void)kind, (void)older; }
void pdisp_idle(void) {}

#else

#include <stdio.h>
#include <stdlib.h>
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
static C3D_Tex sBg[2][4], sObj[2], sRegion[2], s3d;
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

// ---- the GPU thread (docs/3ds.md): the 3D pictures (gpu3d_pica.c) and the compositing on the New 3DS's core 2,
// while the game's thread goes on with the next frame. The jobs in turn, from a short queue: the game's thread waits
// for the last one of a kind before it changes what that reads (render.c: the 3D engine's list and textures for the
// 3D picture, the 2D renderer's layers for the compositing; here and gpu3d_pica.c: the jobs' own inputs), not for
// the others (the game's frames are uneven: it works every third one, the 3D engine swaps then). The 2D layers are
// drawn here too, first: the 2D renderer records each line's state and copies VRAM's 2D banks, the palettes and
// OAM, which the game changes when it likes (Gpu2dFrameInput.deferLayers); the compositing reads those copies.
// citro3d is the GPU thread's then: the game's thread uses it only when that is idle.
// Without core 2 (an Old 3DS; ACWW_PICA_THREAD=0) the jobs are done at once. For the check that a job sees what it
// would at once: ACWW_PICA_DELAY=<ms>, each job waits that long first; ACWW_PICA_HASH=1, a hash of each composited
// picture in the log.
volatile int *gPicaProf = &gHostProf;
enum { QUEUE = 4 };
static Thread sWorker;
static LightLock sLock;  // (over the queue and the counts)
static CondVar sChanged;
static struct { void (*fn)(void *); void *arg; } sQueue[QUEUE];
static u32 sSubmitted, sDone, sLastOf[2][2];  // jobs submitted, done; the numbers of the last two of each kind
static int sDelayMs;

static void worker(void *arg) {
    (void)arg;
    LightLock_Lock(&sLock);
    for (;;) {
        while (sDone == sSubmitted) CondVar_Wait(&sChanged, &sLock);
        void (*fn)(void *) = sQueue[sDone % QUEUE].fn;
        void *a = sQueue[sDone % QUEUE].arg;
        LightLock_Unlock(&sLock);
        if (sDelayMs) svcSleepThread((s64)sDelayMs * 1000000);
        fn(a);
        LightLock_Lock(&sLock);
        sDone++;
        CondVar_WakeUp(&sChanged, ARBITRATION_SIGNAL_ALL);
    }
}
static void start_worker(void) {
    static int tried;
    if (tried++) return;
    const char *e = getenv("ACWW_PICA_THREAD");
    if (e && !atoi(e)) return;
    sDelayMs = getenv("ACWW_PICA_DELAY") ? atoi(getenv("ACWW_PICA_DELAY")) : 0;
    LightLock_Init(&sLock);
    CondVar_Init(&sChanged);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    sWorker = threadCreate(worker, 0, 64 * 1024, prio, 2, true);
    if (sWorker) gPicaProf = &gHostProfWorker;
    fprintf(stderr, "3ds: the GPU work %s\n", sWorker ? "on core 2" : "on the game's core (no core 2)");
}
static void wait_done(int last, int kind, int older) {
    if (!sWorker) return;
    LightLock_Lock(&sLock);
    u32 n = last ? sSubmitted : sLastOf[kind][older];
    if (sDone < n) {
        HOST_PROF(PROF_WAIT);
        while (sDone < n) CondVar_Wait(&sChanged, &sLock);
    }
    LightLock_Unlock(&sLock);
}
void pdisp_wait(int kind, int older) { wait_done(0, kind, older); }
void pdisp_idle(void) { wait_done(1, 0, 0); }
void pdisp_submit(int kind, void (*job)(void *), void *arg) {
    start_worker();
    if (!sWorker) { job(arg); return; }
    LightLock_Lock(&sLock);
    while (sSubmitted - sDone >= QUEUE) CondVar_Wait(&sChanged, &sLock);
    sQueue[sSubmitted % QUEUE].fn = job, sQueue[sSubmitted % QUEUE].arg = arg;
    sLastOf[kind][1] = sLastOf[kind][0];
    sLastOf[kind][0] = ++sSubmitted;
    CondVar_WakeUp(&sChanged, ARBITRATION_SIGNAL_ALL);
    LightLock_Unlock(&sLock);
}

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
        if (!(sLayers.region[e] = linearAlloc(W * H * 4)) || !tex(&sRegion[e], GPU_RGBA8)) return 0;
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

// ---- the BGs drawn from VRAM: a texture of the whole BG each (GBATEK "DS Video BG Modes / Control")
typedef struct {
    int kind, w, h, color256;  // (kind 0: no texture yet)
    u32 frame;                 // the frame it was brought up to date in
    u32 charBase, mapBase;
    C3D_Tex tex;
    u16 map[64 * 64];          // the map entries the texture shows (affine: 8-bit ones)
    u8 chars[0x10000];         // the tile data it was decoded from
    u8 pal[0x200];             // and the BG palette
} BgTex;
static BgTex sBgTex[2][4];
static u32 sFrame;
static u16 sTmpMap[64 * 64];
static u8 sMorton[8][8];       // a texel's place in its 8x8 tile: [y][x]

// len bytes of an engine's BG VRAM from addr (through its 16 KB granules)
static void vram_copy(u8 *dst, const u8 *const *gran, u32 mask, u32 addr, u32 len) {
    while (len) {
        u32 a = addr & mask, in = 0x4000 - (a & 0x3fff), n = in < len ? in : len;
        memcpy(dst, gran[a >> 14] + (a & 0x3fff), n);
        dst += n, addr += n, len -= n;
    }
}
static inline u16 to5551(u16 c) { return (u16)((c & 31) << 11 | (c >> 5 & 31) << 6 | (c >> 10 & 31) << 1 | 1); }
static inline u16 pal(const u8 *p, u32 i) { return (u16)(p[i * 2] | p[i * 2 + 1] << 8); }

// one 8x8 tile of the BG texture from map entry `entry`
static void bg_tile(BgTex *t, int tx, int ty, u16 entry) {
    u16 *dst = (u16 *)t->tex.data + (ty * (t->w / 8) + tx) * 64;
    if (t->kind == 2) {  // affine: 256 colours, no flips
        const u8 *c = t->chars + (entry & 0xff) * 64u;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                u8 i = c[y * 8 + x];
                dst[sMorton[y][x]] = i ? to5551(pal(t->pal, i)) : 0;
            }
        return;
    }
    u32 tile = entry & 0x3ff;
    int fx = entry & 0x400 ? 7 : 0, fy = entry & 0x800 ? 7 : 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int sx = x ^ fx, sy = y ^ fy;
            u32 i;
            if (t->color256) i = t->chars[tile * 64 + (u32)(sy * 8 + sx)];
            else {
                u8 b = t->chars[tile * 32 + (u32)(sy * 4 + sx / 2)];
                i = sx & 1 ? b >> 4 : b & 15;
                if (i) i += (entry >> 12) * 16u;
            }
            dst[sMorton[y][x]] = i ? to5551(pal(t->pal, i)) : 0;
        }
}

// where BG n's tile data (cb) and map (mb) are, its size and colours
static void bg_layout(int e, int n, const Gpu2dLineState *s, int *w, int *h, int *color256, u32 *cb, u32 *mb) {
    u16 cnt = s->bgcnt[n];
    *cb = (cnt >> 2 & 15) * 0x4000u, *mb = (cnt >> 8 & 31) * 0x800u;
    if (e == 0) *cb += (s->dispcnt >> 24 & 7) * 0x10000u, *mb += (s->dispcnt >> 27 & 7) * 0x10000u;
    if (s->bgKind[n] == 1) *w = cnt & 0x4000 ? 512 : 256, *h = cnt & 0x8000 ? 512 : 256, *color256 = (cnt & 0x80) != 0;
    else *w = *h = 128 << (cnt >> 14), *color256 = 1;
}
// the highest tile a map uses (kind 2: 8-bit entries)
static u32 max_tile(const u16 *map, int count, int kind) {
    u32 m = 0;
    for (int i = 0; i < count; i++) {
        u32 tile = kind == 2 ? map[i] : map[i] & 0x3ffu;
        if (tile > m) m = tile;
    }
    return m;
}
// the map's entries into out, as the texture lays them out (32x32-tile blocks of 2 KB, left to right, then down)
static void read_map(u16 *out, const u8 *const *gran, u32 mask, u32 mb, int tw, int th, int kind) {
    if (kind == 2) {
        static u8 m8[128 * 128];
        vram_copy(m8, gran, mask, mb, (u32)(tw * th));
        for (int i = 0; i < tw * th; i++) out[i] = m8[i];
        return;
    }
    for (int by = 0; by < th / 32; by++)
        for (int bx = 0; bx < tw / 32; bx++) {
            static u16 block[32 * 32];
            vram_copy((u8 *)block, gran, mask, mb + (u32)(by * (tw / 32) + bx) * 0x800u, 0x800);
            for (int y = 0; y < 32; y++) memcpy(&out[(by * 32 + y) * tw + bx * 32], &block[y * 32], 64);
        }
}

// BG n of an engine as its texture, brought up to date with VRAM: 0 if it can't be
static BgTex *bg_texture(int e, int n, const Gpu2dLineState *s) {
    BgTex *t = &sBgTex[e][n];
    if (t->kind && t->frame == sFrame) return t;  // (once a frame: VRAM as at its end)
    t->frame = sFrame;
    int kind = s->bgKind[n], w, h, color256;
    u32 cb, mb;
    bg_layout(e, n, s, &w, &h, &color256, &cb, &mb);
    int full = 0;
    if (t->kind != kind || t->w != w || t->h != h) {
        if (t->kind) C3D_TexDelete(&t->tex);
        t->kind = 0;
        if (!C3D_TexInit(&t->tex, (u16)w, (u16)h, GPU_RGBA5551)) return 0;
        C3D_TexSetFilter(&t->tex, GPU_NEAREST, GPU_NEAREST);
        t->tex.border = 0;  // (an affine BG without wrap: transparent outside)
        t->kind = kind, t->w = w, t->h = h;
        full = 1;
    }
    if (t->color256 != color256 || t->charBase != cb || t->mapBase != mb) full = 1;
    t->color256 = color256, t->charBase = cb, t->mapBase = mb;
    const u8 *const *gran = sLayers.bgGran[e];
    u32 mask = sLayers.bgMask[e];
    if (memcmp(t->pal, sLayers.palette[e], 0x200)) full = 1, memcpy(t->pal, sLayers.palette[e], 0x200);
    // the map first: the tiles it uses (up to the highest one) are those to compare
    int tw = w / 8, th = h / 8, drawn = 0;
    u32 tileBytes = color256 ? 64 : 32;
    read_map(sTmpMap, gran, mask, mb, tw, th, kind);
    u32 maxTile = max_tile(sTmpMap, tw * th, kind);
    // the tile data, compared where it is in 1 KB pieces (in one 16 KB granule each: the character base is in 16 KB
    // steps), then the tiles of a piece that differs
    static u8 dirty[1024];
    u32 len = ((maxTile + 1) * tileBytes + 1023) & ~1023u;
    memset(dirty, full, sizeof dirty);
    for (u32 o = 0; o < len; o += 1024) {
        u32 a = (cb + o) & mask;
        const u8 *src = gran[a >> 14] + (a & 0x3fff);
        if (!full && !memcmp(src, t->chars + o, 1024)) continue;
        if (!full)
            for (u32 k = o / tileBytes; k < (o + 1024) / tileBytes && k < 1024; k++)
                dirty[k] = memcmp(src + (k * tileBytes - o), t->chars + k * tileBytes, tileBytes) != 0;
        memcpy(t->chars + o, src, 1024);
    }
    for (int ty = 0; ty < th; ty++)
        for (int tx = 0; tx < tw; tx++) {
            u16 entry = sTmpMap[ty * tw + tx];
            u32 tile = kind == 2 ? entry : entry & 0x3ffu;
            if (full || entry != t->map[ty * tw + tx] || dirty[tile]) {
                bg_tile(t, tx, ty, entry);
                drawn = 1;
            }
            t->map[ty * tw + tx] = entry;
        }
    if (drawn) C3D_TexFlush(&t->tex);
    return t;
}

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
static void stencil(GPU_TESTFUNC f, u32 ref, u32 mask, u32 write) {
    C3D_StencilTest(true, f, (int)ref, (int)mask, (int)write);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
}

// ---- a pass: quads put together, then drawn at once (draws are what costs on the CPU: the GPU's state is set up for
// each one)
static int sMark;
static void begin(void) { sMark = sNV; }
static void end(void) {
    if (sNV > sMark) C3D_DrawArrays(GPU_TRIANGLES, sMark, sNV - sMark);
}
// a quad: its corners (pixels: top left, top right, bottom right, bottom left) and their texture coordinates
static void quad(const float xy[4][2], const float uv[4][2], const float col[4]) {
    if (sNV + 6 > MAX_VERTS) return;
    static const float none[4] = {0, 0, 0, 0};
    if (!col) col = none;
    DVertex q[4];
    for (int i = 0; i < 4; i++) {
        q[i].pos[0] = xy[i][0], q[i].pos[1] = xy[i][1], q[i].pos[2] = 0.5f;
        memcpy(q[i].col, col, sizeof q[i].col);
        q[i].tc[0] = uv[i][0], q[i].tc[1] = uv[i][1];
    }
    DVertex *d = sV + sNV;
    d[0] = q[0], d[1] = q[1], d[2] = q[2], d[3] = q[0], d[4] = q[2], d[5] = q[3];
    sNV += 6;
}
// a rectangle [x0, x1) x [y0, y1) of a texture's pixels from (u0 * size, row ty0) on, the texture `rows` tall (the
// GPU's texture v runs from the bottom: v = 1 is the first row)
static void rect(float x0, float y0, float x1, float y1, float u0, float u1, float ty0, float rows, const float col[4]) {
    if (x1 <= x0 || y1 <= y0) return;
    float v0 = 1.0f - ty0 / rows, v1 = 1.0f - (ty0 + y1 - y0) / rows;
    const float xy[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    quad(xy, uv, col);
}
static void bgr555(u16 c, float a, float out[4]) {
    out[0] = (float)(c & 31) / 31.0f, out[1] = (float)(c >> 5 & 31) / 31.0f, out[2] = (float)(c >> 10 & 31) / 31.0f;
    out[3] = a;
}

// a group of lines with the same state for the GPU (the layers, their order, textures and blending, the windows'
// masks) - one draw per pass; in pieces of lines that differ only in what is vertex data (scroll, the affine
// parameters, the backdrop and brightness): a quad each
enum { MAX_PIECES = H };
typedef struct {
    int engine, n;
    const Gpu2dLineState *s;      // (the group's first line's: the GPU state)
    int y0[MAX_PIECES], y1[MAX_PIECES];
} Group;
static const Gpu2dLineState *piece_state(const Group *g, int i) { return &sLayers.line[g->engine][g->y0[i]]; }

// a layer's texture and its wrapping for the group (0: none)
static BgTex *layer_bind(const Group *g, int l, int is3d) {
    const Gpu2dLineState *s = g->s;
    if (l == 4) { source(&sObj[g->engine]); return 0; }
    if (l == 0 && is3d) { source(s3dTex); return 0; }
    if (!(s->gpuBg >> l & 1)) { source(&sBg[g->engine][l]); return 0; }
    BgTex *b = bg_texture(g->engine, l, s);
    if (!b) return 0;
    int wrap = b->kind == 1 || (s->bgcnt[l] & 0x2000);  // (text BGs repeat; affine ones by BGxCNT bit 13)
    C3D_TexSetWrap(&b->tex, wrap ? GPU_REPEAT : GPU_CLAMP_TO_BORDER, wrap ? GPU_REPEAT : GPU_CLAMP_TO_BORDER);
    source(&b->tex);
    return b;
}
// a layer's quad for piece i
static void layer_quad(const Group *g, int i, int l, int is3d, const BgTex *b) {
    const Gpu2dLineState *s = piece_state(g, i);
    float y0 = (float)g->y0[i], y1 = (float)g->y1[i];
    if (l == 0 && is3d) {  // (the 3D layer scrolled by BG0HOFS; nothing where it is scrolled away)
        float x0 = s->hofs3d < 0 ? (float)-s->hofs3d : 0, x1 = s->hofs3d > 0 ? (float)(W - s->hofs3d) : W;
        rect(x0, y0, x1, y1, (x0 + s->hofs3d) / TEX, (x1 + s->hofs3d) / TEX, y0, TEX, 0);
        return;
    }
    if (!b) {  // (the OBJs, a BG the CPU drew)
        if (l == 4 || !(g->s->gpuBg >> l & 1)) rect(0, y0, W, y1, 0, (float)W / TEX, y0, TEX, 0);
        return;
    }
    if (b->kind == 1) {  // text: scrolled
        float u0 = (float)s->bghofs[l] / b->w, u1 = (float)(s->bghofs[l] + W) / b->w;
        rect(0, y0, W, y1, u0, u1, (float)s->bgvofs[l] + y0, (float)b->h, 0);
        return;
    }
    // affine: at pixel (x, y) the texel ((X + PA x + PB (y - y0)) >> 8, (Y + PC x + PD (y - y0)) >> 8), X, Y the
    // piece's first line's reference point; the corners half a pixel out, so that the pixels' centres sample those
    const int16_t *a = s->affine[l - 2];
    const int32_t *rf = &sLayers.ref[g->engine][g->y0[i]][2 * (l - 2)];
    const float xy[4][2] = {{0, y0}, {W, y0}, {W, y1}, {0, y1}};
    float uv[4][2];
    for (int k = 0; k < 4; k++) {
        float px = xy[k][0] - 0.5f, py = xy[k][1] - 0.5f - y0;
        float tx = ((float)rf[0] + a[0] * px + a[1] * py) / 256.0f, ty = ((float)rf[1] + a[2] * px + a[3] * py) / 256.0f;
        uv[k][0] = tx / b->w, uv[k][1] = 1.0f - ty / b->h;
    }
    quad(xy, uv, 0);
}
static void layer_pass(const Group *g, int l, int is3d, const BgTex *b) {
    begin();
    for (int i = 0; i < g->n; i++) layer_quad(g, i, l, is3d, b);
    end();
}

// one layer of a group: l 0-3 BG (BG0 the 3D layer when is3d), 4 the OBJs of priority prio; where the window regions
// allow it (rref / rmask: the stencil's region bits, 0 / 0 everywhere)
static void layer(const Group *g, int l, int prio, int is3d, u32 rref, u32 rmask) {
    const Gpu2dLineState *s = g->s;
    u32 bld = s->bldcnt, effect = bld >> 6 & 3, first = bld >> l & 1, second = bld >> (8 + l) & 1;
    u32 eva = s->bldalpha & 31, evb = s->bldalpha >> 8 & 31;
    eva = eva > 16 ? 16 : eva, evb = evb > 16 ? 16 : evb;
    if (l == 4) C3D_AlphaTest(true, GPU_EQUAL, (prio + 1) * 32);
    else C3D_AlphaTest(true, GPU_GREATER, 0);
    u32 bits = second | first << 1;
    int blend3d = l == 0 && is3d, alpha = !blend3d && first && effect == 1;
    BgTex *b = layer_bind(g, l, is3d);
    if (!(blend3d || alpha)) {
        stencil(GPU_EQUAL, rref | bits, rmask, 3);
        opaque();
        layer_pass(g, l, is3d, b);
        return;
    }
    // over a 2nd target (stencil bit 0), where the windows allow effects (bit 2 clear; the 3D layer: always):
    // blended, and marked (bit 5); elsewhere as it is; then what this layer is, where it is
    u32 cond = blend3d ? 1 : 5;
    stencil(GPU_EQUAL, rref | 1 | 0x20, rmask | cond, 0x20);
    if (blend3d) {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, s3dPremultiplied ? GPU_ONE : GPU_SRC_ALPHA,
                       GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
    } else {
        // (the constant colour's red, green and blue are eva / 16, its alpha evb / 16: R in the low byte)
        u32 ca = eva * 255 / 16, cb = evb * 255 / 16;
        C3D_BlendingColor(ca | ca << 8 | ca << 16 | cb << 24);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_COLOR, GPU_CONSTANT_ALPHA, GPU_ONE, GPU_ZERO);
    }
    layer_pass(g, l, is3d, b);
    stencil(GPU_EQUAL, rref, rmask | 0x20, 0);
    // (the GPU's premultiplied 3D picture over what is below elsewhere too: its colour alone is darkened where its
    // alpha isn't full)
    if (!(blend3d && s3dPremultiplied)) opaque();
    layer_pass(g, l, is3d, b);
    colour_writes(0);
    stencil(GPU_EQUAL, rref | bits, rmask, 0x23);
    layer_pass(g, l, is3d, b);
    colour_writes(1);
}

static void group(const Group *g) {
    const Gpu2dLineState *s = g->s;
    float white[4] = {1, 1, 1, 1}, col[4];
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    source(0);
    opaque();
    if (s->white) {
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
        begin();
        for (int i = 0; i < g->n; i++) rect(0, (float)g->y0[i], W, (float)g->y1[i], 0, 0, 0, TEX, white);
        end();
        return;
    }
    u32 bld = s->bldcnt, effect = bld >> 6 & 3, dispcnt = s->dispcnt;
    int is3d = g->engine == 0 && (dispcnt & 8), windows = (dispcnt & 0xe000) != 0;
    // the backdrop: 1st / 2nd target as BLDCNT bits 5 and 13; the effects allowed, no window, nothing marked
    stencil(GPU_ALWAYS, (bld >> 13 & 1) | (bld >> 5 & 1) << 1, 0, 0xff);
    begin();
    for (int i = 0; i < g->n; i++) {
        bgr555(piece_state(g, i)->backdrop, 1, col);
        rect(0, (float)g->y0[i], W, (float)g->y1[i], 0, 0, 0, TEX, col);
    }
    end();
    // the windows: each pixel's region (bits 3-4) and whether it allows the effects (bit 2); the layers each region
    // shows (outside, window 0, window 1, the OBJ window: WINOUT, WININ, WININ high, WINOUT high)
    u8 shows[4] = {(u8)(s->winout & 0x3f), (u8)(s->winin & 0x3f), (u8)(s->winin >> 8 & 0x3f), (u8)(s->winout >> 8 & 0x3f)};
    if (windows) {
        colour_writes(0);
        source(&sRegion[g->engine]);
        for (u32 k = 0; k < 4; k++) {
            C3D_AlphaTest(true, GPU_EQUAL, (int)(63 + 64 * k));
            stencil(GPU_ALWAYS, k << 3 | (shows[k] & 0x20 ? 0 : 4), 0, 0x1c);
            begin();
            for (int i = 0; i < g->n; i++)
                rect(0, (float)g->y0[i], W, (float)g->y1[i], 0, (float)W / TEX, (float)g->y0[i], TEX, 0);
            end();
        }
        colour_writes(1);
    }
    for (int p = 3; p >= 0; p--) {
        for (int n = 4; n >= 0; n--) {  // (BG3 to BG0, then the OBJs of this priority in front of them)
            int l = n == 4 ? 4 : 3 - n;
            if (l < 4 && (!(dispcnt & 0x100u << l) || s->prio[l] != p)) continue;
            if (l == 4 && !(dispcnt & 0x1000)) continue;
            // (the layers the CPU drew have the windows applied; the GPU's BGs and the 3D layer are drawn per region)
            int perRegion = windows && l < 4 && ((s->gpuBg >> l & 1) || (l == 0 && is3d));
            if (!perRegion) { layer(g, l, p, is3d, 0, 0); continue; }
            for (u32 k = 0; k < 4; k++)
                if (shows[k] >> l & 1) layer(g, l, p, is3d, k << 3, 0x18);
        }
    }
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    source(0);
    if (effect == 2 || effect == 3) {  // brightness up / down where a 1st target is on top (BLDY per piece)
        stencil(GPU_EQUAL, 2, 6, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
        begin();
        for (int i = 0; i < g->n; i++) {
            u32 evy = piece_state(g, i)->bldy & 31;
            float c = effect == 2 ? 1.0f : 0.0f, f[4] = {c, c, c, (float)(evy > 16 ? 16 : evy) / 16.0f};
            if (evy) rect(0, (float)g->y0[i], W, (float)g->y1[i], 0, 0, 0, TEX, f);
        }
        end();
    }
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);  // master brightness (per piece)
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
    begin();
    for (int i = 0; i < g->n; i++) {
        u32 mb = piece_state(g, i)->masterBright, mode = mb >> 14, k = mb & 31;
        float c = mode == 1 ? 1.0f : 0.0f, f[4] = {c, c, c, (float)(k > 16 ? 16 : k) / 16.0f};
        if ((mode == 1 || mode == 2) && k) rect(0, (float)g->y0[i], W, (float)g->y1[i], 0, 0, 0, TEX, f);
    }
    end();
}

static void upload(void *src, C3D_Tex *t, u32 bytes, GX_TRANSFER_FORMAT fmt) {
    GSPGPU_FlushDataCache(src, bytes);
    HOST_PROF_ON(gPicaProf, PROF_GPU);
    C3D_SyncDisplayTransfer(src, GX_BUFFER_DIM(W, H), t->data, GX_BUFFER_DIM(W, H),
                            kLinearToTiled | GX_TRANSFER_IN_FORMAT(fmt) | GX_TRANSFER_OUT_FORMAT(fmt));
}

// lines y and y + 1 of an engine in one run: the same state, and the affine BGs the GPU draws stepping on as without
// a reload of their reference points (an H-blank write)
static int same_run(int e, int y) {
    const Gpu2dLineState *a = &sLayers.line[e][y], *b = a + 1;
    if (memcmp(a, b, sizeof *a)) return 0;
    for (int i = 0; i < 2; i++)
        if ((a->gpuBg >> (2 + i) & 1) && a->bgKind[2 + i] == 2 &&
            (sLayers.ref[e][y + 1][2 * i] != sLayers.ref[e][y][2 * i] + a->affine[i][1] ||
             sLayers.ref[e][y + 1][2 * i + 1] != sLayers.ref[e][y][2 * i + 1] + a->affine[i][3]))
            return 0;
    return 1;
}

// the state the GPU is set up with for a line: what is not vertex data
static int same_gpu_state(const Gpu2dLineState *a, const Gpu2dLineState *b) {
    return a->dispcnt == b->dispcnt && a->gpuBg == b->gpuBg && !memcmp(a->bgKind, b->bgKind, sizeof a->bgKind) &&
           !memcmp(a->bgcnt, b->bgcnt, sizeof a->bgcnt) && !memcmp(a->prio, b->prio, sizeof a->prio) &&
           a->bldcnt == b->bldcnt && a->bldalpha == b->bldalpha && a->winin == b->winin && a->winout == b->winout &&
           a->white == b->white;
}

// (two frames' arguments in turn, as the 2D renderer's records: the compositing before the last may still run)
static void frame_job(void *arg);
static struct FrameArgs { void *texture3d; const uint32_t *picture3d; int record; } sFrameArgs[2];
static int sHash = -1;
void pdisp_frame(void *texture3d, const uint32_t *picture3d) {
    pdisp_wait(PDISP_COMP, 1);  // (the arguments of the compositing before the last)
    sScreenRead[0] = sScreenRead[1] = 0;
    int record = gpu2d_record();
    sFrameArgs[record] = (struct FrameArgs){texture3d, picture3d, record};
    pdisp_submit(PDISP_COMP, frame_job, &sFrameArgs[record]);
}

static void frame_job(void *arg) {
    const struct FrameArgs *a = arg;
    {  // the layers the 2D renderer recorded (render.c: Gpu2dFrameInput.deferLayers)
        HOST_PROF_ON(gPicaProf, PROF_2D);
        gpu2d_draw_layers(a->record);
    }
    HOST_PROF_ON(gPicaProf, PROF_COMP);
    void *texture3d = a->texture3d;
    const uint32_t *picture3d = a->picture3d;
    sFrame++;
    s3dTex = texture3d ? (C3D_Tex *)texture3d : &s3d;
    s3dPremultiplied = texture3d != 0;
    if (!sMorton[0][1])
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                sMorton[y][x] = (u8)((x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3);
    // the layers the CPU drew this frame, to the GPU's textures; the 3D picture as RGBA8 (its colour straight)
    u32 used[2] = {0, 0}, cpuBg[2] = {0, 0};
    for (int e = 0; e < 2; e++)
        for (int y = 0; y < H; y++)
            if (!sLayers.line[e][y].white) used[e] |= sLayers.line[e][y].dispcnt, cpuBg[e] |= ~sLayers.line[e][y].gpuBg & 15u;
    {
        HOST_PROF_ON(gPicaProf, PROF_GPU);
        C3D_FrameBegin(0);  // (waits for the GPU's work before)
    }
    for (int e = 0; e < 2; e++) {
        for (int n = 0; n < 4; n++)
            if ((used[e] & 0x100u << n) && (cpuBg[e] >> n & 1))
                upload(sLayers.bg[e][n], &sBg[e][n], W * H * 2, GX_TRANSFER_FMT_RGB5A1);
        if (used[e] & 0x1000) upload(sLayers.obj[e], &sObj[e], W * H * 4, GX_TRANSFER_FMT_RGBA8);
        if (used[e] & 0xe000) upload(sLayers.region[e], &sRegion[e], W * H * 4, GX_TRANSFER_FMT_RGBA8);
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
    // each engine's picture, group by group of lines with the same GPU state, in pieces of lines with the same state
    for (int e = 0; e < 2; e++) {
        C3D_RenderTargetClear(sCompTarget[e], C3D_CLEAR_ALL, 0, 0);
        C3D_FrameDrawOn(sCompTarget[e]);
        colour_writes(1);
        projection(TEX, TEX, 0);
        static Group g;
        g.engine = e, g.n = 0, g.s = &sLayers.line[e][0];
        for (int y0 = 0, y1; y0 < H; y0 = y1) {
            for (y1 = y0 + 1; y1 < H && same_run(e, y1 - 1); y1++) {}
            if (g.n && !same_gpu_state(g.s, &sLayers.line[e][y0])) {
                group(&g);
                g.n = 0, g.s = &sLayers.line[e][y0];
            }
            g.y0[g.n] = y0, g.y1[g.n] = y1, g.n++;
        }
        group(&g);
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
        begin();
        rect(x0, y0, x0 + W, y0 + H, 0, (float)W / TEX, 0, TEX, 0);
        end();
    }
    GSPGPU_FlushDataCache(sV, (u32)sNV * sizeof(DVertex));
    C3D_FrameEnd(0);
    if (sHash < 0) sHash = getenv("ACWW_PICA_HASH") && atoi(getenv("ACWW_PICA_HASH"));
    if (sHash) {  // (both engines' pictures read back: FNV-1a)
        u32 hash = 2166136261u;
        for (int e = 0; e < 2; e++) {
            C3D_SyncDisplayTransfer((u32 *)sComp[e].data, GX_BUFFER_DIM(TEX, TEX), sReadback, GX_BUFFER_DIM(TEX, TEX),
                                    kTiledToLinear);
            GSPGPU_InvalidateDataCache(sReadback, TEX * TEX * 4);
            for (int i = 0; i < W * H; i++) hash = (hash ^ sReadback[i]) * 16777619u;
        }
        fprintf(stderr, "pica: picture %u hash %08lx\n", (unsigned)sFrame, (unsigned long)hash);
    }
}

const uint32_t *pdisp_screen(int screen) {
    pdisp_idle();  // (citro3d and the pictures)
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
