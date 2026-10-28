// Host build: the game's side of the frontend (frontend.h). At each VBlank the frame goes to the AcwwShm block and the
// input comes from it, through the backend: frontend_sdl.c (in-process SDL2, the 64-bit builds) or frontend_viewer.c
// (the 64-bit viewer process, the i386 build). With ACWW_HEADLESS there is no backend at all: no window, no sound
// device, input only from ACWW_KEYS / ACWW_TOUCH.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend.h"
#include "host.h"
#include "host_mem.h"

static AcwwShm *sShm;

#include "render.h"
#include "wgpu_display.h"

void frontend_init(const char *argv0) {
    if (getenv("ACWW_HEADLESS")) return;
    sShm = frontend_backend_open(argv0);
}

AcwwShm *frontend_shm(void) { return sShm; }

// scripted keys for headless runs: ACWW_KEYS="frame:frames:mask;..." (mask: the bits of AcwwShm.keys, hex)
static u32 scripted_keys(void) {
    static const char *spec;
    if (!spec) spec = getenv("ACWW_KEYS") ? getenv("ACWW_KEYS") : "";
    u32 f = (u32)host_frame(), k = 0;
    for (const char *p = spec; *p;) {
        unsigned fr, n, m;
        if (sscanf(p, "%u:%u:%x", &fr, &n, &m) == 3 && f >= fr && f < fr + n) k |= m;
        p = strchr(p, ';');
        if (!p) break;
        p++;
    }
    return k;
}

// at each VBlank: publish the frame, take the input
void frontend_frame(void) {
    u16 keyinput = 0x3ff;  // KEYINPUT: released bits are 1
    // the ARM7's button word (HW_BUTTON_XY_BUF, the system work area), OR-ed into KEYINPUT by PAD_Read: X bit 10, Y bit 11, debug bit 13
    // (released = 1); bit 15 clear = not folded
    u16 xy = 0x2c00;
    if (sShm) {
        // (not when the GPU shows them itself: they would be read back from it)
        if (!render_presenting())
            for (int s = 0; s < 2; s++) memcpy(sShm->pixels + s * 256 * 192, render_screen(s), 256 * 192 * 4);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        sShm->frame++;
        frontend_backend_frame(sShm);
        if (sShm->quit) host_quit();
        u32 k = sShm->keys;
        keyinput = (u16)(~k & 0x3ff);
        xy = (u16)(0x2000 | (~k & 0x0c00));
    }
    u32 sk = scripted_keys();
    keyinput &= (u16)~(sk & 0x3ff);
    xy &= (u16)~(sk & 0x0c00);
    *(u16 *)host_io(0x04000130) = keyinput;
    *(volatile u16 *)&gHostSysWork.buttonXY = xy;
}

// for the touch panel service (pxi7.c): the mouse or a finger on the bottom screen
int frontend_touch(int *x, int *y) {
    if (!sShm || !sShm->touch_down) return 0;
    *x = sShm->touch_x;
    *y = sShm->touch_y;
    return 1;
}

void frontend_shutdown(void) {
    if (sShm) frontend_backend_close();
}
