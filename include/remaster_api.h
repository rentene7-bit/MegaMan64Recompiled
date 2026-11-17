#ifndef REMASTER_API_H
#define REMASTER_API_H

#include <cstdint>

struct recomp_context;

extern "C" {
void remaster_patch_flags(uint8_t* rdram, recomp_context* ctx);
void remaster_fade_steps(uint8_t* rdram, recomp_context* ctx);
void remaster_bilerp_mode(uint8_t* rdram, recomp_context* ctx);
void remaster_adjust_fog(uint8_t* rdram, recomp_context* ctx);
void remaster_scale_projection(uint8_t* rdram, recomp_context* ctx);
void remaster_set_cutscene_active(uint8_t* rdram, recomp_context* ctx);
void remaster_consume_cutscene_skip(uint8_t* rdram, recomp_context* ctx);
void remaster_play_voice(uint8_t* rdram, recomp_context* ctx);
void recomp_get_camera_inputs(uint8_t* rdram, recomp_context* ctx);
}

#endif
