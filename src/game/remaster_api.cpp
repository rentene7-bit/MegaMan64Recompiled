#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "recomp_ui.h"
#include "remaster.hpp"
#include "addons.hpp"
#include "zelda_config.h"
#ifdef MM64R_RESEARCH
#include "research.hpp"
#endif

#include <atomic>

namespace {

std::atomic<int32_t> g_player_guest{0};

bool guest_ram(int32_t address) {
    const uint32_t value = static_cast<uint32_t>(address);
    return value >= 0x80000000u && value < 0x80800000u;
}

int64_t guest_at(uint32_t address) {
    return static_cast<int64_t>(static_cast<int32_t>(address));
}

bool read_flag(uint8_t* rdram, int id) {
    const int64_t at = guest_at(remaster::kFlagBase + static_cast<uint32_t>(id >> 3));
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (id & 7));
    return (MEM_BU(0, at) & mask) != 0;
}

void write_flag(uint8_t* rdram, int id) {
    if (id < remaster::kAddonFlagOrigin || id >= remaster::kAddonFlagOrigin + 0x100) {
        return;
    }
    const int64_t at = guest_at(remaster::kFlagBase + static_cast<uint32_t>(id >> 3));
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (id & 7));
    MEM_BU(0, at) = static_cast<uint8_t>(MEM_BU(0, at) | mask);
}

void apply_edit(uint8_t* rdram, int32_t player, const remaster::AddonEdit& edit) {
    const bool persist = edit.zenny_delta != 0 || edit.flag_count > 0;
    if (edit.zenny_delta != 0) {
        const int64_t at = guest_at(remaster::kZennyGuest);
        int next = static_cast<int>(MEM_HU(0, at)) + edit.zenny_delta;
        if (next < 0) {
            next = 0;
        }
        if (next > 65535) {
            next = 65535;
        }
        MEM_HU(0, at) = static_cast<uint16_t>(next);
    }
    if (edit.hp_delta != 0) {
        int next = MEM_H(0x6E, player) + edit.hp_delta;
        if (next < 0) {
            next = 0;
        }
        if (next > 999) {
            next = 999;
        }
        MEM_H(0x6E, player) = static_cast<int16_t>(next);
    }
    for (int i = 0; i < edit.flag_count; ++i) {
        write_flag(rdram, edit.flags_to_set[i]);
    }
    if (persist) {
        zelda64::save_config();
    }
}

void remember_player(uint8_t* rdram, int32_t player) {
    if (!guest_ram(player)) {
        return;
    }
    g_player_guest.store(player);
    const int hp = MEM_H(0x6E, player);
    remaster::note_actor_hp(0, hp);
    remaster::AddonView world;
    world.area = MEM_BU(0, guest_at(remaster::kAreaGuest));
    world.entrance = MEM_BU(0, guest_at(remaster::kEntranceGuest));
    world.zenny = MEM_HU(0, guest_at(remaster::kZennyGuest));
    world.hp = hp;
    world.vanilla_right_chest = read_flag(rdram, 0x440);
    world.vanilla_left_chest = read_flag(rdram, 0x441);
    world.tutorial_done = read_flag(rdram, 0x610);
    apply_edit(rdram, player, remaster::addon_tick(world));
    world.zenny = MEM_HU(0, guest_at(remaster::kZennyGuest));
    world.hp = MEM_H(0x6E, player);
    apply_edit(rdram, player, remaster::addon_apply_requests(world));
    /* Absolute address confirmed on pc1. 0 is Remove, 2 is Machine Buster. */
    const int64_t equipped_at = static_cast<int64_t>(static_cast<int32_t>(0x80204B20));
    const int pending = remaster::take_equipped_weapon_write();
    if (pending >= 0 && pending <= 255) {
        MEM_BU(0, equipped_at) = static_cast<uint8_t>(pending);
    }
    remaster::note_equipped_weapon(MEM_BU(0, equipped_at));
}

} // namespace

extern "C" void remaster_note_player(uint8_t* rdram, recomp_context* ctx) {
    remember_player(rdram, _arg<0, s32>(rdram, ctx));
}

extern "C" void remaster_note_player_ctx(uint8_t* rdram, recomp_context* ctx) {
    remember_player(rdram, static_cast<int32_t>(ctx->r4));
}

extern "C" void remaster_scale_damage(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const int32_t player = g_player_guest.load();
    const int32_t victim = static_cast<int32_t>(ctx->r16);
    const int32_t amount = static_cast<int32_t>(ctx->r17);
    const bool known = guest_ram(player);
    const bool is_player = known && victim == player;
    const int scaled = remaster::scale_damage_amount(known, is_player, amount);
    ctx->r17 = static_cast<gpr>(static_cast<int64_t>(scaled));
}

extern "C" void remaster_note_boss_bar(uint8_t* rdram, recomp_context* ctx) {
    const int32_t bar = static_cast<int32_t>(ctx->r4);
    if (!guest_ram(bar)) {
        return;
    }
    const int32_t actor = MEM_W(0x20, bar);
    if (!guest_ram(actor)) {
        return;
    }
    const int hp = MEM_H(0x6E, actor);
    int id = static_cast<int>((static_cast<uint32_t>(actor) >> 4) & 0x7fffu);
    if (id == 0) {
        id = 1;
    }
    remaster::note_actor_hp(id, hp);
}

extern "C" void remaster_patch_flags(uint8_t* rdram, recomp_context* ctx) {
#ifdef MM64R_RESEARCH
    mm64r::note_rdram(rdram);
#else
    (void)rdram;
#endif
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

extern "C" void remaster_text_pointer(uint8_t* rdram, recomp_context* ctx) {
    /* v0 already points one byte past the glyph this tick revealed. */
    const int32_t pointer = static_cast<int32_t>(ctx->r2);
    uint8_t upcoming[2] = {};
    int count = 0;
    for (int i = 0; i < 2; i++) {
        const int32_t at = pointer + i;
        const uint32_t guest = static_cast<uint32_t>(at);
        if (guest < 0x80000000u || guest >= 0x80800000u) {
            break;
        }
        upcoming[i] = MEM_BU(0, static_cast<int64_t>(at));
        count++;
    }
    ctx->r2 = static_cast<gpr>(static_cast<int64_t>(pointer + remaster::dialogue_extra_advance(upcoming, count)));
}

extern "C" void remaster_play_voice(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, remaster::play_voice(_arg<0, s32>(rdram, ctx)) ? 1 : 0);
}
