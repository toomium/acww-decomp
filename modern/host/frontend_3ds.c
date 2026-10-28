// Host build: the Nintendo 3DS frontend backend (frontend.h; HOST_FRONTEND_3DS, docs/3ds.md T2.4-T2.6): both DS
// screens 1:1 in the middle of the 3DS's screens (the CPU renderers' pictures), the buttons (the 3DS's are the DS's,
// the circle pad as the D-pad), the touch screen, the sound through the DSP (ndsp; it needs the DSP firmware,
// sdmc:/3ds/dspfirm.cdc, which the user dumps), and the home menu's close.
#if HOST_FRONTEND_3DS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "frontend.h"
#include "pica_display.h"

int render_presenting(void);  // (render.h: not includable beside libctru, whose u32 differs from host.h's)

static AcwwShm sShm;

// ---- sound: wave buffers in the DSP's memory, refilled at each frame from the AcwwShm ring as it plays them
enum { WAVE_BUFS = 4, WAVE_FRAMES = 1024 };
static ndspWaveBuf sWave[WAVE_BUFS];
static int16_t *sWaveData;

static void open_audio(void) {
    if (getenv("ACWW_MUTE")) return;
    if (R_FAILED(ndspInit())) {
        fprintf(stderr, "frontend: no sound: the DSP firmware (sdmc:/3ds/dspfirm.cdc) is missing\n");
        return;
    }
    sWaveData = linearAlloc(WAVE_BUFS * WAVE_FRAMES * 4);
    if (!sWaveData) { ndspExit(); return; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, ACWW_AUDIO_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    for (int i = 0; i < WAVE_BUFS; i++) {
        sWave[i].data_pcm16 = sWaveData + i * WAVE_FRAMES * 2;
        sWave[i].status = NDSP_WBUF_DONE;  // (free to be filled)
    }
}

// a played buffer is filled when a whole buffer's sound is waiting: the ring holds the rest meanwhile
static void feed_audio(AcwwShm *shm) {
    if (!sWaveData) return;
    uint32_t r = shm->audio_read, w = __atomic_load_n(&shm->audio_write, __ATOMIC_ACQUIRE);
    if (w - r > ACWW_AUDIO_FRAMES / 2) r = w - WAVE_FRAMES;  // far behind (a stall): skip ahead
    for (int i = 0; i < WAVE_BUFS && w - r >= WAVE_FRAMES; i++) {
        ndspWaveBuf *b = &sWave[i];
        if (b->status != NDSP_WBUF_DONE && b->status != NDSP_WBUF_FREE) continue;
        for (int k = 0; k < WAVE_FRAMES; k++, r++) {
            b->data_pcm16[k * 2] = shm->audio[(r % ACWW_AUDIO_FRAMES) * 2];
            b->data_pcm16[k * 2 + 1] = shm->audio[(r % ACWW_AUDIO_FRAMES) * 2 + 1];
        }
        b->nsamples = WAVE_FRAMES;
        DSP_FlushDataCache(b->data_pcm16, WAVE_FRAMES * 4);
        ndspChnWaveBufAdd(0, b);
    }
    __atomic_store_n(&shm->audio_read, r, __ATOMIC_RELEASE);
}

// ---- picture: the framebuffers are BGR8 and turned by 90 degrees (column-major, 240 pixels a column, bottom up)
static void show(gfxScreen_t screen, const uint32_t *px) {
    u16 h, w;  // (the framebuffer's own width and height: 240 and the screen's width)
    u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &h, &w);
    int x0 = (w - 256) / 2, y0 = (240 - 192) / 2;
    for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 256; x++) {
            uint32_t c = px[y * 256 + x];
            u8 *d = fb + ((x0 + x) * 240 + (239 - (y0 + y))) * 3;
            d[0] = (u8)c, d[1] = (u8)(c >> 8), d[2] = (u8)(c >> 16);
        }
    }
}

AcwwShm *frontend_backend_open(const char *argv0) {
    (void)argv0;
    gfxInitDefault();
    pdisp_attach_screens();  // (when the GPU composites, it shows the screens)
    sShm.magic = ACWW_SHM_MAGIC;
    open_audio();
    return &sShm;
}

void frontend_backend_frame(AcwwShm *shm) {
    if (!aptMainLoop()) shm->quit = 1;  // (the home menu's close; it also handles the home button and sleep)
    hidScanInput();
    u32 k = hidKeysHeld();
    // frontend.h's bits are the 3DS's (and the DS's) first 12: A B Select Start Right Left Up Down R L X Y
    uint32_t keys = k & 0xfff;
    if (k & KEY_CPAD_RIGHT) keys |= KEY_DRIGHT;
    if (k & KEY_CPAD_LEFT) keys |= KEY_DLEFT;
    if (k & KEY_CPAD_UP) keys |= KEY_DUP;
    if (k & KEY_CPAD_DOWN) keys |= KEY_DDOWN;
    shm->keys = keys;
    // the bottom screen's 320x240 to the DS's 256x192 in its middle; a touch outside it is none
    touchPosition t;
    hidTouchRead(&t);
    int tx = t.px - 32, ty = t.py - 24;
    shm->touch_down = (k & KEY_TOUCH) && tx >= 0 && tx < 256 && ty >= 0 && ty < 192;
    if (shm->touch_down) shm->touch_x = tx, shm->touch_y = ty;
    if (!render_presenting()) {  // (else the GPU has shown them: pica_display.c)
        show(GFX_TOP, shm->pixels);
        show(GFX_BOTTOM, shm->pixels + 256 * 192);
        gfxFlushBuffers();
        gfxSwapBuffers();
    }
    feed_audio(shm);
}

void frontend_backend_close(void) {
    if (sWaveData) { ndspExit(); linearFree(sWaveData); sWaveData = 0; }
    gfxExit();
}

#endif
