#include "addons.hpp"
#include "addon_tools.hpp"

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

    ShotSpawn light;
    light.charge_built = true;
    light.charge_meter = 2;
    light.half = kShotLight;
    ShotSpawnOut still = adjust_shot_spawn(light);
    expect(still.half == kShotLight && !still.heavy, "a short hold stays a light shot");
    light.charge_meter = 3;
    ShotSpawnOut heavy = adjust_shot_spawn(light);
    expect(heavy.half == kShotHeavy && heavy.heavy && heavy.zero_rapid, "a full hold is the heavy shot");

    ShotHit hit;
    hit.alive = 0x80;
    hit.vel_x = 5;
    hit.vel_z = -2;
    hit.pierce = true;
    ShotHitOut passed = adjust_shot_hit(hit);
    expect(passed.keep && passed.alive == 0 && (passed.tag & 1) != 0, "pierce keeps the shot");
    hit.tag = passed.tag;
    hit.pierce = false;
    hit.bounce = true;
    ShotHitOut bounced = adjust_shot_hit(hit);
    expect(bounced.keep && bounced.vel_x == -5 && bounced.vel_z == 2, "bounce reverses the shot");
    hit.tag = bounced.tag;
    hit.bounce = false;
    hit.arc = true;
    hit.vel_x = 5;
    ShotHitOut chained = adjust_shot_hit(hit);
    expect(chained.chain && chained.keep, "arc chain lets the shot continue");
    hit.tag = chained.tag;
    ShotHitOut done = adjust_shot_hit(hit);
    expect(!done.keep, "a shot dies after pierce, bounce, and arc");

    OutgoingHit blow;
    blow.amount = 4;
    blow.heavy = true;
    blow.chain = true;
    OutgoingHitOut scaled = adjust_outgoing_hit(blow);
    expect(scaled.amount == 24 && !scaled.heavy_left && !scaled.chain_left, "heavy and chain multiply once");
    blow.victim_is_player = true;
    OutgoingHitOut player_hit = adjust_outgoing_hit(blow);
    expect(player_hit.amount == 4, "player hits are not boosted");

    expect(fusion_part_byte(9, 4) == 4, "fusion keeps the smaller part byte");
    expect(fusion_part_byte(0, 0) == 1, "an empty pair writes the low hybrid");

    MoveStep move;
    move = step_motion(move, true, false, false);
    expect(move.vel_x == 12 && move.dash_left == 7, "dash adds the existing velocity step");
    MoveStep kick;
    kick = step_motion(kick, false, true, true);
    expect(kick.vel_z == 12 && kick.kicked, "wall kick only fires in a shaft");
    MoveStep blocked = step_motion(MoveStep{}, false, true, false);
    expect(blocked.vel_z == 0 && !blocked.kicked, "wall kick does not fire in an old room");

    addon_reset();
    world = rich();
    addon_command(AddonVerb::FindJunk, AddonId::Sonar, world);
    addon_command(AddonVerb::Build, AddonId::Sonar, world);
    expect(tools::sonar(), "sonar() matches the built tool");
    expect(tools::tool_hooks().sonar && !tools::fusion(), "ToolHooks reports sonar only");
    expect(tools::kFlagLo == 0xC00 && tools::kFlagHi == 0xCFF, "published flag range is 0xC00-0xCFF");
    addon_command(AddonVerb::Ping, AddonId::Sonar, world);
    addon_command(AddonVerb::OpenVault, AddonId::Vaults, world);
    addon_command(AddonVerb::DeepPush, AddonId::DeepGate, world);
    expect(tools::deep_gate_floor() == 1, "deep gate floor is visible to the other branch");
    expect(addon_take_scene_reload(), "descending requests the scene reload");
    tools::note_deep_gate_floor(4);
    expect(tools::deep_gate_floor() == 4, "the other branch can record a floor");
    OutgoingHit soaked;
    soaked.amount = 8;
    soaked.victim_is_player = true;
    soaked.decoy = true;
    expect(adjust_outgoing_hit(soaked).amount == 4, "decoy halves damage taken");

    OutgoingHit cold;
    cold.amount = 5;
    cold.coolant = true;
    OutgoingHitOut slowed = adjust_outgoing_hit(cold);
    expect(slowed.amount == 10 && slowed.zero_velocity, "coolant hits harder and stops the victim");

    ShotHit disc_hit;
    disc_hit.alive = 0x80;
    disc_hit.vel_x = 4;
    disc_hit.disc = true;
    ShotHitOut bounce1 = adjust_shot_hit(disc_hit);
    disc_hit.tag = bounce1.tag;
    ShotHitOut bounce2 = adjust_shot_hit(disc_hit);
    disc_hit.tag = bounce2.tag;
    ShotHitOut bounce3 = adjust_shot_hit(disc_hit);
    disc_hit.tag = bounce3.tag;
    expect(bounce1.keep && bounce2.keep && bounce3.keep, "the disc bounces three times");
    expect(!adjust_shot_hit(disc_hit).keep, "the disc dies on the fourth impact");
    OutgoingHit disc_damage;
    disc_damage.amount = 2;
    disc_damage.disc = true;
    expect(adjust_outgoing_hit(disc_damage).amount == 6, "disc hits deal triple damage");

    ShotSpawn overheated;
    overheated.locked = true;
    overheated.half = kShotLight;
    expect(adjust_shot_spawn(overheated).locked && adjust_shot_spawn(overheated).half == 0, "overclock lock suppresses the shot");

    OutgoingHit skilled;
    skilled.amount = 4;
    skilled.mastery = 2;
    expect(adjust_outgoing_hit(skilled).amount == 6, "mastery adds its perk");

    OutgoingHit soft;
    soft.amount = 8;
    soft.bounce_penalty = true;
    expect(adjust_outgoing_hit(soft).amount == 6, "bounce lens lowers attack");

    OutgoingHit yanked;
    yanked.amount = 3;
    yanked.yank = true;
    OutgoingHitOut pull = adjust_outgoing_hit(yanked);
    expect(pull.amount == 6 && !pull.yank_left, "grapple yank spends itself");

    OutgoingHit pulse;
    pulse.amount = 4;
    pulse.stagger = 2;
    OutgoingHitOut pulsed = adjust_outgoing_hit(pulse);
    expect(pulsed.zero_velocity && pulsed.stagger_left == 1, "sonar stagger stops the victim");

    OutgoingHit shove;
    shove.amount = 4;
    shove.resonance = true;
    expect(adjust_outgoing_hit(shove).knockback == 8, "resonance knocks the victim back");

    addon_reset();
    expect(lantern_fog_bonus() == 0, "lantern is dark until it is built");
    world = rich();
    addon_command(AddonVerb::FindJunk, AddonId::Lantern, world);
    addon_command(AddonVerb::Build, AddonId::Lantern, world);
    expect(lantern_fog_bonus() == 150, "lantern pushes fog");

    addon_reset();
    world = rich();
    addon_command(AddonVerb::FindJunk, AddonId::Magnet, world);
    addon_command(AddonVerb::Build, AddonId::Magnet, world);
    world.zenny = 40;
    addon_tick(world);
    world.zenny = 80;
    AddonEdit caught = addon_tick(world);
    expect(caught.zenny_delta == 10, "magnet adds a quarter of a small gain");

    addon_reset();
    world = rich();
    addon_command(AddonVerb::FindJunk, AddonId::Sentry, world);
    addon_command(AddonVerb::Build, AddonId::Sentry, world);
    addon_note_victim(0x802049B0);
    int sentry_hits = 0;
    for (int i = 0; i < 45; ++i) {
        sentry_hits += addon_take_sentry_damage();
    }
    expect(sentry_hits == 4, "sentry chips the last actor once it is due");

    return g_failed;
}
