#include "addons.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace remaster {
namespace {

constexpr int kCount = 24;

struct Def {
    AddonId id;
    const char* name;
    int build_cost;
    int upgrade_cost;
    int max_level;
};

constexpr Def kDefs[] = {
    {AddonId::Grapple, "Grapple Arm", 3000, 25000, 5},
    {AddonId::Sonar, "Echo Sonar", 1500, 10000, 6},
    {AddonId::Decoy, "Decoy Beacon", 2000, 13000, 6},
    {AddonId::Coolant, "Coolant Sprayer", 4000, 33000, 6},
    {AddonId::Arc, "Arc Chain", 6000, 50000, 6},
    {AddonId::Disc, "Ricochet Disc", 2500, 20000, 6},
    {AddonId::Sentry, "Sentry Pod", 8000, 66000, 5},
    {AddonId::Patch, "Patch Arm", 5000, 16000, 5},
    {AddonId::Charge, "Charge Capacitor", 0, 0, 1},
    {AddonId::Pierce, "Piercing Core", 0, 0, 1},
    {AddonId::Bounce, "Bounce Lens", 0, 0, 1},
    {AddonId::Overclock, "Overclock Chip", 0, 0, 1},
    {AddonId::Fusion, "Part fusion", 20000, 0, 1},
    {AddonId::Resonance, "Resonance pairs", 0, 0, 1},
    {AddonId::Mastery, "Weapon mastery", 0, 0, 3},
    {AddonId::Lantern, "Digger's Lantern", 800, 0, 1},
    {AddonId::Dash, "Dash Thrusters", 4000, 0, 1},
    {AddonId::WallKick, "Wall-Kick Soles", 6000, 0, 1},
    {AddonId::Magnet, "Zenny Magnet Belt", 12000, 0, 1},
    {AddonId::Analyzer, "Analyzer Visor", 2500, 0, 1},
    {AddonId::Relay, "Pocket Relay", 3000, 0, 1},
    {AddonId::DeepGate, "The Deep Gate", 0, 0, 1},
    {AddonId::Vaults, "Sealed vaults", 0, 0, 1},
};

struct State {
    bool junk[kCount] = {};
    bool built[kCount] = {};
    int level[kCount] = {};
    bool pinged[8] = {};
    bool vault_open[4] = {};
    int deep_floor = 0;
    int deep_unbanked = 0;
    bool deep_inside = false;
    int heat = 0;
    int charge = 0;
    int mastery[8] = {};
    bool relay_used_area[8] = {};
    int last_area = -1;
    int seen_hp = -1;
    int slow_heal = 0;
};

State g;
std::atomic<int> g_cycle{0};
std::atomic<int> g_confirm{0};
std::atomic<int> g_special{0};
int g_selected = static_cast<int>(AddonId::Sonar);

int index_of(AddonId id) {
    const int n = static_cast<int>(id);
    return (n >= 1 && n < kCount) ? n : 0;
}

const Def* def_of(AddonId id) {
    for (const Def& def : kDefs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

void note_flag(AddonEdit& edit, int flag) {
    if (flag < 0 || edit.flag_count >= 8) {
        return;
    }
    edit.flags_to_set[edit.flag_count++] = flag;
}

void merge_edit(AddonEdit& into, const AddonEdit& from) {
    into.zenny_delta += from.zenny_delta;
    into.hp_delta += from.hp_delta;
    if (!from.note.empty()) {
        into.note = from.note;
    }
    for (int i = 0; i < from.flag_count; ++i) {
        note_flag(into, from.flags_to_set[i]);
    }
}

int ruin_slot(int area) {
    if (area == 0) {
        return 0;
    }
    if (area == 0x13) {
        return 1;
    }
    if (area == 0x1A) {
        return 2;
    }
    return -1;
}

AddonEdit spend(AddonId id, int cost, const AddonView& world, const char* why) {
    AddonEdit edit;
    if (world.zenny < cost) {
        edit.note = "Not enough zenny.";
        return edit;
    }
    edit.zenny_delta = -cost;
    edit.note = why;
    note_flag(edit, kAddonFlagOrigin + index_of(id));
    return edit;
}

void grant_opening_junk(const AddonView& world, AddonEdit& edit) {
    if (!world.tutorial_done) {
        return;
    }
    if (!g.junk[index_of(AddonId::Sonar)]) {
        g.junk[index_of(AddonId::Sonar)] = true;
        edit.note = "Found a cracked speaker cone and a sonar diaphragm.";
        note_flag(edit, kAddonFlagOrigin + 2);
    }
    if (world.vanilla_right_chest && !g.junk[index_of(AddonId::Grapple)]) {
        g.junk[index_of(AddonId::Grapple)] = true;
        note_flag(edit, kAddonFlagOrigin + 1);
    }
}

AddonEdit ping(const AddonView& world) {
    AddonEdit edit;
    if (!g.built[index_of(AddonId::Sonar)]) {
        edit.note = "Echo Sonar is not built.";
        return edit;
    }
    const int slot = ruin_slot(world.area);
    if (slot < 0) {
        edit.note = "Sonar finds nothing in this area.";
        return edit;
    }
    g.pinged[slot] = true;
    g.built[index_of(AddonId::Vaults)] = true;
    edit.note = "Sonar marked a sealed vault.";
    note_flag(edit, kAddonFlagOrigin + 0x80 + slot);
    return edit;
}

AddonEdit open_vault(const AddonView& world) {
    AddonEdit edit;
    const int slot = ruin_slot(world.area);
    if (slot < 0 || !g.pinged[slot]) {
        edit.note = "No marked vault here.";
        return edit;
    }
    if (g.vault_open[slot]) {
        edit.note = "This vault is already open.";
        return edit;
    }
    if (slot == 1 && !g.built[index_of(AddonId::Grapple)]) {
        edit.note = "The vault ring needs the Grapple Arm.";
        return edit;
    }
    if (slot == 2 && !g.built[index_of(AddonId::Arc)]) {
        edit.note = "The vault switch needs Arc Chain.";
        return edit;
    }
    g.vault_open[slot] = true;
    int loot = 800 + 400 * slot;
    if (g.built[index_of(AddonId::Magnet)]) {
        loot += 200;
    }
    edit.zenny_delta = loot;
    edit.note = "Sealed vault opened.";
    note_flag(edit, kAddonFlagOrigin + 0x90 + slot);
    if (slot == 0) {
        g.junk[index_of(AddonId::Grapple)] = true;
    }
    if (slot == 1) {
        g.junk[index_of(AddonId::Charge)] = true;
        g.junk[index_of(AddonId::Pierce)] = true;
    }
    return edit;
}

AddonEdit deep_push(const AddonView& world) {
    AddonEdit edit;
    if (ruin_slot(world.area) < 0 || !g.vault_open[0]) {
        edit.note = "The Deep Gate is still sealed.";
        return edit;
    }
    g.deep_inside = true;
    g.built[index_of(AddonId::DeepGate)] = true;
    g.deep_floor += 1;
    const int find = 150 * g.deep_floor;
    g.deep_unbanked += find;
    edit.note = "Deep Gate floor reached. Unbanked loot is not safe yet.";
    note_flag(edit, kAddonFlagOrigin + 0xA0);
    return edit;
}

AddonEdit deep_extract() {
    AddonEdit edit;
    if (!g.deep_inside) {
        edit.note = "You are not in the Deep Gate.";
        return edit;
    }
    edit.zenny_delta = g.deep_unbanked;
    g.deep_unbanked = 0;
    g.deep_inside = false;
    edit.note = "Extracted from the Deep Gate. Loot banked.";
    return edit;
}

AddonEdit fuse(const AddonView& world) {
    AddonEdit edit;
    const int pierce = index_of(AddonId::Pierce);
    const int bounce = index_of(AddonId::Bounce);
    if (!g.junk[pierce] || !g.junk[bounce] || g.level[pierce] < 1 || g.level[bounce] < 1) {
        edit.note = "Fusion needs two maxed low-tier parts.";
        return edit;
    }
    if (world.zenny < 20000) {
        edit.note = "Fusion costs 20,000 zenny.";
        return edit;
    }
    g.junk[pierce] = false;
    g.junk[bounce] = false;
    g.level[pierce] = 0;
    g.level[bounce] = 0;
    g.built[index_of(AddonId::Fusion)] = true;
    g.level[index_of(AddonId::Fusion)] = 1;
    edit.zenny_delta = -20000;
    edit.note = "Roll fused a hybrid. It does not beat a top single part.";
    note_flag(edit, kAddonFlagOrigin + 13);
    return edit;
}

void on_hurt(const AddonView& world, AddonEdit& edit) {
    if (!g.deep_inside) {
        g.seen_hp = world.hp;
        return;
    }
    if (g.seen_hp >= 0 && world.hp > 0 && world.hp < g.seen_hp) {
        /* Still alive. Unbanked loot stays. */
    }
    if (world.hp <= 0 && g.deep_unbanked > 0) {
        g.deep_unbanked = 0;
        g.deep_inside = false;
        edit.note = "Deep Gate fall. Unbanked finds are gone.";
    }
    g.seen_hp = world.hp;
}

void gear_tick(const AddonView& world, AddonEdit& edit) {
    const int slot = ruin_slot(world.area);
    if (slot >= 0 && g.built[index_of(AddonId::Relay)] && !g.relay_used_area[slot] && world.hp > 0 && world.hp < 40) {
        g.relay_used_area[slot] = true;
        edit.hp_delta += 40;
        edit.note = "Pocket Relay: one field heal.";
    }
    if (g.built[index_of(AddonId::Patch)] && world.hp > 0 && world.hp < 80) {
        g.slow_heal += 1;
        if (g.slow_heal >= 30) {
            g.slow_heal = 0;
            edit.hp_delta += 1;
        }
    }
    if (g.built[index_of(AddonId::Overclock)]) {
        g.heat = std::min(100, g.heat + 1);
    }
    if (g.built[index_of(AddonId::Charge)]) {
        g.charge = std::min(3, g.charge + 1);
    }
    if (g.built[index_of(AddonId::Charge)] && g.built[index_of(AddonId::Pierce)]) {
        g.built[index_of(AddonId::Resonance)] = true;
    }
}

} // namespace

const char* addon_name(AddonId id) {
    const Def* def = def_of(id);
    return def != nullptr ? def->name : "Unknown";
}

bool addon_built(AddonId id) {
    const int index = index_of(id);
    return index != 0 && g.built[index];
}

int addon_level(AddonId id) {
    const int index = index_of(id);
    return index == 0 ? 0 : g.level[index];
}

int addon_zenny_spent() {
    int spent = 0;
    for (const Def& def : kDefs) {
        const int index = index_of(def.id);
        if (!g.built[index]) {
            continue;
        }
        spent += def.build_cost;
        spent += def.upgrade_cost * g.level[index];
    }
    return spent;
}

std::string addon_status_line(AddonId id) {
    const int index = index_of(id);
    if (index == 0) {
        return "missing";
    }
    std::ostringstream out;
    out << addon_name(id) << (g.built[index] ? " built" : (g.junk[index] ? " junk" : " unseen"));
    out << " lv" << g.level[index];
    return out.str();
}

int addon_built_flag(AddonId id) {
    const int index = index_of(id);
    if (index == 0 || !g.built[index]) {
        return -1;
    }
    return kAddonFlagOrigin + index;
}

AddonEdit addon_command(AddonVerb verb, AddonId id, const AddonView& world) {
    const int index = index_of(id);
    const Def* def = def_of(id);
    if (verb == AddonVerb::Ping) {
        return ping(world);
    }
    if (verb == AddonVerb::OpenVault) {
        return open_vault(world);
    }
    if (verb == AddonVerb::DeepPush) {
        return deep_push(world);
    }
    if (verb == AddonVerb::DeepExtract) {
        return deep_extract();
    }
    if (verb == AddonVerb::Fuse) {
        return fuse(world);
    }
    if (index == 0 || def == nullptr) {
        AddonEdit edit;
        edit.note = "Unknown add-on.";
        return edit;
    }
    if (verb == AddonVerb::FindJunk) {
        g.junk[index] = true;
        AddonEdit edit;
        edit.note = "Junk recorded.";
        note_flag(edit, kAddonFlagOrigin + index);
        return edit;
    }
    if (verb == AddonVerb::Build) {
        if (g.built[index]) {
            AddonEdit edit;
            edit.note = "Already built.";
            return edit;
        }
        const bool needs_junk = def->build_cost > 0 || (index >= 9 && index <= 12);
        if (!g.junk[index] && needs_junk) {
            AddonEdit edit;
            edit.note = "Junk is missing.";
            return edit;
        }
        AddonEdit edit = spend(id, def->build_cost, world, "Roll finished the build.");
        if (edit.zenny_delta < 0 || def->build_cost == 0) {
            g.built[index] = true;
            if (def->build_cost == 0) {
                edit.note = "Equipped.";
                note_flag(edit, kAddonFlagOrigin + index);
            }
        }
        return edit;
    }
    if (verb == AddonVerb::Upgrade) {
        if (!g.built[index] || g.level[index] >= def->max_level) {
            AddonEdit edit;
            edit.note = "Nothing to upgrade.";
            return edit;
        }
        AddonEdit edit = spend(id, def->upgrade_cost, world, "Upgrade paid.");
        if (edit.note == "Upgrade paid.") {
            g.level[index] += 1;
            if (index >= 1 && index <= 8) {
                g.mastery[index - 1] = std::min(3, g.level[index]);
            }
        }
        return edit;
    }
    if (verb == AddonVerb::UseGear) {
        AddonEdit edit;
        if (!g.built[index]) {
            edit.note = "Not built.";
            return edit;
        }
        if (id == AddonId::Grapple) {
            edit.note = "Grapple is ready. Vault rings use it; the pull reuses the vanilla step.";
            return edit;
        }
        if (id == AddonId::Dash) {
            edit.note = "Dash ready on its cooldown. Motion reuse is still the vanilla step.";
            return edit;
        }
        if (id == AddonId::Analyzer) {
            edit.note = "Analyzer is reading the actor HP the remaster already tracks.";
            return edit;
        }
        edit.note = "Gear is active.";
        return edit;
    }
    AddonEdit edit;
    edit.note = "No such command.";
    return edit;
}

void addon_request_cycle(int delta) {
    g_cycle.fetch_add(delta);
}

void addon_request_confirm() {
    g_confirm.fetch_add(1);
}

void addon_request_special() {
    g_special.fetch_add(1);
}

AddonId addon_selected() {
    return static_cast<AddonId>(g_selected);
}

AddonVerb special_for(AddonId id) {
    switch (id) {
    case AddonId::Grapple:
        return AddonVerb::UseGear;
    case AddonId::Sonar:
        return AddonVerb::Ping;
    case AddonId::Vaults:
        return AddonVerb::OpenVault;
    case AddonId::DeepGate:
        return AddonVerb::DeepPush;
    case AddonId::Fusion:
        return AddonVerb::Fuse;
    case AddonId::Dash:
    case AddonId::Analyzer:
    case AddonId::Lantern:
    case AddonId::WallKick:
        return AddonVerb::UseGear;
    default:
        return AddonVerb::None;
    }
}

AddonEdit addon_apply_requests(const AddonView& world) {
    const int cycle = g_cycle.exchange(0);
    if (cycle != 0) {
        int next = g_selected + (cycle > 0 ? 1 : -1);
        if (next < 1) {
            next = kCount - 1;
        }
        if (next >= kCount) {
            next = 1;
        }
        g_selected = next;
    }
    AddonEdit edit;
    if (g_special.exchange(0) > 0) {
        const AddonId id = static_cast<AddonId>(g_selected);
        const AddonVerb verb = special_for(id);
        if (verb == AddonVerb::None) {
            edit.note = std::string(addon_name(id)) + " has no field action.";
        } else if (id == AddonId::DeepGate && g.deep_inside) {
            edit = deep_extract();
        } else {
            edit = addon_command(verb, id, world);
        }
    }
    if (g_confirm.exchange(0) > 0) {
        const AddonId id = static_cast<AddonId>(g_selected);
        const AddonVerb verb = g.built[index_of(id)] ? AddonVerb::Upgrade : AddonVerb::Build;
        merge_edit(edit, addon_command(verb, id, world));
    }
    return edit;
}

AddonEdit addon_tick(const AddonView& world) {
    AddonEdit edit;
    grant_opening_junk(world, edit);
    if (world.area != g.last_area) {
        g.last_area = world.area;
        if (g.built[index_of(AddonId::Sonar)]) {
            merge_edit(edit, ping(world));
        }
    }
    AddonEdit hurt;
    on_hurt(world, hurt);
    merge_edit(edit, hurt);
    AddonEdit gear;
    gear_tick(world, gear);
    merge_edit(edit, gear);
    return edit;
}

std::string addon_save_blob() {
    std::ostringstream out;
    out << "v1";
    for (int i = 1; i < kCount; ++i) {
        out << '|' << (g.junk[i] ? 1 : 0) << (g.built[i] ? 1 : 0) << g.level[i];
    }
    out << ';' << g.deep_floor << ',' << g.deep_unbanked << ',' << (g.deep_inside ? 1 : 0);
    return out.str();
}

void addon_load_blob(const std::string& blob) {
    addon_reset();
    if (blob.rfind("v1", 0) != 0) {
        return;
    }
    const auto semi = blob.find(';');
    const std::string head = blob.substr(0, semi);
    std::istringstream items(head);
    std::string piece;
    std::getline(items, piece, '|');
    for (int i = 1; i < kCount && std::getline(items, piece, '|'); ++i) {
        if (piece.size() < 2) {
            continue;
        }
        g.junk[i] = piece[0] == '1';
        g.built[i] = piece[1] == '1';
        g.level[i] = std::max(0, std::atoi(piece.c_str() + 2));
    }
    if (semi != std::string::npos) {
        int inside = 0;
        std::sscanf(blob.c_str() + semi + 1, "%d,%d,%d", &g.deep_floor, &g.deep_unbanked, &inside);
        g.deep_inside = inside != 0;
    }
}

void addon_reset() {
    g = State{};
}

} // namespace remaster
