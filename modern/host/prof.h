// Host build runtime: ACWW_FPS's profile (hw.c). The part of the program the game's thread is in, read every
// millisecond by a sampler thread; HOST_PROF(part) sets it for the rest of the enclosing block. Whatever isn't marked
// is PROF_GAME: the game with its SDK and the runtime's remaining parts. (Its own header, without host.h's types: the
// 3DS's GPU files include it beside libctru's.)
#ifndef HOST_PROF_H
#define HOST_PROF_H

enum {
    PROF_GAME, PROF_REGS, PROF_DMA, PROF_GEOM, PROF_2D, PROF_3D, PROF_COMP, PROF_SOUND, PROF_FRONT, PROF_PACE,
    PROF_GPU,  // waiting for the GPU (the 3DS)
    PROF_LIGHT, PROF_VERTEX,  // the geometry engine's lighting, and its vertices and polygons (setup, clipping)
    PROF_WAIT,  // waiting for the GPU thread (the 3DS: pica_display.c)
    PROF_IDLE,  // (the GPU thread's marker: nothing to do)
    PROF_N
};
extern volatile int gHostProf;
static inline void host_prof_restore(int *saved) { gHostProf = *saved; }
#define HOST_PROF(part) int hostProfSaved_ __attribute__((cleanup(host_prof_restore))) = gHostProf; gHostProf = (part)
// the GPU thread's own marker, and HOST_PROF for a marker given (*var)
extern volatile int gHostProfWorker;
typedef struct { volatile int *var; int saved; } HostProfSaved;
static inline void host_prof_restore_on(HostProfSaved *s) { *s->var = s->saved; }
#define HOST_PROF_ON(v, part) \
    HostProfSaved hostProfSavedOn_ __attribute__((cleanup(host_prof_restore_on))) = {(v), *(v)}; *(v) = (part)

#endif
