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

} // namespace remaster

#endif
