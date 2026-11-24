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

void nudge_pickups(uint8_t* rdram, int32_t player);
void note_lock_on(uint8_t* rdram);

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
    const bool action = (MEM_HU(remaster::kButtonPressed, player) & remaster::kActionButton) != 0;
    const remaster::AddonEdit flutter = remaster::addon_flutter_vault(
        world.area, world.entrance, MEM_W(0, guest_at(0x80162830u)), MEM_W(0, guest_at(0x80162834u)),
        MEM_W(0, guest_at(0x80162838u)), action, remaster::addon_built(remaster::AddonId::Sonar));
    apply_edit(rdram, player, flutter);
    const int buttons = MEM_HU(remaster::kButtonHeld, player);
    remaster::addon_hold_fire((buttons & remaster::kFireButton) != 0);
    if (remaster::addon_take_decoy_place()) {
        const int64_t decoy = guest_at(remaster::kDecoyPosGuest);
        MEM_H(0, decoy) = MEM_H(0x14, player);
        MEM_H(2, decoy) = MEM_H(0x16, player);
        MEM_H(4, decoy) = MEM_H(0x18, player);
    }
    if (remaster::addon_charge_built()) {
        MEM_BU(0, guest_at(remaster::kPartByteGuest + 1u)) = 0;
    }
    const bool shaft = world.area == 0 || world.area == 0x13 || world.area == 0x1A;
    const int vel_x = MEM_H(0x44, player);
    const int vel_z = MEM_H(0x48, player);
    const remaster::MoveStep step = remaster::addon_take_motion(
        vel_x, vel_z, shaft, MEM_W(0x1C, player), MEM_W(0x24, player));
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
    if (remaster::addon_freeze_step()) {
        const int32_t frozen = remaster::addon_victim();
        if (guest_ram(frozen)) {
            MEM_H(0x44, frozen) = 0;
            MEM_H(0x48, frozen) = 0;
        }
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
    nudge_pickups(rdram, player);
    note_lock_on(rdram);
}

bool enemy_live(uint32_t mask, int slot) {
    return (mask & (0x80000000u >> slot)) != 0;
}

int64_t enemy_addr(int slot) {
    return guest_at(remaster::kEnemyPool + static_cast<uint32_t>(slot * remaster::kEnemyStride));
}

void read_enemy_spots(uint8_t* rdram, remaster::EnemySpot* spots) {
    const uint32_t mask = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::kEnemyLiveMask)));
    for (int slot = 0; slot < remaster::kEnemySlots; ++slot) {
        spots[slot].live = enemy_live(mask, slot);
        if (!spots[slot].live) {
            continue;
        }
        const int64_t actor = enemy_addr(slot);
        spots[slot].x = MEM_W(0x1C, actor);
        spots[slot].z = MEM_W(0x24, actor);
    }
}

void apply_enemy_drop(uint8_t* rdram, int32_t actor, int drop) {
    remaster::OutgoingHit hit;
    hit.amount = drop;
    hit.victim_is_player = false;
    hit.heavy = remaster::addon_heavy_pending();
    hit.chain = remaster::addon_chain_hits();
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
    const int extra = out.amount - drop;
    int next = MEM_H(0x6E, actor) - extra;
    if (next < 0) {
        next = 0;
    }
    if (next > 32767) {
        next = 32767;
    }
    MEM_H(0x6E, actor) = static_cast<int16_t>(next);
    remaster::addon_note_enemy_hit(next, out.amount);
    remaster::addon_note_victim(actor);
    remaster::addon_note_victim_pos(MEM_W(0x1C, actor), MEM_W(0x24, actor));
    if (out.zero_velocity) {
        MEM_H(0x44, actor) = 0;
        MEM_H(0x48, actor) = 0;
        if (hit.coolant) {
            remaster::addon_start_freeze();
        }
    }
    if (out.knockback != 0) {
        int vel = MEM_H(0x48, actor) + out.knockback;
        if (vel > 32767) {
            vel = 32767;
        }
        if (vel < -32768) {
            vel = -32768;
        }
        MEM_H(0x48, actor) = static_cast<int16_t>(vel);
    }
}

int prev_enemy_hp[remaster::kEnemySlots] = {};
bool seen_enemy_hp[remaster::kEnemySlots] = {};

void nudge_pickups(uint8_t* rdram, int32_t player) {
    if (!remaster::addon_built(remaster::AddonId::Magnet) || !guest_ram(player)) {
        return;
    }
    const int player_x = MEM_H(0x14, player);
    const int player_z = MEM_H(0x18, player);
    for (int slot = 0; slot < remaster::kPickupSlots; ++slot) {
        const int64_t drop = guest_at(remaster::kPickupPool + static_cast<uint32_t>(slot * remaster::kPickupStride));
        if (MEM_BU(remaster::kPickupTypeOff, drop) != remaster::kPickupDropType) {
            continue;
        }
        remaster::DropNudge nudge;
        nudge.drop_x = MEM_H(0x38, drop);
        nudge.drop_z = MEM_H(0x3C, drop);
        nudge.player_x = player_x;
        nudge.player_z = player_z;
        const remaster::DropNudgeOut moved = remaster::nudge_drop(nudge);
        MEM_H(0x38, drop) = static_cast<int16_t>(moved.x);
        MEM_H(0x3C, drop) = static_cast<int16_t>(moved.z);
    }
}

void note_lock_on(uint8_t* rdram) {
    if (!remaster::addon_built(remaster::AddonId::Analyzer)) {
        return;
    }
    const uint32_t id = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::kLockOnId)));
    int slot = remaster::lock_on_slot(id);
    const uint32_t pool = remaster::kEnemyPool;
    const uint32_t pool_end = pool + static_cast<uint32_t>(remaster::kEnemySlots * remaster::kEnemyStride);
    if (slot < 0 && id >= pool && id < pool_end) {
        slot = static_cast<int>((id - pool) / remaster::kEnemyStride);
    }
    const uint32_t mask = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::kEnemyLiveMask)));
    if (slot < 0 || slot >= remaster::kEnemySlots || !enemy_live(mask, slot)) {
        remaster::addon_set_lock_hp(-1);
        return;
    }
    remaster::addon_set_lock_hp(MEM_H(0x6E, enemy_addr(slot)));
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
    /* func_8006E838 runs from the player update, so the victim is the player.
     * Enemy HP is adjusted from the enemy list in remaster_enemy_pool. */
    if (!is_player) {
        ctx->r17 = static_cast<gpr>(static_cast<int64_t>(scaled));
        return;
    }
    remaster::OutgoingHit hit;
    hit.amount = scaled;
    hit.victim_is_player = true;
    hit.decoy = remaster::addon_decoy_ticks() > 0;
    const remaster::OutgoingHitOut out = remaster::adjust_outgoing_hit(hit);
    ctx->r17 = static_cast<gpr>(static_cast<int64_t>(out.amount));
}

extern "C" void remaster_enemy_pool(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    const uint32_t mask = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::kEnemyLiveMask)));
    for (int slot = 0; slot < remaster::kEnemySlots; ++slot) {
        if (!enemy_live(mask, slot)) {
            seen_enemy_hp[slot] = false;
            continue;
        }
        const int32_t actor = static_cast<int32_t>(enemy_addr(slot));
        int hp_now = MEM_H(0x6E, actor);
        if (seen_enemy_hp[slot] && hp_now < prev_enemy_hp[slot]) {
            apply_enemy_drop(rdram, actor, prev_enemy_hp[slot] - hp_now);
            hp_now = MEM_H(0x6E, actor);
        }
        prev_enemy_hp[slot] = hp_now;
        seen_enemy_hp[slot] = true;
    }
}

extern "C" void remaster_retarget_player(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    if (!remaster::addon_retarget_active()) {
        return;
    }
    const gpr player_pos = static_cast<gpr>(static_cast<int64_t>(static_cast<int32_t>(remaster::kPlayerPos16)));
    const gpr decoy = static_cast<gpr>(static_cast<int64_t>(static_cast<int32_t>(remaster::kDecoyPosGuest)));
    if (static_cast<int32_t>(ctx->r4) == static_cast<int32_t>(player_pos)) {
        ctx->r4 = decoy;
    }
    if (static_cast<int32_t>(ctx->r5) == static_cast<int32_t>(player_pos)) {
        ctx->r5 = decoy;
    }
    if (static_cast<int32_t>(ctx->r6) == static_cast<int32_t>(player_pos)) {
        ctx->r6 = decoy;
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
    if (remaster::addon_take_plant()) {
        const int32_t shot = static_cast<int32_t>(ctx->r17);
        if (static_cast<uint32_t>(shot) >= 0x80000000u && static_cast<uint32_t>(shot) < 0x80800000u) {
            MEM_B(0x9, shot) = 8;
            MEM_H(0x44, shot) = 0;
            MEM_H(0x48, shot) = 0;
        }
    } else if (remaster::addon_built(remaster::AddonId::Coolant)) {
        const int32_t shot = static_cast<int32_t>(ctx->r17);
        if (static_cast<uint32_t>(shot) >= 0x80000000u && static_cast<uint32_t>(shot) < 0x80800000u) {
            auto stretch = [](int value) {
                value *= 2;
                if (value > 32767) {
                    value = 32767;
                }
                if (value < -32768) {
                    value = -32768;
                }
                return static_cast<int16_t>(value);
            };
            MEM_H(0x44, shot) = stretch(MEM_H(0x44, shot));
            MEM_H(0x48, shot) = stretch(MEM_H(0x48, shot));
        }
    }
}

extern "C" void remaster_shot_hit(uint8_t* rdram, recomp_context* ctx) {
    const int32_t shot = static_cast<int32_t>(ctx->r17);
    if (static_cast<uint32_t>(shot) < 0x80000000u || static_cast<uint32_t>(shot) >= 0x80800000u) {
        return;
    }
    remaster::PlantTick plant;
    plant.lives = MEM_B(0x9, shot);
    plant.alive = static_cast<int>(ctx->r2);
    const remaster::PlantTickOut planted = remaster::plant_shot_tick(plant);
    if (planted.keep) {
        ctx->r2 = 0;
        MEM_B(0x3, shot) = 0;
        MEM_B(0x9, shot) = static_cast<int8_t>(planted.lives);
        MEM_H(0x44, shot) = 0;
        MEM_H(0x48, shot) = 0;
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
    int vel_x = out.vel_x;
    int vel_z = out.vel_z;
    if (out.chain) {
        remaster::ShotSeek seek;
        seek.seek = true;
        seek.vel_x = vel_x;
        seek.vel_z = vel_z;
        seek.from_x = MEM_W(0x1C, shot);
        seek.from_z = MEM_W(0x24, shot);
        remaster::EnemySpot spots[remaster::kEnemySlots];
        read_enemy_spots(rdram, spots);
        int victim_x = 0;
        int victim_z = 0;
        remaster::addon_victim_pos(victim_x, victim_z);
        int hit_slot = remaster::nearest_other_enemy(spots, remaster::kEnemySlots, victim_x, victim_z, -1, nullptr, nullptr);
        if (victim_x == 0 && victim_z == 0) {
            hit_slot = remaster::nearest_other_enemy(spots, remaster::kEnemySlots, seek.from_x, seek.from_z, -1, nullptr, nullptr);
        }
        int chain_x = 0;
        int chain_z = 0;
        const int chain_slot = remaster::nearest_other_enemy(
            spots, remaster::kEnemySlots, seek.from_x, seek.from_z, hit_slot, &chain_x, &chain_z);
        if (chain_slot >= 0) {
            seek.to_x = chain_x;
            seek.to_z = chain_z;
        }
        const remaster::ShotSeekOut steered = remaster::steer_shot(seek);
        vel_x = steered.vel_x;
        vel_z = steered.vel_z;
    }
    MEM_H(0x44, shot) = static_cast<int16_t>(vel_x);
    MEM_H(0x48, shot) = static_cast<int16_t>(vel_z);
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
    const int zoom = remaster::resonance_zoom_q8(remaster::widescreen_scale_q8(aspect));
    remaster::scale_projection_matrix(matrix, zoom);
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

bool vault_door_gates(uint8_t* rdram) {
    if (MEM_BU(0, guest_at(0x801BC3E8u)) != 3 || MEM_BU(0, guest_at(0x801BC3E9u)) != 2) {
        return false;
    }
    if (MEM_B(0, guest_at(0x802047CCu)) != 0) {
        return false;
    }
    if (MEM_H(0, guest_at(0x801FF39Au)) != 0) {
        return false;
    }
    if ((static_cast<uint32_t>(MEM_W(0, guest_at(0x80203FF0u))) & 0x0F000000u) != 0) {
        return false;
    }
    if (MEM_BU(0, guest_at(0x801BC5B0u)) != 0) {
        return false;
    }
    const int64_t player = guest_at(0x802049B0u);
    if (MEM_H(0x6E, player) < 0 || MEM_BU(0xBD, player) != 0 || MEM_BU(0x08, player) != 1) {
        return false;
    }
    return true;
}

extern "C" void remaster_apply_vault_door(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    remaster::VaultDoor door;
    if (!remaster::addon_peek_vault_door(door)) {
        return;
    }
    if (remaster::addon_vault_door_expired()) {
        remaster::addon_clear_vault_door();
        return;
    }
    if (!vault_door_gates(rdram)) {
        return;
    }
    const int64_t base = guest_at(0x802047C0u);
    MEM_H(0, base) = static_cast<int16_t>(door.x);
    MEM_H(2, base) = static_cast<int16_t>(door.y);
    MEM_H(4, base) = static_cast<int16_t>(door.z);
    MEM_H(6, base) = static_cast<int16_t>(door.yaw);
    MEM_HU(0xA, base) = static_cast<uint16_t>(door.yaw);
    MEM_B(0, guest_at(0x802047CDu)) = 0;
    MEM_B(0, guest_at(0x802047CEu)) = static_cast<int8_t>(door.area);
    MEM_B(0, guest_at(0x802047CFu)) = static_cast<int8_t>(door.room);
    MEM_B(0, guest_at(0x802047D0u)) = 2;
    MEM_B(0, guest_at(0x802047D1u)) = static_cast<int8_t>(0x15);
    MEM_B(0, guest_at(0x802047CCu)) = 2;
    remaster::addon_clear_vault_door();
}

#ifndef MM64R_RESEARCH
extern "C" void remaster_warp_pad(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}
extern "C" void remaster_warp_frame(uint8_t* rdram, recomp_context* ctx) {
    remaster_apply_vault_door(rdram, ctx);
}
extern "C" void remaster_warp_flag_test(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}
extern "C" void remaster_warp_flag_set(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}
#endif
