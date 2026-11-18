#ifndef REMASTER_ADDONS_HPP
#define REMASTER_ADDONS_HPP

#include <cstdint>
#include <string>
#include <vector>

/* Add-ons 24-46. Add-ons 1-23 are another branch. Their event flags are
 * reserved at 0x900-0x9FF and are not written here. These systems use
 * 0xA00-0xAFF (MSB-first bits in the array at 0x802055A8). Host state is the
 * source of truth; flag_writes() is what a game hook should SET. */

namespace remaster::addons {

constexpr int kEarlyAddonFlagLo = 0x900;
constexpr int kEarlyAddonFlagHi = 0x9FF;
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
void note_story_complete(bool done);
bool story_complete();
void note_zenny(int amount);
int zenny();
bool spend_zenny(int amount);
void grant_zenny(int amount);
void note_area(int area, int entrance);
int area();
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

float sunken_move_scale();
bool enter_sunken();
bool equip_water_gear();
bool sunken_chest_marked();

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
