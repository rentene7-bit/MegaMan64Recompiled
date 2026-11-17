#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "recomp_ui.h"
#include "remaster.hpp"

extern "C" void remaster_patch_flags(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, remaster::patch_flags());
}

extern "C" void remaster_fade_steps(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, remaster::fade_steps());
}

extern "C" void remaster_bilerp_mode(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, remaster::bilerp_mode());
}

extern "C" void remaster_adjust_fog(uint8_t* rdram, recomp_context* ctx) {
    s16* start = _arg<0, s16*>(rdram, ctx);
    s16* end = _arg<1, s16*>(rdram, ctx);
    if (start == nullptr || end == nullptr) {
        return;
    }
    remaster::adjust_fog(*start, *end);
}

extern "C" void remaster_scale_projection(uint8_t* rdram, recomp_context* ctx) {
    void* matrix = _arg<0, void*>(rdram, ctx);
    int width = 0;
    int height = 0;
    recompui::get_window_size(width, height);
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : (4.0f / 3.0f);
    remaster::scale_projection_matrix(matrix, remaster::widescreen_scale_q8(aspect));
}

extern "C" void remaster_set_cutscene_active(uint8_t* rdram, recomp_context* ctx) {
    remaster::set_cutscene_active(_arg<0, s32>(rdram, ctx) != 0);
}

extern "C" void remaster_consume_cutscene_skip(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, remaster::consume_cutscene_skip() ? 1 : 0);
}

extern "C" void remaster_play_voice(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, remaster::play_voice(_arg<0, s32>(rdram, ctx)) ? 1 : 0);
}
