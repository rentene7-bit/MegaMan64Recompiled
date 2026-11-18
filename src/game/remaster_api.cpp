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
#include <string>

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
    if (edit.note.find("Pocket Relay") != std::string::npos) {
        remaster::request_field_save();
    }
    if (edit.note.find("fused") != std::string::npos) {
        const int64_t part = guest_at(remaster::kPartByteGuest);
        const int hybrid = remaster::fusion_part_byte(MEM_BU(0, part), MEM_BU(0, part + 1));
        MEM_BU(0, part) = static_cast<uint8_t>(hybrid);
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
    const int buttons = MEM_HU(0x7C, player);
    remaster::addon_hold_fire((buttons & remaster::kFireButton) != 0);
    if (remaster::addon_charge_built()) {
        MEM_BU(0, guest_at(remaster::kPartByteGuest + 1u)) = 0;
    }
    const bool shaft = world.area == 0 || world.area == 0x13 || world.area == 0x1A;
    const int vel_x = MEM_H(0x44, player);
    const int vel_z = MEM_H(0x48, player);
    const remaster::MoveStep step = remaster::addon_take_motion(vel_x, vel_z, shaft);
    if (step.vel_x != vel_x) {
        int next = step.vel_x;
        if (next > 32767) {
            next = 32767;
        }
        if (next < -32768) {
            next = -32768;
        }
        MEM_H(0x44, player) = static_cast<int16_t>(next);
    }
    if (step.vel_z != vel_z) {
        int next = step.vel_z;
        if (next > 32767) {
            next = 32767;
        }
        if (next < -32768) {
            next = -32768;
        }
        MEM_H(0x48, player) = static_cast<int16_t>(next);
    }
    const int sentry = remaster::addon_take_sentry_damage();
    const int32_t sentry_target = remaster::addon_victim();
    if (sentry > 0 && guest_ram(sentry_target)) {
        int hp = MEM_H(0x6E, sentry_target) - sentry;
        if (hp < 0) {
            hp = 0;
        }
        MEM_H(0x6E, sentry_target) = static_cast<int16_t>(hp);
        remaster::addon_note_enemy_hp(hp);
    }
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
    const int32_t player = g_player_guest.load();
    const int32_t victim = static_cast<int32_t>(ctx->r16);
    const int32_t amount = static_cast<int32_t>(ctx->r17);
    const bool known = guest_ram(player);
    const bool is_player = known && victim == player;
    const int scaled = remaster::scale_damage_amount(known, is_player, amount);
    remaster::OutgoingHit hit;
    hit.amount = scaled;
    hit.victim_is_player = is_player;
    hit.heavy = remaster::addon_heavy_pending();
    hit.chain = remaster::addon_chain_hits();
    hit.decoy = remaster::addon_decoy_ticks() > 0;
    hit.coolant = remaster::addon_built(remaster::AddonId::Coolant);
    hit.bounce_penalty = remaster::addon_built(remaster::AddonId::Bounce);
    hit.disc = remaster::addon_built(remaster::AddonId::Disc);
    hit.yank = remaster::addon_yank_armed();
    hit.stagger = remaster::addon_stagger_left();
    hit.resonance = remaster::addon_built(remaster::AddonId::Resonance);
    hit.mastery = remaster::addon_mastery_bonus();
    const remaster::OutgoingHitOut out = remaster::adjust_outgoing_hit(hit);
    remaster::addon_set_heavy(out.heavy_left);
    remaster::addon_store_chain(out.chain_left);
    remaster::addon_set_stagger(out.stagger_left);
    remaster::addon_set_yank(out.yank_left);
    ctx->r17 = static_cast<gpr>(static_cast<int64_t>(out.amount));
    if (!is_player && guest_ram(victim)) {
        remaster::addon_note_enemy_hp(MEM_H(0x6E, victim));
        remaster::addon_note_victim(victim);
        if (out.zero_velocity) {
            MEM_H(0x44, victim) = 0;
            MEM_H(0x48, victim) = 0;
        }
        if (out.knockback != 0) {
            int next = MEM_H(0x48, victim) + out.knockback;
            if (next > 32767) {
                next = 32767;
            }
            if (next < -32768) {
                next = -32768;
            }
            MEM_H(0x48, victim) = static_cast<int16_t>(next);
        }
    }
}

extern "C" void remaster_shot_spawn(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    remaster::ShotSpawn in;
    in.half = static_cast<int>(ctx->r2);
    in.charge_built = remaster::addon_charge_built();
    in.charge_meter = remaster::addon_charge_meter();
    in.locked = remaster::addon_buster_locked();
    const remaster::ShotSpawnOut out = remaster::adjust_shot_spawn(in);
    ctx->r2 = static_cast<gpr>(out.half);
    remaster::addon_set_charge_meter(out.charge_meter);
    if (out.locked) {
        const int32_t shot = static_cast<int32_t>(ctx->r17);
        if (static_cast<uint32_t>(shot) >= 0x80000000u && static_cast<uint32_t>(shot) < 0x80800000u) {
            MEM_B(0x3, shot) = static_cast<int8_t>(0x80);
        }
    }
    if (out.heavy) {
        remaster::addon_set_heavy(true);
    }
    if (out.zero_rapid) {
        const int64_t rapid = static_cast<int64_t>(static_cast<int32_t>(remaster::kPartByteGuest + 1u));
        MEM_BU(0, rapid) = 0;
    }
}

extern "C" void remaster_shot_hit(uint8_t* rdram, recomp_context* ctx) {
    const int32_t shot = static_cast<int32_t>(ctx->r17);
    if (static_cast<uint32_t>(shot) < 0x80000000u || static_cast<uint32_t>(shot) >= 0x80800000u) {
        return;
    }
    remaster::ShotHit in;
    in.alive = static_cast<int>(ctx->r2);
    in.tag = MEM_B(0xB, shot);
    in.vel_x = MEM_H(0x44, shot);
    in.vel_z = MEM_H(0x48, shot);
    in.pierce = remaster::addon_built(remaster::AddonId::Pierce);
    in.bounce = remaster::addon_built(remaster::AddonId::Bounce);
    in.arc = remaster::addon_built(remaster::AddonId::Arc);
    in.disc = remaster::addon_built(remaster::AddonId::Disc);
    const remaster::ShotHitOut out = remaster::adjust_shot_hit(in);
    if (!out.keep) {
        return;
    }
    ctx->r2 = 0;
    MEM_B(0x3, shot) = 0;
    MEM_B(0xB, shot) = static_cast<int8_t>(out.tag);
    MEM_H(0x44, shot) = static_cast<int16_t>(out.vel_x);
    MEM_H(0x48, shot) = static_cast<int16_t>(out.vel_z);
    if (out.chain) {
        remaster::addon_set_chain(true);
    }
}

extern "C" void remaster_scene_reload(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    if (!remaster::addon_take_scene_reload()) {
        return;
    }
    /* Loaded-area halfword is r2. Area is r16. A mismatch takes the game's
     * own reload path. The area byte is left as the ruin the player is in. */
    ctx->r2 = ctx->r16 + 1;
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
