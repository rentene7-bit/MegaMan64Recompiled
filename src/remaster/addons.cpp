#include "remaster_addons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace remaster::addons {
namespace {

struct State {
    ToolHooks tools;
    bool story = false;
    int zenny = 0;
    int zenny_pending = 0;
    int area = 0;
    int entrance = 0;
    int deep_floor = 0;
    bool lab = false;
    bool trophy = false;
    int reputation = 0;

    int spar_index = -1;
    int spar_hp = 0;
    bool spar_prize[5] = {};

    int contract = -1;
    int contract_progress = 0;
    bool contract_done[6] = {};

    bool servbot[24] = {};
    int servbot_paid = 0;

    int rebuild = 0;

    bool paint_open[4] = {true, false, false, false};
    Rgb paint[4] = {};

    bool photo[6] = {};
    int range_best[8] = {};
    bool journal[8] = {};
    bool disc_found[6] = {};
    bool disc_played = false;

    bool sunken = false;
    bool water_gear = false;
    bool collapse = false;
    double collapse_left = 0;
    int collapse_prize = 0;
    bool collapse_banked = false;

    int flutter_parts[4] = {};
    int flutter_count = 0;

    int shards_sold_day = -1;
    int shards_sold = 0;

    int (*track_count)(int) = nullptr;
    bool (*play)(int, int) = nullptr;
    bool saw_deep = false;
    int sonar_marks = 0;
    bool mirror_on = false;
    int elite_hp = 0;
    int elite_max = 36;
    int elite_kind = 0;
    bool elite_front = true;
    bool elite_done = false;
    bool titan_alive = false;
    bool titan_fought = false;
    int titan_hp = 0;
    bool custodian_done = false;
    int tagged = 0;
    int role = 0;
    bool rival_on = false;
    bool rival_won = false;
    double rival_time = 0;
    int rival_area = -1;
    SpawnRequest spawns[8] = {};
    int spawn_count = 0;
    std::string folder;
    double save_wait = 0;
    bool loaded = false;
};

State g;

int clamp_byte(int value) {
    return std::clamp(value, 0, 255);
}

void push_flag(std::vector<int>& out, int id) {
    if (id < kFlagLo || id > kFlagHi) {
        return;
    }
    if (std::find(out.begin(), out.end(), id) == out.end()) {
        out.push_back(id);
    }
}

const char* kMachines[] = {
    "Gesellschaft drill",
    "Gesellschaft bomber",
    "Gesellschaft tank",
    "Gesellschaft bird",
    "Gesellschaft flagship"
};
const int kMachineHp[] = {40, 55, 70, 48, 90};
const int kMachinePart[] = {41, 42, 43, 44, 45};

struct ContractDef {
    const char* title;
    int goal;
    int payout;
};
const ContractDef kContracts[] = {
    {"Cull the burrowers on the west shelf", 4, 800},
    {"Bring back a sealed relay from the gate", 1, 1200},
    {"Walk the surveyor to the lift", 1, 1500},
    {"Cull the shielded pair in the mirror hall", 2, 1800},
    {"Recover the guild stamp from the collapse", 1, 2000},
    {"Escort the lamp tender across the span", 1, 2200}
};

struct ServbotDef {
    int area;
    const char* hint;
};
const ServbotDef kServbots[] = {
    {0x1A, "Behind the gate crane, under a loose plate."},
    {0x1A, "In the ticket booth, asleep on the ledger."},
    {0x1B, "Under Roll's console, hugging a cable."},
    {0x1B, "In the corridor locker with the spare bulbs."},
    {0x00, "On the tower's first landing, behind the rod."},
    {0x00, "Wedged in the tutorial door track."},
    {0x13, "At the market stall that sells nothing."},
    {0x13, "On the bakery roof, counting birds."},
    {0x19, "By the sub-gate dock piling."},
    {0x19, "Inside the empty cargo pod."},
    {0x12, "In the old city's dry fountain."},
    {0x12, "On the boarded hotel sign."},
    {0x07, "At the lake spillway, fishing with a stick."},
    {0x07, "Under the pier where the paint cans are."},
    {0x1A, "In the guild notice box, reading the notices."},
    {0x1B, "Folded in the wardrobe with the spare scarves."},
    {0x00, "On the chest that is not a chest."},
    {0x13, "Behind the courier bench."},
    {0x19, "In the Flutter's tool drawer."},
    {0x12, "On the rebuild scaffold, holding a nail."},
    {0x07, "In the reed bed, only the antenna showing."},
    {0x1A, "Under the sparring yard tarp."},
    {0x00, "At the top of the last tower stair."},
    {0x12, "In the paint shop's color cabinet."}
};

struct StageDef {
    const char* name;
    int zenny;
    int supplies;
    const char* shop;
};
const StageDef kStages[] = {
    {"Clear the rubble", 2000, 2, "Scrap counter"},
    {"Restore the well", 5000, 3, "Water office"},
    {"Raise the first roof", 9000, 4, "Parts stall"},
    {"Light the street", 14000, 5, "Lampwright"},
    {"Open the guild hall", 20000, 6, "Digger's desk"}
};

const char* kJournals[] = {
    "Day 3. The gate eats compasses. I marked the walls with chalk instead.",
    "Day 6. The servbots copy our routes. Leave them a light and they stay.",
    "Day 9. Zenny in the left chest, a tool in the right. Always.",
    "Day 12. Water in the lower gallery does not recede. Bring a coat that floats.",
    "Day 15. When the ceiling ticks, you have already stayed too long.",
    "Day 18. The mirror halls move the same stones into new arguments.",
    "Day 21. The pirates drill on the yard after dark. They bow if you bow.",
    "Day 24. The city comes back one roof at a time, if anyone pays for the nails."
};

const char* kDiscs[] = {
    "Yard whistle",
    "Sub-gate hum",
    "Market brass",
    "Ruin drip",
    "Flutter night",
    "Closing bell"
};

const char* kJunk[] = {
    "a bent survey pin",
    "a cracked gauge glass",
    "a salt-stained coil",
    "a guild token with no number",
    "a speaker cone full of sand"
};

} // namespace

void reset() { g = State{}; }

void set_tool_hooks(ToolHooks hooks) { g.tools = hooks; }
ToolHooks tool_hooks() { return g.tools; }
void note_neighbor(const NeighborView& view) {
    if (view.grapple) g.tools.grapple = true;
    if (view.sonar) g.tools.sonar = true;
    if (view.coolant) g.tools.coolant = true;
    if (view.fusion) g.tools.fusion = true;
    if (view.sonar_marks > g.sonar_marks) g.sonar_marks = view.sonar_marks;
    if (view.deep_gate && !g.saw_deep) {
        g.saw_deep = true;
        if (g.deep_floor < 10) g.deep_floor = 10;
    }
}
NeighborView neighbor() {
    NeighborView view;
    view.grapple = g.tools.grapple;
    view.sonar = g.tools.sonar;
    view.coolant = g.tools.coolant;
    view.fusion = g.tools.fusion;
    view.deep_gate = g.saw_deep;
    view.sonar_marks = g.sonar_marks;
    return view;
}

static void queue_spawn(int model_id, int count, const char* role) {
    if (g.spawn_count >= 8 || count <= 0) return;
    g.spawns[g.spawn_count++] = SpawnRequest{model_id, count, role};
}
void note_story_complete(bool done) { g.story = done; }
bool story_complete() { return g.story; }
void note_zenny(int amount) { g.zenny = std::max(0, amount); }
int zenny() { return g.zenny; }
int settle_zenny(int game_zenny) {
    const int pending = g.zenny_pending;
    g.zenny_pending = 0;
    const int settled = std::clamp(std::max(0, game_zenny) + pending, 0, 65535);
    g.zenny = settled;
    return settled;
}
bool spend_zenny(int amount) {
    if (amount < 0 || g.zenny < amount) return false;
    g.zenny -= amount;
    g.zenny_pending -= amount;
    return true;
}
void grant_zenny(int amount) {
    if (amount > 0) {
        g.zenny += amount;
        g.zenny_pending += amount;
    }
}
void note_area(int area_id, int entrance) {
    g.area = area_id;
    g.entrance = entrance;
}
int area() { return g.area; }
void note_deep_gate_floor(int floor) { g.deep_floor = std::max(0, floor); }
int deep_gate_floor() { return g.deep_floor; }
bool lab_ready() { return g.lab; }

bool sparring_available() { return g.story; }
int sparring_machine_count() { return 5; }
const char* sparring_machine_name(int index) {
    if (index < 0 || index >= 5) return "";
    return kMachines[index];
}
bool start_sparring(int index) {
    if (!g.story || index < 0 || index >= 5 || g.spar_index >= 0) return false;
    g.spar_index = index;
    g.spar_hp = kMachineHp[index];
    return true;
}
int sparring_hp() { return g.spar_hp; }
int strike_sparring(int damage) {
    if (g.spar_index < 0 || damage <= 0) return g.spar_hp;
    g.spar_hp = std::max(0, g.spar_hp - damage);
    if (g.spar_hp == 0) {
        g.spar_prize[g.spar_index] = true;
        g.reputation += 1;
        g.spar_index = -1;
    }
    return g.spar_hp;
}
bool sparring_prize_claimed(int index) {
    return index >= 0 && index < 5 && g.spar_prize[index];
}
int sparring_prize_part(int index) {
    if (!sparring_prize_claimed(index)) return 0;
    return kMachinePart[index];
}

int contract_count() { return 6; }
const char* contract_title(int index) {
    if (index < 0 || index >= 6) return "";
    return kContracts[index].title;
}
int contract_payout(int index) {
    if (index < 0 || index >= 6) return 0;
    return kContracts[index].payout;
}
bool accept_contract(int index) {
    if (index < 0 || index >= 6 || g.contract >= 0) return false;
    g.contract = index;
    g.contract_progress = 0;
    return true;
}
int active_contract() { return g.contract; }
bool report_contract_progress(int amount) {
    if (g.contract < 0 || amount <= 0) return false;
    g.contract_progress += amount;
    return g.contract_progress >= kContracts[g.contract].goal;
}
bool turn_in_contract() {
    if (g.contract < 0) return false;
    if (g.contract_progress < kContracts[g.contract].goal) return false;
    grant_zenny(kContracts[g.contract].payout);
    g.contract_done[g.contract] = true;
    g.reputation += 1;
    g.contract = -1;
    g.contract_progress = 0;
    return true;
}

int servbot_count() { return 24; }
const char* servbot_hint(int index) {
    if (index < 0 || index >= 24) return "";
    return kServbots[index].hint;
}
int servbot_area(int index) {
    if (index < 0 || index >= 24) return -1;
    return kServbots[index].area;
}
bool find_servbot(int index) {
    if (index < 0 || index >= 24 || g.servbot[index]) return false;
    g.servbot[index] = true;
    const int tiers[] = {6, 12, 18, 24};
    const int pays[] = {500, 1000, 2000, 4000};
    const int found = servbots_returned();
    for (int i = 0; i < 4; i++) {
        if (found >= tiers[i] && g.servbot_paid < tiers[i]) {
            grant_zenny(pays[i]);
            g.servbot_paid = tiers[i];
            g.reputation += 1;
        }
    }
    return true;
}
int servbots_returned() {
    return static_cast<int>(std::count(std::begin(g.servbot), std::end(g.servbot), true));
}
int servbot_reward_zenny() {
    int pay = 0;
    if (g.servbot_paid >= 6) pay += 500;
    if (g.servbot_paid >= 12) pay += 1000;
    if (g.servbot_paid >= 18) pay += 2000;
    if (g.servbot_paid >= 24) pay += 4000;
    return pay;
}

int rebuild_stage_count() { return 5; }
int rebuild_stage() { return g.rebuild; }
const char* rebuild_stage_name(int stage) {
    if (stage < 0 || stage >= 5) return "";
    return kStages[stage].name;
}
int rebuild_cost(int stage) {
    if (stage < 0 || stage >= 5) return 0;
    return kStages[stage].zenny;
}
const char* rebuild_shop(int stage) {
    if (stage < 0 || stage > g.rebuild || stage >= 5) return "";
    if (stage == g.rebuild) return "";
    return kStages[stage].shop;
}
bool fund_rebuild(int supplies) {
    if (g.rebuild >= 5) return false;
    const StageDef& stage = kStages[g.rebuild];
    if (supplies < stage.supplies) return false;
    if (!spend_zenny(stage.zenny)) return false;
    g.rebuild++;
    g.reputation += 2;
    if (g.rebuild >= 5) unlock_paint_slot(3);
    return true;
}

bool paint_slot_unlocked(int slot) {
    return slot >= 0 && slot < 4 && g.paint_open[slot];
}
void unlock_paint_slot(int slot) {
    if (slot >= 0 && slot < 4) g.paint_open[slot] = true;
}
bool set_paint(int slot, Rgb color) {
    if (!paint_slot_unlocked(slot)) return false;
    g.paint[slot] = Rgb{clamp_byte(color.r), clamp_byte(color.g), clamp_byte(color.b)};
    return true;
}
Rgb paint(int slot) {
    if (slot < 0 || slot >= 4) return {};
    return g.paint[slot];
}

int photo_job_count() { return 6; }
bool complete_photo(int job, int area_id, bool photo_mode) {
    if (!photo_mode || job < 0 || job >= 6 || g.photo[job]) return false;
    const int wanted[] = {0x1A, 0x1B, 0x00, 0x13, 0x12, 0x07};
    if (area_id != wanted[job]) return false;
    g.photo[job] = true;
    grant_zenny(300);
    return true;
}
int photo_payout() {
    return static_cast<int>(std::count(std::begin(g.photo), std::end(g.photo), true)) * 300;
}

Appraisal appraise(int rarity) {
    Appraisal out;
    rarity = std::clamp(rarity, 1, 5);
    out.fee = rarity * 100;
    if (!spend_zenny(out.fee)) return {};
    out.ok = true;
    out.name = kJunk[rarity - 1];
    return out;
}

int refractor_price(int day) {
    day = std::max(0, day);
    const int table[] = {40, 55, 35, 70, 50, 45, 80};
    return table[day % 7];
}
int sell_refractors(int shards, int day) {
    if (shards <= 0) return 0;
    if (g.shards_sold_day != day) {
        g.shards_sold_day = day;
        g.shards_sold = 0;
    }
    const int room = std::max(0, 20 - g.shards_sold);
    const int sold = std::min(shards, room);
    g.shards_sold += sold;
    const int pay = sold * refractor_price(day);
    grant_zenny(pay);
    return pay;
}

int record_range_score(int weapon, int score) {
    if (weapon < 0 || weapon >= 8 || score < 0) return 0;
    g.range_best[weapon] = std::max(g.range_best[weapon], score);
    return g.range_best[weapon];
}
int best_range_score(int weapon) {
    if (weapon < 0 || weapon >= 8) return 0;
    return g.range_best[weapon];
}

int journal_count() { return 8; }
const char* journal_page(int index) {
    if (index < 0 || index >= 8) return "";
    return kJournals[index];
}
bool read_journal(int index) {
    if (index < 0 || index >= 8) return false;
    g.journal[index] = true;
    return true;
}
bool journals_complete() {
    return std::count(std::begin(g.journal), std::end(g.journal), true) == 8;
}

void set_disc_player(int (*track_count)(int), bool (*play)(int, int)) {
    g.track_count = track_count;
    g.play = play;
}
int music_disc_count() { return 6; }
bool find_music_disc(int index) {
    if (index < 0 || index >= 6 || g.disc_found[index]) return false;
    g.disc_found[index] = true;
    return true;
}
DiscCue play_music_disc(int index) {
    DiscCue cue;
    if (index < 0 || index >= 6 || !g.disc_found[index]) return cue;
    cue.disc = 0;
    cue.track = index;
    cue.title = kDiscs[index];
    const int count = g.track_count ? g.track_count(0) : 0;
    if (count <= index || g.play == nullptr) return cue;
    cue.ready = g.play(0, index);
    if (cue.ready) g.disc_played = true;
    return cue;
}

int reputation() { return g.reputation; }
std::string epilogue() {
    std::ostringstream out;
    if (g.rebuild >= 5 && g.reputation >= 8) {
        out << "The old city keeps its lights on. Diggers leave their stamps in the new hall, and the servbots have stopped hiding.";
    } else if (g.rebuild > 0) {
        out << "A few roofs are back. The rest of the district still smells of wet stone, waiting on the next purse of zenny.";
    } else if (g.reputation > 0) {
        out << "The island remembers the work, even if the streets do not show it yet.";
    } else {
        out << "You left the ruins as you found them. The city stays dark, and the guild board yellows in its case.";
    }
    return out.str();
}

float sunken_move_scale() {
    if (!g.sunken) return 1.0f;
    if (g.water_gear) return 0.85f;
    return 0.45f;
}
bool enter_sunken() {
    g.sunken = true;
    return true;
}
bool equip_water_gear() {
    g.water_gear = true;
    return true;
}
bool sunken_chest_marked() {
    return g.sunken && (g.tools.sonar || g.sonar_marks > 0);
}

bool start_collapse(int prize, double seconds) {
    if (g.collapse || prize <= 0 || seconds <= 0) return false;
    g.collapse = true;
    g.collapse_prize = prize;
    g.collapse_left = seconds;
    g.collapse_banked = false;
    return true;
}
void tick(double dt) {
    const double step = std::clamp(dt, 0.0, 0.1);
    if (g.rival_on) g.rival_time += step;
    if (g.collapse) {
        g.collapse_left -= step;
        if (g.collapse_left <= 0) {
            g.collapse = false;
            g.collapse_prize = 0;
        }
    }
    if (g.folder.empty()) return;
    g.save_wait += step;
    if (g.save_wait < 5.0) return;
    g.save_wait = 0;
    save_to_folder(g.folder);
}
bool escape_collapse() {
    if (!g.collapse || g.collapse_left <= 0) return false;
    grant_zenny(g.collapse_prize);
    g.collapse = false;
    g.collapse_banked = true;
    g.collapse_prize = 0;
    return true;
}
bool collapse_banked() { return g.collapse_banked; }

void on_world(int area_id, int entrance, int play_frames) {
    const bool changed = area_id != g.area;
    note_area(area_id, entrance);
    if (changed && area_id != 0x1A) g.tagged = 0;
    if (changed && g.rival_on) {
        const RaceResult result = finish_race(g.rival_time, 45.0, 1000);
        g.rival_won = result.won;
        g.rival_on = false;
    } else if (changed && area_id == 0x1A && !g.rival_on) {
        g.rival_on = true;
        g.rival_time = 0;
        g.rival_area = area_id;
        queue_spawn(0, 1, "rival");
    }
    if (changed && area_id == 0x12 && g.rebuild < 5) {
        fund_rebuild(kStages[g.rebuild].supplies);
    }
    if (area_id == 0x1B) {
        if (g.tools.grapple) add_flutter_part(1);
        if (g.tools.sonar) add_flutter_part(2);
        if (g.tools.fusion) add_flutter_part(3);
    }
    if (area_id == 0x1A && !g.mirror_on) {
        g.mirror_on = true;
        const auto remix = mirror_remix(area_id);
        if (!remix.empty()) {
            g.elite_kind = remix[0].enemy % 3;
            g.elite_max = 24 + remix[0].count * 12;
        }
    }
    if (!g.titan_fought && play_frames >= 36000) {
        g.titan_fought = true;
        g.titan_alive = true;
        g.titan_hp = 120;
        queue_spawn(1, 1, "titan");
    }
}

int claim_actor(int victim_key) {
    if (victim_key == 0) return -1;
    if (g.tagged != 0 && g.tagged != victim_key) return -1;
    if (g.tagged == 0) {
        if (g.area == 0x1A && g.titan_alive) {
            g.tagged = victim_key;
            g.role = 1;
        } else if (g.area == 0x1A && g.mirror_on && !g.elite_done) {
            g.tagged = victim_key;
            g.role = 3;
            if (g.elite_hp <= 0) g.elite_hp = g.elite_max;
        } else {
            return -1;
        }
    }
    if (g.role == 1) return g.titan_hp;
    if (g.role == 3) return g.elite_hp;
    return -1;
}

int on_hurt(int victim_key, bool victim_is_player, int damage) {
    if (victim_is_player || damage <= 0) return damage;
    if (g.spar_index >= 0) {
        strike_sparring(damage);
        return 0;
    }
    if (claim_actor(victim_key) < 0) return damage;
    if (g.role == 1) {
        g.titan_hp = std::max(0, g.titan_hp - damage);
        if (g.titan_hp == 0) {
            g.titan_alive = false;
            g.tagged = 0;
            defeat_titan();
            return 32767;
        }
        return 0;
    }
    const EliteKind kind = g.elite_kind == 1 ? EliteKind::Split : g.elite_kind == 2 ? EliteKind::Burrow : EliteKind::Shield;
    const bool surfaced = kind != EliteKind::Burrow || !g.elite_front;
    const EliteHit hit = strike_elite(kind, g.elite_hp, damage, g.elite_front, surfaced);
    if (!hit.damaged) {
        g.elite_front = false;
        return 0;
    }
    g.elite_hp = hit.hp;
    if (hit.spawned > 0) {
        g.elite_hp = std::max(1, hit.hp / 2);
    }
    if (g.elite_hp == 0) {
        g.elite_done = true;
        g.tagged = 0;
        return 32767;
    }
    return 0;
}

RgbTint scene_tint() {
    RgbTint tint;
    if (g.sunken) {
        tint.active = true;
        tint.r = 40;
        tint.g = 90;
        tint.b = 150;
        return tint;
    }
    if (g.elite_hp > 0 || g.titan_alive) {
        tint.active = true;
        tint.r = 170;
        tint.g = 50;
        tint.b = 40;
        return tint;
    }
    if (g.rebuild > 0) {
        tint.active = true;
        tint.r = 150;
        tint.g = 110;
        tint.b = 60;
    }
    return tint;
}

int spawn_request_count() { return g.spawn_count; }
SpawnRequest spawn_request(int index) {
    if (index < 0 || index >= g.spawn_count) return {};
    return g.spawns[index];
}
void clear_spawn_requests() { g.spawn_count = 0; }

std::vector<RemixSlot> mirror_remix(int gate_id) {
    uint32_t mix = static_cast<uint32_t>(gate_id) * 16777619u + 0xA26u;
    std::vector<RemixSlot> slots;
    const int kinds[] = {1, 2, 3, 4};
    for (int i = 0; i < 4; i++) {
        mix = mix * 1664525u + 1013904223u;
        slots.push_back(RemixSlot{kinds[mix % 4], 1 + static_cast<int>((mix >> 8) % 3)});
    }
    return slots;
}

EliteHit strike_elite(EliteKind kind, int hp, int damage, bool from_front, bool surfaced) {
    EliteHit hit;
    hit.hp = std::max(0, hp);
    if (damage <= 0) return hit;
    if (kind == EliteKind::Shield && from_front) return hit;
    if (kind == EliteKind::Burrow && !surfaced) return hit;
    hit.damaged = true;
    hit.hp = std::max(0, hit.hp - damage);
    if (kind == EliteKind::Split && hit.hp > 0 && hit.hp <= hp / 2) {
        hit.spawned = 2;
    }
    return hit;
}

ChestOpen open_chest(bool mimic) {
    if (!mimic) return ChestOpen::Loot;
    if (g.tools.sonar || g.sonar_marks > 0) return ChestOpen::Safe;
    return ChestOpen::Fight;
}

Custodian custodian_for_floor(int floor) {
    Custodian boss;
    if (floor < 10 || (floor % 10) != 0) return boss;
    static const char* pieces[] = {"drill head", "bomber bell", "tank tread", "bird wing"};
    boss.active = true;
    boss.module = (floor / 10) % 4;
    boss.hp = 80 + boss.module * 20;
    boss.piece = pieces[boss.module];
    return boss;
}

bool titan_due(double world_seconds) {
    return world_seconds >= 600.0;
}
TitanDrop defeat_titan() {
    TitanDrop drop;
    g.reputation += 2;
    if (g.tools.fusion) {
        drop.material = true;
        return drop;
    }
    drop.zenny = 2500;
    grant_zenny(drop.zenny);
    return drop;
}

RaceResult finish_race(double elapsed, double par, int prize) {
    RaceResult result;
    if (elapsed <= par) {
        result.won = true;
        grant_zenny(prize);
        g.reputation += 1;
        return result;
    }
    result.buyout = std::max(0, prize * 3 / 2);
    return result;
}

RefitPart add_flutter_part(int part_id) {
    RefitPart out;
    if (part_id <= 0) return out;
    for (int i = 0; i < g.flutter_count; i++) {
        if (g.flutter_parts[i] == part_id) return out;
    }
    if (g.flutter_count >= 4) return out;
    g.flutter_parts[g.flutter_count++] = part_id;
    out.added = true;
    if (g.flutter_count >= 3) {
        g.lab = true;
        g.trophy = true;
        out.lab = true;
    }
    return out;
}
bool flutter_trophy() { return g.trophy; }

Delivery finish_delivery(double elapsed, double par, bool has_skates) {
    Delivery out;
    if (!has_skates || elapsed < 0) return out;
    out.finished = true;
    out.pay = 400;
    if (elapsed <= par) out.pay += 250;
    grant_zenny(out.pay);
    return out;
}

Catch reel_fish(int cast_id, double seconds) {
    Catch caught;
    if (seconds < 0.4) {
        caught.name = "a snapped line";
        return caught;
    }
    const int roll = (cast_id * 17 + static_cast<int>(seconds * 10.0)) % 5;
    if (roll == 0) {
        caught.name = "a lake trout";
        caught.zenny = 150;
    } else if (roll == 1) {
        caught.name = "a sunken servo";
        caught.fusion_junk = g.tools.fusion;
        caught.zenny = g.tools.fusion ? 0 : 200;
    } else if (roll == 2) {
        caught.name = "a coil of wet wire";
        caught.fusion_junk = g.tools.fusion;
        caught.zenny = g.tools.fusion ? 0 : 80;
    } else if (roll == 3) {
        caught.name = "a pale crab";
        caught.zenny = 60;
    } else {
        caught.name = "an empty can";
        caught.zenny = 10;
    }
    grant_zenny(caught.zenny);
    return caught;
}

std::vector<int> flag_writes() {
    std::vector<int> out;
    if (g.sunken) push_flag(out, SunkenEntered);
    if (g.water_gear) push_flag(out, WaterGear);
    if (g.collapse_banked) push_flag(out, CollapseBanked);
    if (g.story) push_flag(out, SparringUnlocked);
    for (int i = 0; i < 5; i++) {
        if (g.spar_prize[i]) push_flag(out, SparringClear0 + i);
    }
    for (int i = 0; i < 6; i++) {
        if (g.contract_done[i]) push_flag(out, GuildTurnIn + i);
    }
    for (int i = 0; i < 24; i++) {
        if (g.servbot[i]) push_flag(out, Servbot0 + i);
    }
    for (int i = 0; i < g.rebuild; i++) push_flag(out, RebuildStage0 + i);
    if (g.lab) push_flag(out, FlutterLab);
    if (g.trophy) push_flag(out, FlutterTrophy);
    if (journals_complete()) push_flag(out, JournalsComplete);
    if (g.disc_played) push_flag(out, DiscPlayed);
    if (g.paint_open[1] || g.paint_open[2] || g.paint_open[3]) push_flag(out, PaintUnlocked);
    if (g.mirror_on) push_flag(out, EliteEnabled);
    if (g.elite_done) push_flag(out, MirrorCleared0);
    if (g.custodian_done) push_flag(out, CustodianSeen);
    if (g.titan_fought && !g.titan_alive) push_flag(out, TitanDown);
    if (g.rival_won) push_flag(out, RivalWon);
    return out;
}

std::string serialize() {
    std::ostringstream out;
    out << "zenny=" << g.zenny << "\n";
    out << "story=" << (g.story ? 1 : 0) << "\n";
    out << "reputation=" << g.reputation << "\n";
    out << "rebuild=" << g.rebuild << "\n";
    out << "lab=" << (g.lab ? 1 : 0) << "\n";
    out << "water=" << (g.water_gear ? 1 : 0) << "\n";
    out << "servbots=";
    for (bool found : g.servbot) out << (found ? '1' : '0');
    out << "\n";
    out << "spar=";
    for (bool claimed : g.spar_prize) out << (claimed ? '1' : '0');
    out << "\n";
    out << "mirror=" << (g.mirror_on ? 1 : 0) << "\n";
    out << "elite_done=" << (g.elite_done ? 1 : 0) << "\n";
    out << "titan_fought=" << (g.titan_fought ? 1 : 0) << "\n";
    out << "titan_alive=" << (g.titan_alive ? 1 : 0) << "\n";
    out << "custodian_done=" << (g.custodian_done ? 1 : 0) << "\n";
    out << "rival_won=" << (g.rival_won ? 1 : 0) << "\n";
    return out.str();
}

bool deserialize(const std::string& text) {
    const ToolHooks tools = g.tools;
    const auto track_count = g.track_count;
    const auto play = g.play;
    const std::string folder = g.folder;
    const bool loaded = g.loaded;
    reset();
    g.tools = tools;
    g.track_count = track_count;
    g.play = play;
    g.folder = folder;
    g.loaded = loaded;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "zenny") g.zenny = std::max(0, std::atoi(value.c_str()));
        else if (key == "story") g.story = value == "1";
        else if (key == "reputation") g.reputation = std::max(0, std::atoi(value.c_str()));
        else if (key == "rebuild") g.rebuild = std::clamp(std::atoi(value.c_str()), 0, 5);
        else if (key == "lab") g.lab = value == "1";
        else if (key == "water") g.water_gear = value == "1";
        else if (key == "servbots") {
            for (size_t i = 0; i < value.size() && i < 24; i++) g.servbot[i] = value[i] == '1';
        } else if (key == "spar") {
            for (size_t i = 0; i < value.size() && i < 5; i++) g.spar_prize[i] = value[i] == '1';
        } else if (key == "mirror") g.mirror_on = value == "1";
        else if (key == "elite_done") g.elite_done = value == "1";
        else if (key == "titan_fought") g.titan_fought = value == "1";
        else if (key == "titan_alive") {
            g.titan_alive = value == "1";
            if (g.titan_alive) g.titan_hp = 120;
        } else if (key == "custodian_done") g.custodian_done = value == "1";
        else if (key == "rival_won") g.rival_won = value == "1";
    }
    g.trophy = g.lab;
    return true;
}

bool save_to_folder(const std::string& folder) {
    if (folder.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::ofstream out(std::filesystem::path(folder) / "remaster-addons-b.txt");
    if (!out) return false;
    out << serialize();
    return out.good();
}

bool load_from_folder(const std::string& folder) {
    std::ifstream in(std::filesystem::path(folder) / "remaster-addons-b.txt");
    if (!in) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return deserialize(buffer.str());
}

void bind_save_folder(const std::string& folder) {
    g.folder = folder;
    if (g.loaded || folder.empty()) return;
    load_from_folder(folder);
    g.loaded = true;
}

std::vector<ItemReport> reports() {
    return {
        {24, ItemStatus::Blocked, "Sunken sub-gate", "No confirmed player-speed field, so the live actor is not slowed."},
        {25, ItemStatus::Done, "Collapse digs", "Timer, bank, and failure. Uses the current ruin."},
        {26, ItemStatus::Blocked, "Mirror ruins", "No confirmed actor-spawn function, so remix slots are never created."},
        {27, ItemStatus::Done, "Elite variants", "The actor you hit in the gate is shielded, then damaged. Tint is bgPrim."},
        {28, ItemStatus::Blocked, "Mimic chest", "No chest-open function is hooked, so a mimic never intercepts an open."},
        {29, ItemStatus::Done, "Bonne sparring", "Five rematches, part prizes, no story flags."},
        {30, ItemStatus::Blocked, "The Custodian", "0xCA0 is not a floor count, and there is no custodian actor to attach."},
        {31, ItemStatus::Done, "Roaming titan", "After the play-time halfword, the next actor you hit in area 0x1A is the titan."},
        {32, ItemStatus::Done, "Rival digger", "Leaving area 0x1A pays zenny back into 0x801BC406."},
        {33, ItemStatus::Done, "Digger's Guild board", "Six repeatable contracts and payouts."},
        {34, ItemStatus::Done, "Lost Servbots", "Twenty-four hints and tier rewards."},
        {35, ItemStatus::Done, "Old City rebuild", "Entering area 0x12 spends the next stage cost and warms bgPrim."},
        {36, ItemStatus::Done, "News photo jobs", "Area-matched shots while photo mode is on."},
        {37, ItemStatus::Done, "Flutter refit", "Docking at 0x1B with grapple, sonar, and fusion opens the lab."},
        {38, ItemStatus::Done, "Parts appraiser", "Fee by rarity, original junk names."},
        {39, ItemStatus::Done, "Courier job", "Skate deliveries with a time bonus."},
        {40, ItemStatus::Done, "Refractor exchange", "Daily price, twenty-shard cap."},
        {41, ItemStatus::Blocked, "Lake fishing", "No confirmed action-button address, so a cast cannot be read."},
        {42, ItemStatus::Done, "Weapon range", "Best score per weapon, no zenny spent."},
        {43, ItemStatus::Done, "Digger journals", "Eight original pages. A full set opens the vault flag."},
        {44, ItemStatus::Done, "Music discs", "Track index only. Audio is the player's own disc at runtime."},
        {45, ItemStatus::Done, "Armor paint shop", "Four slots, colors made in game. Legs unlock with the city."},
        {46, ItemStatus::Done, "Karma epilogues", "Original text from reputation and rebuild stage."}
    };
}

std::string checklist() {
    std::ostringstream out;
    for (const ItemReport& item : reports()) {
        const char* status = "partial";
        if (item.status == ItemStatus::Done) status = "done";
        if (item.status == ItemStatus::Blocked) status = "blocked";
        out << "#" << item.number << " " << status << " " << item.name << " — " << item.note << "\n";
    }
    return out.str();
}

} // namespace remaster::addons
