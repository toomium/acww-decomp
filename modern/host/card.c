// Host build: the ARM9's game card registers. The SDK reads the ROM itself (CARD_ReadRom): an 8-byte command at
// 0x040001a8, ROMCTRL (0x040001a4) bit 31 starts it, the data comes 4 bytes at a time from 0x04100010 (by the CPU
// or by DMA in card mode). Command 0xB7 reads 0x200 bytes at a ROM address; the others return 0xffffffff.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "plat.h"

#define ROMCTRL 0x040001a4u
static FILE *sRom;
static u32 sRomSize;
static u8 sHeader[0x200];
static const char *sRomPath;  // (host_rom_path: pxi7.c's default save file)
static u32 sAddr, sLeft, sIsData;

// The image read from the file as the game asks for it, through a cache of one block: ACWW's is 32 MB, too much to
// hold on the 3DS beside the program (docs/3ds.md D4); the game reads its files in runs of 512-byte pages.
enum { BLOCK = 0x8000 };
static u8 sBlock[BLOCK];
static u32 sBlockAddr = ~0u;
static u8 rom_byte(u32 a) {
    if (a >= sRomSize) return 0xff;
    if ((a & ~(u32)(BLOCK - 1)) != sBlockAddr) {
        sBlockAddr = a & ~(u32)(BLOCK - 1);
        memset(sBlock, 0xff, BLOCK);
        if (fseek(sRom, (long)sBlockAddr, SEEK_SET) != 0 || (fread(sBlock, 1, BLOCK, sRom) == 0 && ferror(sRom)))
            host_fatal("the ROM file can't be read");
    }
    return sBlock[a & (BLOCK - 1)];
}

void card_init(const char *path) {
    sRomPath = path;
    sRom = fopen(path, "rb");
    if (!sRom) { perror(path); exit(1); }
    if (fseek(sRom, 0, SEEK_END) != 0) { perror(path); exit(1); }
    long n = ftell(sRom);
    if (n < (long)sizeof sHeader) host_fatal("the ROM file is too small");
    sRomSize = (u32)n;
    for (u32 i = 0; i < sizeof sHeader; i++) sHeader[i] = rom_byte(i);
}

const u8 *host_rom(u32 *size) { *size = sizeof sHeader; return sHeader; }  // (the header: main.c)
const char *host_rom_path(void) { return sRomPath; }

void host_card_dma(void);

void card_control_written(u32 ctrl) {
    if (!(ctrl >> 31)) return;
    const u8 *cmd = host_io(0x040001a8);
    u32 blk = ctrl >> 24 & 7;
    sLeft = blk == 0 ? 0 : blk == 7 ? 4 : 0x100u << blk;
    sIsData = cmd[0] == 0xb7;
    sAddr = (u32)cmd[1] << 24 | cmd[2] << 16 | cmd[3] << 8 | cmd[4];
    if (sLeft == 0) { *(u32 *)host_io(ROMCTRL) = ctrl & ~0x80800000u; return; }
    *(u32 *)host_io(ROMCTRL) = ctrl | 0x00800000;  // data ready
    host_card_dma();
}

// CARDi_ReadCard's fast path (HOST): a 512-byte page straight from the image
void host_card_read(u32 addr, void *dst, u32 n) {
    for (u32 i = 0; i < n; i++) ((u8 *)dst)[i] = rom_byte(addr + i);
}

u32 card_read_data(void) {
    u32 v = 0xffffffff;
    if (sLeft) {
        if (sIsData) {
            v = 0;
            for (int i = 0; i < 4; i++) {
                u32 a = sAddr + i;
                if (a < 0x8000) a = 0x8000 + (a & 0x1ff);  // secure area reads mirror (unused by the SDK)
                v |= (u32)rom_byte(a) << (i * 8);
            }
        }
        sAddr += 4;
        sLeft -= 4;
        if (sLeft == 0) {
            u32 c = *(u32 *)host_io(ROMCTRL) & ~0x80800000u;
            *(u32 *)host_io(ROMCTRL) = c;
            if (*(u16 *)host_io(0x040001a0) & 0x4000) host_irq_raise(1u << 19);  // transfer complete
        }
    }
    return v;
}
