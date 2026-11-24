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
int g_sunken_hp_before = -1;
int g_custodian_slot = -1;

int64_t guest_at(uint32_t address) {
    return static_cast<int64_t>(static_cast<int32_t>(address));
}

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
    return (MEM_BU(0, guest_at(addr)) & mask) != 0;
}

void place_enemy(uint8_t* rdram, int slot, int x, int y, int z) {
    if (slot < 0 || slot >= remaster::addons::kEnemySlots) return;
    const uint32_t actor = remaster::addons::kEnemyPool + static_cast<uint32_t>(slot) * remaster::addons::kEnemyStride;
    const int64_t guest = guest_at(actor);
    MEM_W(remaster::addons::kActorPosX, guest) = x;
    MEM_W(remaster::addons::kActorPosY, guest) = y;
    MEM_W(remaster::addons::kActorPosZ, guest) = z;
}

int64_t enemy_guest(int slot) {
    return guest_at(remaster::addons::kEnemyPool + static_cast<uint32_t>(slot) * remaster::addons::kEnemyStride);
}

uint32_t enemy_mask(uint8_t* rdram) {
    const uint32_t mask = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::addons::kEnemyLiveMask)));
    if (mask != 0) return mask;
    /* Spawn func_8004EEAC (0x9C) and despawn func_8004F418 (0x70) keep this
     * word. When it is 0, a slot with a header and positive HP is still live. */
    uint32_t built = 0;
    for (int slot = 0; slot < remaster::addons::kEnemySlots; ++slot) {
        const int64_t actor = enemy_guest(slot);
        if (MEM_W(0, actor) == 0 || MEM_H(0x6E, actor) <= 0) continue;
        built |= 0x80000000u >> slot;
    }
    return built;
}

void set_enemy_live(uint8_t* rdram, int slot, bool on) {
    const uint32_t bit = 0x80000000u >> slot;
    const int64_t at = guest_at(remaster::addons::kEnemyLiveMask);
    uint32_t mask = static_cast<uint32_t>(MEM_W(0, at));
    mask = on ? (mask | bit) : (mask & ~bit);
    MEM_W(0, at) = static_cast<int32_t>(mask);
}

constexpr int kSlotWords = static_cast<int>(remaster::addons::kEnemyStride / 4u);

int free_enemy_slot(uint8_t* rdram) {
    const uint32_t mask = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::addons::kEnemyLiveMask)));
    for (int i = 0; i < remaster::addons::kEnemySlots; i++) {
        if ((mask & (0x80000000u >> i)) == 0) return i;
    }
    return -1;
}

/* A class-0x03 slot the current room is already updating. Nothing is kept
 * across rooms: the other room's table indexes crash func_8004BFB0. */
int room_fighter_source(uint8_t* rdram) {
    const uint32_t mask = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::addons::kEnemyLiveMask)));
    int dormant = -1;
    for (int slot = 0; slot < remaster::addons::kEnemySlots; slot++) {
        if (!remaster::addons::enemy_live(mask, slot)) continue;
        const uint32_t header = static_cast<uint32_t>(MEM_W(0, enemy_guest(slot)));
        if (!remaster::addons::actor_header_dispatches(header)) continue;
        if (MEM_H(0x6E, enemy_guest(slot)) > 0) return slot;
        if (dormant < 0) dormant = slot;
    }
    return dormant;
}

/* Duplicate that slot, then set the live bit. A blank or class-1 slot is never
 * marked live. func_8004EEAC's header 00200000 has no update function. */
int duplicate_room_fighter(uint8_t* rdram) {
    const int src = room_fighter_source(rdram);
    const int dst = free_enemy_slot(rdram);
    if (src < 0 || dst < 0 || src == dst) return -1;
    const uint32_t src_base = remaster::addons::kEnemyPool + static_cast<uint32_t>(src) * remaster::addons::kEnemyStride;
    const uint32_t dst_base = remaster::addons::kEnemyPool + static_cast<uint32_t>(dst) * remaster::addons::kEnemyStride;
    const int64_t from = enemy_guest(src);
    const int64_t actor = enemy_guest(dst);
    for (int i = 0; i < kSlotWords; i++) {
        uint32_t word = static_cast<uint32_t>(MEM_W(i * 4, from));
        if (word >= src_base && word < src_base + remaster::addons::kEnemyStride) {
            word = dst_base + (word - src_base);
        }
        MEM_W(i * 4, actor) = static_cast<int32_t>(word);
    }
    const uint32_t header = static_cast<uint32_t>(MEM_W(0, actor));
    if (!remaster::addons::actor_header_dispatches(header)) {
        for (int i = 0; i < kSlotWords; i++) MEM_W(i * 4, actor) = 0;
        return -1;
    }
    set_enemy_live(rdram, dst, true);
    return dst;
}

uint32_t fighters_now(uint8_t* rdram) {
    const uint32_t raw = static_cast<uint32_t>(MEM_W(0, guest_at(remaster::addons::kEnemyLiveMask)));
    int hp[remaster::addons::kEnemySlots];
    int type[remaster::addons::kEnemySlots];
    for (int slot = 0; slot < remaster::addons::kEnemySlots; slot++) {
        hp[slot] = 0;
        type[slot] = 0;
        if (!remaster::addons::enemy_live(raw, slot)) continue;
        hp[slot] = MEM_H(0x6E, enemy_guest(slot));
        type[slot] = MEM_BU(0, enemy_guest(slot));
    }
    return remaster::addons::fighter_slots(raw, hp, type, remaster::addons::kEnemySlots);
}

void addon_flag_raise(uint8_t* rdram, int id) {
    if (id < remaster::addons::kFlagLo || id > remaster::addons::kFlagHi) {
        return;
    }
    const uint32_t addr = remaster::addons::kFlagGuest + static_cast<uint32_t>(id >> 3);
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (id & 7));
    const int64_t guest = guest_at(addr);
    MEM_BU(0, guest) = static_cast<uint8_t>(MEM_BU(0, guest) | mask);
}

} // namespace

extern "C" void remaster_addons_on_player(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    const int area = MEM_BU(0, guest_at(0x801BC438));
    const int entrance = MEM_BU(0, guest_at(0x801BC439));
    const int zenny = static_cast<uint16_t>(MEM_H(0, guest_at(0x801BC406)));
    const int frames = static_cast<uint16_t>(MEM_H(0, guest_at(0x801BC3F6)));
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
    remaster::addons::note_underwater(area, entrance, bits.coolant);
    remaster::addons::on_world(area, entrance, frames);
    const int32_t player_now = static_cast<int32_t>(remaster::addons::kPlayerGuest);
    const int hp_now = MEM_H(0x6E, player_now);
    if (g_sunken_hp_before >= 0) {
        const int clamped = remaster::addons::sunken_hp_after(g_sunken_hp_before, hp_now);
        if (clamped != hp_now) {
            MEM_H(0x6E, player_now) = static_cast<int16_t>(clamped);
        }
    }
    g_sunken_hp_before = MEM_H(0x6E, player_now);

    const int32_t player = static_cast<int32_t>(remaster::addons::kPlayerGuest);
    const int px = MEM_W(remaster::addons::kActorPosX, player);
    const int py = MEM_W(remaster::addons::kActorPosY, player);
    const int pz = MEM_W(remaster::addons::kActorPosZ, player);
    /* Just-pressed, held, and released action words. Not player+0x7C. */
    const auto pressed = static_cast<uint16_t>(MEM_H(0, guest_at(remaster::addons::kPlayerPressedGuest)));
    const auto held = static_cast<uint16_t>(MEM_H(0, guest_at(remaster::addons::kPlayerHeldGuest)));
    const auto released = static_cast<uint16_t>(MEM_H(0, guest_at(remaster::addons::kPlayerReleasedGuest)));
    remaster::addons::update_cast(remaster::addons::player_at_shore(area, entrance, px, py, pz), pressed, held, released, 1.0 / 60.0);

    remaster::addons::EnemyMove moves[remaster::addons::kEnemySlots];
    const uint32_t fought = fighters_now(rdram);
    const int move_count = remaster::addons::take_mirror_plan(fought, moves, remaster::addons::kEnemySlots);
    for (int i = 0; i < move_count; i++) {
        place_enemy(rdram, moves[i].slot, moves[i].x, moves[i].y, moves[i].z);
    }
    if (remaster::addons::mimic_slot() >= 0) {
        const int64_t actor = enemy_guest(remaster::addons::mimic_slot());
        int hp = MEM_H(0x6E, actor);
        if ((pressed & remaster::addons::kFireButton) != 0 && hp > 0) {
            hp -= 8;
            if (hp < 0) hp = 0;
            MEM_H(0x6E, actor) = static_cast<int16_t>(hp);
        }
        if (hp <= 0 || !remaster::addons::enemy_live(fought, remaster::addons::mimic_slot())) {
            set_enemy_live(rdram, remaster::addons::mimic_slot(), false);
            remaster::addons::note_mimic_alive(false);
        }
    }
    if (!remaster::addons::custodian_ready() || remaster::addons::custodian_beaten()) {
        if (g_custodian_slot >= 0) {
            set_enemy_live(rdram, g_custodian_slot, false);
            g_custodian_slot = -1;
        }
    } else if (g_custodian_slot < 0) {
        g_custodian_slot = duplicate_room_fighter(rdram);
        if (g_custodian_slot >= 0) {
            remaster::addons::note_custodian_actor_placed();
            int spot_x = 0;
            int spot_y = 0;
            int spot_z = 0;
            int spot_hp = 0;
            remaster::addons::custodian_spot(&spot_x, &spot_y, &spot_z, &spot_hp);
            place_enemy(rdram, g_custodian_slot, spot_x, spot_y, spot_z);
            MEM_H(0x6E, enemy_guest(g_custodian_slot)) = static_cast<int16_t>(spot_hp);
        }
    } else {
        const int64_t actor = enemy_guest(g_custodian_slot);
        int hp = MEM_H(0x6E, actor);
        /* Class 0 has no hitbox, so a B press in this room is the shot. */
        if ((pressed & remaster::addons::kFireButton) != 0 && hp > 0) {
            hp -= 8;
            if (hp < 0) hp = 0;
            MEM_H(0x6E, actor) = static_cast<int16_t>(hp);
        }
        if (hp <= 0) {
            set_enemy_live(rdram, g_custodian_slot, false);
            g_custodian_slot = -1;
            remaster::addons::note_custodian_down();
        } else {
            int spot_x = 0;
            int spot_y = 0;
            int spot_z = 0;
            int spot_hp = 0;
            remaster::addons::custodian_spot(&spot_x, &spot_y, &spot_z, &spot_hp);
            (void)spot_hp;
            place_enemy(rdram, g_custodian_slot, spot_x, spot_y, spot_z);
            set_enemy_live(rdram, g_custodian_slot, true);
        }
    }
    const int settled = remaster::addons::settle_zenny(zenny);
    if (settled != zenny) {
        MEM_H(0, guest_at(0x801BC406)) = static_cast<int16_t>(static_cast<uint16_t>(settled));
    }

    for (int id : remaster::addons::flag_writes()) {
        addon_flag_raise(rdram, id);
    }

    const remaster::addons::RgbTint tint = remaster::addons::scene_tint();
    if (tint.active) {
        /* GfxContext at 0x802047F0, bgPrimRed/Green/Blue at +0xA4. */
        MEM_BU(0, guest_at(0x80204894)) = static_cast<uint8_t>(tint.r);
        MEM_BU(0, guest_at(0x80204895)) = static_cast<uint8_t>(tint.g);
        MEM_BU(0, guest_at(0x80204896)) = static_cast<uint8_t>(tint.b);
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
    if (remaster::addons::custodian_buff_due(actor, hp)) {
        int spot_x = 0;
        int spot_y = 0;
        int spot_z = 0;
        int spot_hp = hp;
        remaster::addons::custodian_spot(&spot_x, &spot_y, &spot_z, &spot_hp);
        hp = spot_hp;
        MEM_H(0x6E, actor) = static_cast<int16_t>(hp);
        remaster::addons::note_custodian_buff(actor);
    } else {
        const int encounter_hp = remaster::addons::claim_actor(actor);
        if (encounter_hp > 0) {
            hp = encounter_hp;
            MEM_H(0x6E, actor) = static_cast<int16_t>(hp);
        }
    }
    int id = static_cast<int>((static_cast<uint32_t>(actor) >> 4) & 0x7fffu);
    if (id == 0) {
        id = 1;
    }
    remaster::note_actor_hp(id, hp);
}

extern "C" void remaster_sunken_settle(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    const int32_t player = static_cast<int32_t>(remaster::addons::kPlayerGuest);
    const int now = MEM_H(0x6E, player);
    if (g_sunken_hp_before >= 0) {
        const int clamped = remaster::addons::sunken_hp_after(g_sunken_hp_before, now);
        if (clamped != now) {
            MEM_H(0x6E, player) = static_cast<int16_t>(clamped);
        }
    }
    g_sunken_hp_before = MEM_H(0x6E, player);
}

extern "C" void remaster_scale_move(uint8_t* rdram, recomp_context* ctx) {
    const int area = MEM_BU(0, guest_at(0x801BC438));
    const int room = MEM_BU(0, guest_at(0x801BC439));
    const bool gear = addon_flag_set(rdram, remaster::addons::kNeighborCoolant) || remaster::addons::tool_hooks().coolant;
    ctx->r5 = static_cast<gpr>(static_cast<int64_t>(remaster::addons::scale_axis(area, room, gear, static_cast<int32_t>(ctx->r5))));
    ctx->r6 = static_cast<gpr>(static_cast<int64_t>(remaster::addons::scale_axis(area, room, gear, static_cast<int32_t>(ctx->r6))));
    ctx->r7 = static_cast<gpr>(static_cast<int64_t>(remaster::addons::scale_axis(area, room, gear, static_cast<int32_t>(ctx->r7))));
}

extern "C" void remaster_chest_reward(uint8_t* rdram, recomp_context* ctx) {
    const int32_t regs[2] = {static_cast<int32_t>(ctx->r16), static_cast<int32_t>(ctx->r17)};
    int32_t chest = 0;
    int chest_id = -1;
    for (int32_t ptr : regs) {
        if (!guest_ram(ptr)) continue;
        const int id = static_cast<int>(static_cast<int8_t>(MEM_BU(0xC, ptr)));
        const int area = MEM_BU(0, guest_at(0x801BC438));
        const int room = MEM_BU(0, guest_at(0x801BC439));
        const remaster::addons::ChestResult result = remaster::addons::consider_chest(id, area, room);
        if (!result.fight) continue;
        chest = ptr;
        chest_id = id;
        break;
    }
    if (!guest_ram(chest)) return;
    (void)chest_id;
    /* Only a fighter already live in this room. A chest-only room pays normally. */
    const bool local = room_fighter_source(rdram) >= 0;
    if (remaster::addons::mimic_grants_reward(local)) return;
    const int slot = duplicate_room_fighter(rdram);
    if (slot < 0) return;
    ctx->r5 = static_cast<gpr>(1);
    ctx->r6 = static_cast<gpr>(0);
    const int x = MEM_W(remaster::addons::kActorPosX, chest);
    const int y = MEM_W(remaster::addons::kActorPosY, chest);
    const int z = MEM_W(remaster::addons::kActorPosZ, chest);
    place_enemy(rdram, slot, x, y, z);
    int spot_hp = 0;
    remaster::addons::custodian_spot(nullptr, nullptr, nullptr, &spot_hp);
    if (spot_hp <= 0) spot_hp = 100;
    MEM_H(0x6E, enemy_guest(slot)) = static_cast<int16_t>(spot_hp);
    remaster::addons::note_mimic_placed(slot);
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
        upcoming[i] = MEM_BU(0, static_cast<int64_t>(static_cast<int32_t>(at)));
        count++;
    }
    ctx->r2 = static_cast<gpr>(static_cast<int64_t>(pointer + remaster::dialogue_extra_advance(upcoming, count)));
}

extern "C" void remaster_play_voice(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, remaster::play_voice(_arg<0, s32>(rdram, ctx)) ? 1 : 0);
}
