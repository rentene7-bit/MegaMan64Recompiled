#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "recomp_ui.h"
#include "remaster.hpp"
#include "remaster_addons.hpp"
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

void remember_player(uint8_t* rdram, int32_t player) {
    if (!guest_ram(player)) {
        return;
    }
    g_player_guest.store(player);
    const int hp = MEM_H(0x6E, player);
    remaster::note_actor_hp(0, hp);
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
    const bool tagged_player = known && victim == player;
    const bool absolute_player = static_cast<uint32_t>(victim) == 0x802049B0u;
    const int adjusted = remaster::addons::on_hurt(victim, tagged_player || absolute_player, amount);
    const int scaled = remaster::scale_damage_amount(known, tagged_player, adjusted);
    ctx->r17 = static_cast<gpr>(static_cast<int64_t>(scaled));
}

namespace {

bool addon_flag_set(uint8_t* rdram, int id) {
    const uint32_t addr = remaster::addons::kFlagGuest + static_cast<uint32_t>(id >> 3);
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (id & 7));
    return (MEM_BU(0, static_cast<int64_t>(addr)) & mask) != 0;
}

void addon_flag_raise(uint8_t* rdram, int id) {
    if (id < remaster::addons::kFlagLo || id > remaster::addons::kFlagHi) {
        return;
    }
    const uint32_t addr = remaster::addons::kFlagGuest + static_cast<uint32_t>(id >> 3);
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (id & 7));
    const int64_t guest = static_cast<int64_t>(addr);
    MEM_BU(0, guest) = static_cast<uint8_t>(MEM_BU(0, guest) | mask);
}

} // namespace

extern "C" void remaster_addons_on_player(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    const int area = MEM_BU(0, static_cast<int64_t>(0x801BC438));
    const int entrance = MEM_BU(0, static_cast<int64_t>(0x801BC439));
    const int zenny = static_cast<uint16_t>(MEM_H(0, static_cast<int64_t>(0x801BC406)));
    const int frames = static_cast<uint16_t>(MEM_H(0, static_cast<int64_t>(0x801BC3F6)));
    remaster::addons::note_zenny(zenny);

    remaster::addons::NeighborView bits;
    bits.grapple = addon_flag_set(rdram, remaster::addons::kNeighborGrapple);
    bits.sonar = addon_flag_set(rdram, remaster::addons::kNeighborSonar);
    bits.coolant = addon_flag_set(rdram, remaster::addons::kNeighborCoolant);
    bits.fusion = addon_flag_set(rdram, remaster::addons::kNeighborFusion);
    bits.deep_gate = addon_flag_set(rdram, remaster::addons::kNeighborDeepGate);
    int marks = 0;
    for (int slot = 0; slot < 8; slot++) {
        if (addon_flag_set(rdram, remaster::addons::kNeighborSonarMark + slot)) {
            marks++;
        }
    }
    bits.sonar_marks = marks;
    remaster::addons::note_neighbor(bits);
    if (bits.coolant && area == 0x19) {
        remaster::addons::enter_sunken();
        remaster::addons::equip_water_gear();
    }
    remaster::addons::on_world(area, entrance, frames);
    const int settled = remaster::addons::settle_zenny(zenny);
    if (settled != zenny) {
        MEM_H(0, static_cast<int64_t>(0x801BC406)) = static_cast<int16_t>(static_cast<uint16_t>(settled));
    }

    for (int id : remaster::addons::flag_writes()) {
        addon_flag_raise(rdram, id);
    }

    const remaster::addons::RgbTint tint = remaster::addons::scene_tint();
    if (tint.active) {
        /* GfxContext at 0x802047F0, bgPrimRed/Green/Blue at +0xA4. */
        MEM_BU(0, static_cast<int64_t>(0x80204894)) = static_cast<uint8_t>(tint.r);
        MEM_BU(0, static_cast<int64_t>(0x80204895)) = static_cast<uint8_t>(tint.g);
        MEM_BU(0, static_cast<int64_t>(0x80204896)) = static_cast<uint8_t>(tint.b);
    }
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
    int hp = MEM_H(0x6E, actor);
    const int encounter_hp = remaster::addons::claim_actor(actor);
    if (encounter_hp > 0) {
        hp = encounter_hp;
        MEM_H(0x6E, actor) = static_cast<int16_t>(hp);
    }
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
