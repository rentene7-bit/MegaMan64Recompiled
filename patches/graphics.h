#ifndef __PATCH_GRAPHICS_H__
#define __PATCH_GRAPHICS_H__

#include "patch_helpers.h"

DECLARE_FUNC(void, recomp_get_window_resolution, u32*, u32*);
DECLARE_FUNC(float, recomp_get_target_aspect_ratio, float);
DECLARE_FUNC(float, recomp_get_target_hud_aspect_ratio, float);
DECLARE_FUNC(s32, recomp_get_target_framerate, s32);
DECLARE_FUNC(s32, recomp_high_precision_fb_enabled);
DECLARE_FUNC(float, recomp_get_resolution_scale);
DECLARE_FUNC(u32, remaster_patch_flags);
DECLARE_FUNC(s32, remaster_fade_steps);
DECLARE_FUNC(s32, remaster_bilerp_mode);
DECLARE_FUNC(void, remaster_adjust_fog, s16*, s16*);
DECLARE_FUNC(void, remaster_scale_projection, void*);
DECLARE_FUNC(void, remaster_set_cutscene_active, s32);
DECLARE_FUNC(s32, remaster_consume_cutscene_skip);
DECLARE_FUNC(s32, remaster_play_voice, s32);
DECLARE_FUNC(void, remaster_note_player, void*);
DECLARE_FUNC(s32, remaster_text_pointer, s32);

#endif
