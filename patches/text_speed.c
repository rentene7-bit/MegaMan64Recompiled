#include "patches.h"
#include "graphics.h"

/* pc1: func_80061E44 and func_80061EC4 are never called. The pointer at
 * 0x801D8884 is object+0x14, and func_80064F40 advances it inline at
 * 0x80065068 after func_80069518. The stored pointer still steps one byte,
 * then up to two more while the next bytes are glyphs. */

extern u8 D_80210297_1EB697;
extern u8 D_801A5410_180810;
extern u8 D_801A906F_18446F;
extern u16 D_800AE220_89620;
extern u16 D_800AE260_89660;

s32 func_8006033C_3B73C(s32);
void func_80069518_44918(s32, s32, s32);

typedef struct {
    u8 pad[0x14];
    u32 text_ptr;
    u8 mid[0x5B];
    s8 index;
} Printer;

RECOMP_PATCH void func_80064F40_40340(Printer* box) {
    u32 sum = (u32)D_801A5410_180810 + (u32)(s32)box->index;
    u8 id = *(&D_801A906F_18446F + sum);
    u16 raw = (D_80210297_1EB697 == 0) ? *(&D_800AE220_89620 + id) : *(&D_800AE260_89660 + id);
    s32 mode = func_8006033C_3B73C(0x2EE);
    s32 width = (mode != 0) ? (s32)((u32)raw >> 2) : (s32)raw;
    s32 at = (s32)box->text_ptr + 1;
    func_80069518_44918(0, width, at);
    box->text_ptr = (u32)remaster_text_pointer((s32)box->text_ptr + 1);
}
