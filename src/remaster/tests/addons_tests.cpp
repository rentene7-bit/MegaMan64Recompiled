#include "addons.hpp"

#include <iostream>
#include <string>

namespace {

int g_failed = 0;

void expect(bool ok, const char* name) {
    if (!ok) {
        std::cerr << "FAIL " << name << "\n";
        g_failed += 1;
    }
}

remaster::AddonView rich() {
    remaster::AddonView world;
    world.area = 0;
    world.zenny = 500000;
    world.hp = 80;
    world.tutorial_done = true;
    return world;
}

} // namespace

int run_addon_tests() {
    using namespace remaster;
    addon_reset();

    AddonView world = rich();
    AddonEdit tick = addon_tick(world);
    expect(tick.note.find("speaker") != std::string::npos, "tutorial grants sonar junk");
    expect(!addon_built(AddonId::Sonar), "sonar is not built yet");

    AddonEdit built = addon_command(AddonVerb::Build, AddonId::Sonar, world);
    expect(built.zenny_delta == -1500, "sonar costs 1500");
    expect(addon_built(AddonId::Sonar), "sonar is built");
    expect(addon_built_flag(AddonId::Sonar) == kAddonFlagOrigin + 2, "sonar flag is 0xC02");

    world.area = 0;
    addon_reset();
    addon_command(AddonVerb::FindJunk, AddonId::Sonar, world);
    addon_command(AddonVerb::Build, AddonId::Sonar, world);
    AddonEdit marked = addon_command(AddonVerb::Ping, AddonId::Sonar, world);
    expect(marked.flag_count == 1, "ping sets a mark flag");
    AddonEdit vault = addon_command(AddonVerb::OpenVault, AddonId::Vaults, world);
    expect(vault.zenny_delta == 800, "area 0 vault pays 800");
    AddonEdit again = addon_command(AddonVerb::OpenVault, AddonId::Vaults, world);
    expect(again.zenny_delta == 0, "vault does not pay twice");

    world.area = 0x13;
    AddonEdit need_grapple = addon_command(AddonVerb::Ping, AddonId::Sonar, world);
    expect(need_grapple.flag_count == 1, "second ruin pings");
    AddonEdit locked = addon_command(AddonVerb::OpenVault, AddonId::Vaults, world);
    expect(locked.zenny_delta == 0, "grapple vault stays shut");
    addon_command(AddonVerb::FindJunk, AddonId::Grapple, world);
    addon_command(AddonVerb::Build, AddonId::Grapple, world);
    AddonEdit opened = addon_command(AddonVerb::OpenVault, AddonId::Vaults, world);
    expect(opened.zenny_delta == 1200, "grapple vault pays 1200");

    addon_command(AddonVerb::FindJunk, AddonId::Magnet, world);
    addon_command(AddonVerb::Build, AddonId::Magnet, world);
    world.area = 0x1A;
    addon_command(AddonVerb::FindJunk, AddonId::Arc, world);
    addon_command(AddonVerb::Build, AddonId::Arc, world);
    addon_command(AddonVerb::Ping, AddonId::Sonar, world);
    AddonEdit magnet = addon_command(AddonVerb::OpenVault, AddonId::Vaults, world);
    expect(magnet.zenny_delta == 800 + 800 + 200, "arc vault adds the magnet bonus");

    addon_reset();
    world = rich();
    world.vanilla_right_chest = true;
    addon_tick(world);
    expect(addon_status_line(AddonId::Grapple).find("junk") != std::string::npos, "right chest leaves grapple junk");

    addon_command(AddonVerb::FindJunk, AddonId::Sonar, world);
    addon_command(AddonVerb::Build, AddonId::Sonar, world);
    addon_command(AddonVerb::Ping, AddonId::Sonar, world);
    addon_command(AddonVerb::OpenVault, AddonId::Vaults, world);
    AddonEdit floor1 = addon_command(AddonVerb::DeepPush, AddonId::DeepGate, world);
    expect(floor1.zenny_delta == 0, "deep gate does not bank on the way down");
    addon_command(AddonVerb::DeepPush, AddonId::DeepGate, world);
    AddonEdit banked = addon_command(AddonVerb::DeepExtract, AddonId::DeepGate, world);
    expect(banked.zenny_delta == 150 + 300, "two floors bank 450");

    AddonEdit down = addon_command(AddonVerb::DeepPush, AddonId::DeepGate, world);
    expect(down.zenny_delta == 0, "third floor is unbanked");
    world.hp = 0;
    AddonEdit dead = addon_tick(world);
    expect(dead.note.find("Unbanked") != std::string::npos, "death drops unbanked loot");
    AddonEdit empty = addon_command(AddonVerb::DeepExtract, AddonId::DeepGate, world);
    expect(empty.zenny_delta == 0, "extract after death banks nothing");

    addon_reset();
    world = rich();
    addon_command(AddonVerb::FindJunk, AddonId::Pierce, world);
    addon_command(AddonVerb::FindJunk, AddonId::Bounce, world);
    addon_command(AddonVerb::Build, AddonId::Pierce, world);
    addon_command(AddonVerb::Build, AddonId::Bounce, world);
    AddonEdit early = addon_command(AddonVerb::Fuse, AddonId::Fusion, world);
    expect(!addon_built(AddonId::Fusion), "fusion waits for maxed parts");
    addon_command(AddonVerb::Upgrade, AddonId::Pierce, world);
    addon_command(AddonVerb::Upgrade, AddonId::Bounce, world);
    AddonEdit fused = addon_command(AddonVerb::Fuse, AddonId::Fusion, world);
    expect(fused.zenny_delta == -20000, "fusion spends 20,000");
    expect(addon_built(AddonId::Fusion), "hybrid exists");
    expect(addon_level(AddonId::Pierce) == 0, "low-tier parts are consumed");

    const std::string blob = addon_save_blob();
    addon_reset();
    expect(!addon_built(AddonId::Fusion), "reset clears fusion");
    addon_load_blob(blob);
    expect(addon_built(AddonId::Fusion), "save blob restores fusion");
    addon_load_blob("nope");
    expect(!addon_built(AddonId::Fusion), "bad blob does not keep stale state");

    addon_reset();
    world = rich();
    world.hp = 20;
    addon_command(AddonVerb::FindJunk, AddonId::Relay, world);
    addon_command(AddonVerb::Build, AddonId::Relay, world);
    AddonEdit heal = addon_tick(world);
    expect(heal.hp_delta == 40, "pocket relay heals once");
    AddonEdit second = addon_tick(world);
    expect(second.hp_delta == 0, "pocket relay does not repeat in the same ruin");

    AddonEdit poor = addon_command(AddonVerb::Build, AddonId::Sentry, AddonView{});
    expect(poor.zenny_delta == 0, "a build without junk or zenny spends nothing");

    return g_failed;
}
