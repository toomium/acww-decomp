#ifndef N3DS_UI_H
#define N3DS_UI_H

// The enhanced 3DS port (N3DS_DIRECT, docs/3ds-real-port.md R5.3): the UI at the wide screens' edges. The game says
// which OBJs, BGs and windows make a UI element and where it belongs; the host draws the same pixels moved by the
// margins' width (modern/host/gpu2d/gpu2d.h, pica_display.c). Self-contained (no includes).
#ifdef N3DS_DIRECT

#ifdef __cplusplus
extern "C" {
#endif

// an anchor: the edges (as gpu2d.h's GPU2D_*), or N3DS_UI_NEAR(id) (id 0-7): towards the edges the element (its OBJs
// with this anchor) is in the outer third of, as the host finds each frame
enum { N3DS_UI_LEFT = 1, N3DS_UI_RIGHT = 2, N3DS_UI_TOP = 4, N3DS_UI_BOTTOM = 8 };
#define N3DS_UI_NEAR(id) (0x10 | (id) << 5)

// the OAM entries drawn into mode's buffer (Oam_UseBufferA) since Oam_Count gave `from`: anchored
// (src/main/unk_02087e70.cpp; given to the host with the buffers at the V-blank)
int Oam_Count(int mode);
void Oam_Anchor(int mode, int from, unsigned anchor);
// BGs (bit n: BGn) and windows (bit w: window w) of an engine (0 main, 1 sub) anchored from the next frame on, until
// changed (bgs 0: none)
void gpu2d_bg_anchor(int engine, int bgs, int windows, int anchor);
// the text box's BG shown (TalkFrame_ShowBg / HideBg, src/main/unk_02065e88.cpp): the choices go with it
extern int gN3dsTalkBgShown;

#ifdef __cplusplus
}
#endif

#endif
#endif
