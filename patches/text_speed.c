#include "patches.h"
#include "graphics.h"

/* The confirmed printer object lives at 0x801D8870.
 * +0x04 is the 5-tick countdown at 0x801D8874.
 * +0x14 is the byte pointer at 0x801D8884.
 * +0x6C is the duplicate pointer at 0x801D88DC.
 * func_80061E44 and func_80061EC4 are the one-byte advances for those pointers. */

typedef struct {
    u32 flags;
    u16 delay;
    u16 pad6;
    u32 pad8;
    u32 padC;
    u32 pad10;
    u32 text_ptr;
    u8 gap[0x54];
    u32 dup_ptr;
} DialogueBox;

RECOMP_PATCH s32 func_80061E44_3D244(DialogueBox* box) {
    box->text_ptr = (u32)remaster_text_pointer((s32)box->text_ptr + 1);
    return (s32)box->text_ptr;
}

RECOMP_PATCH s32 func_80061EC4_3D2C4(DialogueBox* box) {
    box->dup_ptr = (u32)remaster_text_pointer((s32)box->dup_ptr + 1);
    return 1;
}
