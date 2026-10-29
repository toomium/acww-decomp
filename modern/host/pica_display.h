// Host build: the screens composited and shown by the 3DS's GPU (pica_display.c, docs/3ds.md T3.3). The 2D renderer
// gives each line's layers and compositing state (gpu2d.h Gpu2dLayers), the 3D engine its picture; here they are put
// together as the DS does, by the GPU, and shown on the 3DS's screens. Plain C types: render.c and the frontend use it.
#ifndef HOST_PICA_DISPLAY_H
#define HOST_PICA_DISPLAY_H

#include <stdint.h>

struct Gpu2dLayers;

int pdisp_init(void);                     // 0 if this build or the GPU can't (then the CPU composites)
void pdisp_attach_screens(void);          // (the frontend, once the 3DS's screens are set up: show on them)
struct Gpu2dLayers *pdisp_layers(void);   // where the 2D renderer puts this frame's layers
// a frame: the layers the 2D renderer made and the 3D picture: a texture the GPU drew (gpu3d.h gpu3d_pica_picture,
// premultiplied alpha), else picture3d (gpu3d_lines' format; NULL: none)
void pdisp_frame(void *texture3d, const uint32_t *picture3d);
// the last frame's screen (0 top, 1 bottom) read back: 256x192, 0x00RRGGBB
const uint32_t *pdisp_screen(int screen);
// The GPU thread (the 3DS's core 2): a job for it, of a kind (done at once where there is none), in turn after the
// others; waiting until the last job of a kind is done (before what it reads changes), or every job (before citro3d
// is used elsewhere); the profile's marker its parts go to (prof.h)
enum { PDISP_3D, PDISP_COMP };
void pdisp_submit(int kind, void (*job)(void *), void *arg);
void pdisp_wait(int kind);
void pdisp_idle(void);
extern volatile int *gPicaProf;

#endif
