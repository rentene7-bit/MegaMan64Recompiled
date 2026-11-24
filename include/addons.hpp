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
/* Player+0x11C is the held word at 0x80204ACC. Fire is B, 0x8000.
 * 0x4000 in that word is A (jump). Pressed-edge is +0x11E (0x80204ACE). */
constexpr int kButtonHeld = 0x11C;
constexpr int kButtonPressed = 0x11E;
constexpr int kFireButton = 0x8000;
constexpr int kJumpButton = 0x4000;
constexpr int kActionButton = 0x2000;
constexpr int kLockButton = 0x200;

constexpr uint32_t kEnemyPool = 0x801FFBE0u;
constexpr int kEnemyStride = 0x3C8;
constexpr int kEnemySlots = 18;
constexpr uint32_t kEnemyLiveMask = 0x801E8EB4u;
constexpr uint32_t kPickupPool = 0x801ADD70u;
constexpr int kPickupStride = 0x58;
constexpr int kPickupSlots = 64;
constexpr int kPickupTypeOff = 0x2;
constexpr int kPickupDropType = 0x87;
constexpr uint32_t kLockOnId = 0x8017B664u;
constexpr uint32_t kPlayerPos16 = 0x802049C4u;
/* Past addon flags 0xC00-0xCFF (which end at 0x80205747). Three s16s. */
constexpr uint32_t kDecoyPosGuest = 0x80205800u;

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

struct ShotSeek {
    int vel_x = 0;
    int vel_z = 0;
    int from_x = 0;
    int from_z = 0;
    int to_x = 0;
    int to_z = 0;
    bool seek = false;
};

struct ShotSeekOut {
    int vel_x = 0;
    int vel_z = 0;
};

ShotSeekOut steer_shot(const ShotSeek& in);
void addon_note_victim_pos(int x, int z);
bool addon_take_plant();
struct PlantTick {
    int lives = 0;
    int alive = 0;
};
struct PlantTickOut {
    bool keep = false;
    int lives = 0;
};
PlantTickOut plant_shot_tick(const PlantTick& in);
void addon_victim_pos(int& x, int& z);

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

struct WallTouch {
    int vel_x = 0;
    int vel_z = 0;
    bool stuck = false;
    bool in_shaft = false;
    bool built = false;
    bool already = false;
};

struct WallTouchOut {
    int vel_x = 0;
    int vel_z = 0;
    bool kicked = false;
};

WallTouchOut wall_contact(const WallTouch& in);

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
void addon_start_freeze();
bool addon_freeze_step();
int resonance_zoom_q8(int base);
void addon_note_victim(int address);
int addon_victim();
int addon_take_sentry_damage();
int addon_charge_meter();
bool addon_charge_built();
bool addon_request_scene_reload();
bool addon_take_scene_reload();
void addon_note_enemy_hp(int hp);
void addon_note_enemy_hit(int hp, int damage);
void addon_set_lock_hp(int hp);
void addon_arm_decoy_place();
bool addon_take_decoy_place();
bool addon_retarget_active();

struct EnemySpot {
    int x = 0;
    int z = 0;
    bool live = false;
};

/* Index of the nearest live enemy that is not exclude, or -1. */
int nearest_other_enemy(const EnemySpot* spots, int count, int from_x, int from_z, int exclude, int* out_x, int* out_z);

struct DropNudge {
    int drop_x = 0;
    int drop_z = 0;
    int player_x = 0;
    int player_z = 0;
};

struct DropNudgeOut {
    int x = 0;
    int z = 0;
};

DropNudgeOut nudge_drop(const DropNudge& in);
int lock_on_slot(uint32_t id);
std::string addon_hud_line();
std::string addon_menu_line();
MoveStep addon_take_motion(int vel_x, int vel_z, bool in_shaft, int pos_x, int pos_z);

} // namespace remaster

#endif
