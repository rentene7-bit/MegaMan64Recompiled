#ifndef MM64R_ADDONS_HPP
#define MM64R_ADDONS_HPP

#include <cstdint>
#include <string>

/* Add-ons 1-23. New story bits use flag ids 0xC00 and up so they sit past the
 * weapon words at 0x80205640. Vanilla chest 0x440 / 0x441 and door bits
 * 0x7B1 / 0x7B2 / 0x617 are only read. */

namespace remaster {

constexpr uint32_t kFlagBase = 0x802055A8u;
constexpr int kAddonFlagOrigin = 0xC00;

constexpr uint32_t kZennyGuest = 0x801BC406u;
constexpr uint32_t kHpGuest = 0x80204A1Eu;
constexpr uint32_t kAreaGuest = 0x801BC438u;
constexpr uint32_t kEntranceGuest = 0x801BC439u;
constexpr uint32_t kLoadedAreaGuest = 0x801BC450u;

enum class AddonId : int {
    Grapple = 1,
    Sonar = 2,
    Decoy = 3,
    Coolant = 4,
    Arc = 5,
    Disc = 6,
    Sentry = 7,
    Patch = 8,
    Charge = 9,
    Pierce = 10,
    Bounce = 11,
    Overclock = 12,
    Fusion = 13,
    Resonance = 14,
    Mastery = 15,
    Lantern = 16,
    Dash = 17,
    WallKick = 18,
    Magnet = 19,
    Analyzer = 20,
    Relay = 21,
    DeepGate = 22,
    Vaults = 23
};

enum class AddonVerb : int {
    None = 0,
    FindJunk,
    Build,
    Upgrade,
    Ping,
    OpenVault,
    Fuse,
    DeepPush,
    DeepExtract,
    UseGear
};

struct AddonView {
    int area = 0;
    int entrance = 0;
    int zenny = 0;
    int hp = 0;
    bool vanilla_right_chest = false; /* flag 0x440 */
    bool vanilla_left_chest = false;  /* flag 0x441 */
    bool tutorial_done = false;       /* flag 0x610 */
};

struct AddonEdit {
    int zenny_delta = 0;
    int hp_delta = 0;
    int flags_to_set[8] = {};
    int flag_count = 0;
    std::string note;
};

const char* addon_name(AddonId id);
bool addon_built(AddonId id);
int addon_level(AddonId id);
int addon_zenny_spent();
std::string addon_status_line(AddonId id);

/* Host commands. They spend zenny only through the returned edit. */
AddonEdit addon_command(AddonVerb verb, AddonId id, const AddonView& world);

/* Keyboard requests. F7 cycles, F8 builds or upgrades, F6 runs the item's special. */
void addon_request_cycle(int delta);
void addon_request_confirm();
void addon_request_special();
AddonEdit addon_apply_requests(const AddonView& world);
AddonId addon_selected();

/* Once per player tick. Auto-ping and vault/deep-gate rules live here. */
AddonEdit addon_tick(const AddonView& world);

std::string addon_save_blob();
void addon_load_blob(const std::string& blob);
void addon_reset();

/* Flag id for a built tool, or -1. */
int addon_built_flag(AddonId id);

/* Buster shot halfwords written by func_8007B814. 0x6208 is the heavy path. */
constexpr int kShotLight = 0x4208;
constexpr int kShotHeavy = 0x6208;
constexpr uint32_t kPartByteGuest = 0x80204E00u;
constexpr int kFireButton = 0x4000;

struct ShotSpawn {
    int half = kShotLight;
    bool charge_built = false;
    int charge_meter = 0;
    bool locked = false;
};

struct ShotSpawnOut {
    int half = kShotLight;
    int charge_meter = 0;
    bool zero_rapid = false;
    bool heavy = false;
    bool locked = false;
};

ShotSpawnOut adjust_shot_spawn(const ShotSpawn& in);

struct ShotHit {
    int alive = 0;
    int tag = 0;
    int vel_x = 0;
    int vel_z = 0;
    bool pierce = false;
    bool bounce = false;
    bool arc = false;
    bool disc = false;
};

struct ShotHitOut {
    bool keep = false;
    int alive = 0;
    int tag = 0;
    int vel_x = 0;
    int vel_z = 0;
    bool chain = false;
};

ShotHitOut adjust_shot_hit(const ShotHit& in);

struct OutgoingHit {
    int amount = 0;
    bool victim_is_player = false;
    bool heavy = false;
    int chain = 0;
    bool decoy = false;
    bool coolant = false;
    bool bounce_penalty = false;
    bool disc = false;
    bool yank = false;
    int stagger = 0;
    bool resonance = false;
    int mastery = 0;
};

struct OutgoingHitOut {
    int amount = 0;
    bool heavy_left = false;
    int chain_left = 0;
    bool yank_left = false;
    int stagger_left = 0;
    bool zero_velocity = false;
    int knockback = 0;
};

OutgoingHitOut adjust_outgoing_hit(const OutgoingHit& in);

/* Hybrid written to the live part byte. Never larger than the better input. */
int fusion_part_byte(int slot_a, int slot_b);

struct MoveStep {
    int vel_x = 0;
    int vel_z = 0;
    int dash_left = 0;
    bool kicked = false;
};

MoveStep step_motion(MoveStep in, bool want_dash, bool want_kick, bool in_shaft);

void addon_hold_fire(bool held);
void addon_set_charge_meter(int value);
void addon_set_heavy(bool heavy);
bool addon_heavy_pending();
void addon_set_chain(bool chain);
void addon_store_chain(int hits);
bool addon_chain_pending();
int addon_chain_hits();
bool addon_buster_locked();
int addon_decoy_ticks();
int addon_stagger_left();
void addon_set_stagger(int hits);
bool addon_yank_armed();
void addon_set_yank(bool armed);
int addon_mastery_bonus();
int lantern_fog_bonus();
void addon_note_victim(int address);
int addon_victim();
int addon_take_sentry_damage();
int addon_charge_meter();
bool addon_charge_built();
bool addon_request_scene_reload();
bool addon_take_scene_reload();
void addon_note_enemy_hp(int hp);
std::string addon_hud_line();
std::string addon_menu_line();
MoveStep addon_take_motion(int vel_x, int vel_z, bool in_shaft);

} // namespace remaster

#endif
