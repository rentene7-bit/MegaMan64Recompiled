#ifndef REMASTER_ADDONS_HPP
#define REMASTER_ADDONS_HPP

#include <cstdint>
#include <string>
#include <vector>

/* Add-ons 24-46. Add-ons 1-23 live on the other branch and own flags
 * 0xC00-0xCFF (guest 0x80205728 and up): 0xC01-0xC17 mirror each add-on id,
 * 0xC80+slot is a sonar mark, 0xC90+slot is a vault, and 0xCA0 is the Deep
 * Gate. This branch only writes 0xA00-0xAFF. Bits are MSB-first in the array
 * at 0x802055A8. Host state is the source of truth; flag_writes() is what the
 * player tick SETs. */

namespace remaster::addons {

constexpr uint32_t kFlagGuest = 0x802055A8u;
constexpr int kNeighborFlagLo = 0xC00;
constexpr int kNeighborFlagHi = 0xCFF;
constexpr int kNeighborGrapple = 0xC01;
constexpr int kNeighborSonar = 0xC02;
constexpr int kNeighborCoolant = 0xC04;
constexpr int kNeighborFusion = 0xC0D;
constexpr int kNeighborSonarMark = 0xC80;
constexpr int kNeighborVault = 0xC90;
constexpr int kNeighborDeepGate = 0xCA0;
constexpr int kFlagLo = 0xA00;
constexpr int kFlagHi = 0xAFF;

enum Flag : int {
    SunkenEntered = 0xA00,
    WaterGear = 0xA01,
    CollapseBanked = 0xA10,
    CollapseFailed = 0xA11,
    MirrorCleared0 = 0xA20,
    EliteEnabled = 0xA30,
    MimicDefeated = 0xA31,
    SparringUnlocked = 0xA40,
    SparringClear0 = 0xA41,
    CustodianSeen = 0xA50,
    TitanDown = 0xA51,
    RivalWon = 0xA52,
    GuildTurnIn = 0xA60,
    Servbot0 = 0xA70,
    RebuildStage0 = 0xA90,
    Photo0 = 0xAA0,
    FlutterLab = 0xAB0,
    FlutterTrophy = 0xAB1,
    CourierRank = 0xAD0,
    FishingCast = 0xAF0,
    JournalsComplete = 0xAF2,
    DiscPlayed = 0xAF3,
    PaintUnlocked = 0xAF4,
    EpilogueSeen = 0xAF5
};

struct ToolHooks {
    bool sonar = false;
    bool grapple = false;
    bool coolant = false;
    bool fusion = false;
};

/* Bits read from add-ons 1-23. A set bit turns the matching stub on and does
 * not clear it again, so a host test can still force the hook. */
struct NeighborView {
    bool grapple = false;
    bool sonar = false;
    bool coolant = false;
    bool fusion = false;
    bool deep_gate = false;
    int sonar_marks = 0;
    int vaults = 0;
};

struct Rgb {
    int r = 180;
    int g = 180;
    int b = 190;
};

enum class ItemStatus { Done, Partial, Blocked };

struct ItemReport {
    int number = 0;
    ItemStatus status = ItemStatus::Partial;
    const char* name = "";
    const char* note = "";
};

void reset();
void set_tool_hooks(ToolHooks hooks);
ToolHooks tool_hooks();
void note_neighbor(const NeighborView& view);
NeighborView neighbor();
void note_story_complete(bool done);
bool story_complete();
void note_zenny(int amount);
int zenny();
/* Applies zenny granted or spent since the last call to the game's current total. */
int settle_zenny(int game_zenny);
bool spend_zenny(int amount);
void grant_zenny(int amount);
void note_area(int area, int entrance);
int area();
void on_world(int area_id, int entrance, int play_frames);
int on_hurt(int victim_key, bool victim_is_player, int damage);
/* Tags the actor already in the fight. Returns the HP to write at +0x6E, or -1. */
int claim_actor(int victim_key);
void note_deep_gate_floor(int floor);
int deep_gate_floor();
bool lab_ready();

bool sparring_available();
int sparring_machine_count();
const char* sparring_machine_name(int index);
bool start_sparring(int index);
int sparring_hp();
int strike_sparring(int damage);
bool sparring_prize_claimed(int index);
int sparring_prize_part(int index);

int contract_count();
const char* contract_title(int index);
int contract_payout(int index);
bool accept_contract(int index);
int active_contract();
bool report_contract_progress(int amount);
bool turn_in_contract();

int servbot_count();
const char* servbot_hint(int index);
int servbot_area(int index);
bool find_servbot(int index);
int servbots_returned();
int servbot_reward_zenny();

int rebuild_stage_count();
int rebuild_stage();
const char* rebuild_stage_name(int stage);
int rebuild_cost(int stage);
const char* rebuild_shop(int stage);
bool fund_rebuild(int supplies);

bool paint_slot_unlocked(int slot);
void unlock_paint_slot(int slot);
bool set_paint(int slot, Rgb color);
Rgb paint(int slot);

int photo_job_count();
bool complete_photo(int job, int area_id, bool photo_mode);
int photo_payout();

struct Appraisal {
    bool ok = false;
    int fee = 0;
    std::string name;
};
Appraisal appraise(int rarity);

int refractor_price(int day);
int sell_refractors(int shards, int day);

int record_range_score(int weapon, int score);
int best_range_score(int weapon);

int journal_count();
const char* journal_page(int index);
bool read_journal(int index);
bool journals_complete();

struct DiscCue {
    bool ready = false;
    int disc = 0;
    int track = 0;
    const char* title = "";
};
void set_disc_player(int (*track_count)(int disc), bool (*play)(int disc, int track));
int music_disc_count();
bool find_music_disc(int index);
DiscCue play_music_disc(int index);

int reputation();
std::string epilogue();

struct RgbTint {
    bool active = false;
    int r = 0;
    int g = 0;
    int b = 0;
};
RgbTint scene_tint();

struct SpawnRequest {
    int model_id = 0;
    int count = 0;
    const char* role = "";
};
int spawn_request_count();
SpawnRequest spawn_request(int index);
void clear_spawn_requests();

float sunken_move_scale();
bool enter_sunken();
bool equip_water_gear();
bool sunken_chest_marked();
/* Area 0x1D rooms 0-2. 0x19 is city streets and 0x13 is dry corridors. */
bool sunken_area(int area);
bool sunken_room(int area, int room);
void note_underwater(int area, int room, bool coolant);
bool underwater();
int scale_axis(int area, int room, bool water_gear, int speed);

/* Player buttons live at +0x11C/+0x11E/+0x120 (0x80204ACC/CE/D0).
 * +0x7C is not a button word. In the action word, 0x2000 is C-down,
 * 0x4000 is A/jump, and 0x8000 is B/fire. */
constexpr uint16_t kActionButton = 0x2000;
constexpr uint16_t kJumpButton = 0x4000;
constexpr uint16_t kFireButton = 0x8000;
constexpr uint32_t kPlayerGuest = 0x802049B0u;
constexpr uint32_t kPlayerHeldGuest = 0x80204ACCu;
constexpr uint32_t kPlayerPressedGuest = 0x80204ACEu;
constexpr uint32_t kPlayerReleasedGuest = 0x80204AD0u;
constexpr int kPlayerButtonOffset = 0x11E;

constexpr int kVanillaRightChest = 0x40; /* flag 0x440, same room, stays loot */
constexpr int kDesignatedMimicChest = 0x41; /* flag 0x441, area 0x00 room 2 */
void designate_mimic_chest(int id);
int designated_mimic_chest();
struct ChestResult {
    bool skip_reward = false;
    bool fight = false;
};
/* area and room of -1 skip the census room check. */
ChestResult consider_chest(int chest_id, int area = -1, int room = -1);
void arm_mimic_at(int x, int y, int z);
bool mimic_pending();
int mimic_pending_x();
int mimic_pending_y();
int mimic_pending_z();
void note_mimic_placed(int slot);
int mimic_slot();
void note_mimic_alive(bool alive);

constexpr uint32_t kEnemyPool = 0x801FFBE0u;
constexpr uint32_t kEnemyStride = 0x3C8u;
constexpr int kEnemySlots = 18;
constexpr uint32_t kEnemyLiveMask = 0x801E8EB4u;
constexpr int kActorPosX = 0x1C;
constexpr int kActorPosY = 0x20;
constexpr int kActorPosZ = 0x24;
struct RemixPoint {
    int x = 0;
    int y = 0;
    int z = 0;
};
struct EnemyMove {
    int slot = -1;
    int x = 0;
    int y = 0;
    int z = 0;
};
int mirror_point_count();
bool enemy_live(uint32_t mask, int slot);
int first_live_enemy(uint32_t mask);
int plan_enemy_moves(uint32_t live_mask, const RemixPoint* points, int count, EnemyMove* out, int cap);
int take_mirror_plan(uint32_t live_mask, EnemyMove* out, int cap);

struct BossRoom {
    int area = -1;
    int entrance = -1;
};
int custodian_room_count();
bool custodian_room(int area, int entrance, const BossRoom* rooms, int count);
int buff_existing_boss(int hp, int floor);
bool custodian_buff_due(int actor, int hp);
void note_custodian_buff(int actor);

bool start_collapse(int prize, double seconds);
void tick(double dt);
bool escape_collapse();
bool collapse_banked();

struct RemixSlot {
    int enemy = 0;
    int count = 0;
};
std::vector<RemixSlot> mirror_remix(int gate_id);

enum class EliteKind { Shield, Split, Burrow };
struct EliteHit {
    int hp = 0;
    int spawned = 0;
    bool damaged = false;
};
EliteHit strike_elite(EliteKind kind, int hp, int damage, bool from_front, bool surfaced);

enum class ChestOpen { Loot, Fight, Safe };
ChestOpen open_chest(bool mimic);

struct Custodian {
    bool active = false;
    int module = 0;
    int hp = 0;
    const char* piece = "";
};
Custodian custodian_for_floor(int floor);

struct TitanDrop {
    bool material = false;
    int zenny = 0;
};
bool titan_due(double world_seconds);
TitanDrop defeat_titan();

struct RaceResult {
    bool won = false;
    int buyout = 0;
};
RaceResult finish_race(double elapsed, double par, int prize);

struct RefitPart {
    bool added = false;
    bool lab = false;
};
RefitPart add_flutter_part(int part_id);
bool flutter_trophy();

struct Delivery {
    bool finished = false;
    int pay = 0;
};
Delivery finish_delivery(double elapsed, double par, bool has_skates);

struct Catch {
    const char* name = "";
    bool fusion_junk = false;
    int zenny = 0;
};
Catch reel_fish(int cast_id, double seconds);

struct ShorePoint {
    int area = -1;
    int x = 0;
    int y = 0;
    int z = 0;
    int radius = 0;
    int room = -1;
};
int shore_count();
bool at_shore(int area, int room, int x, int y, int z, const ShorePoint* shores, int count);
bool player_at_shore(int area, int room, int x, int y, int z);
struct CastUpdate {
    bool started = false;
    bool finished = false;
    Catch caught;
};
CastUpdate update_cast(bool on_shore, uint16_t pressed, uint16_t held, uint16_t released, double dt);

std::vector<int> flag_writes();
std::string serialize();
bool deserialize(const std::string& text);
bool save_to_folder(const std::string& folder);
bool load_from_folder(const std::string& folder);
void bind_save_folder(const std::string& folder);

std::vector<ItemReport> reports();
std::string checklist();

} // namespace remaster::addons

#endif
