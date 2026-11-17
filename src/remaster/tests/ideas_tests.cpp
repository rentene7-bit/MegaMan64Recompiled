#include "remaster.hpp"
#include "upscale.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

namespace {

int g_failed = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL " << message << "\n";
        g_failed++;
    }
}

} // namespace

int run_idea_tests() {
    remaster::set_difficulty(remaster::Difficulty::Easy);
    expect(remaster::damage_taken_q8() < 256, "easy reduces damage taken");
    expect(remaster::scale_damage_amount(false, true, 8) == 8, "damage stays put until the player is known");
    expect(remaster::scale_damage_amount(true, true, 8) == 6, "easy scales a player hit by three quarters");
    expect(remaster::scale_damage_amount(true, true, 1) == 1, "a chip of damage is never scaled away");
    expect(remaster::scale_damage_amount(true, false, 8) > 8, "easy raises damage dealt to other actors");
    remaster::note_actor_hp(7, 40);
    remaster::note_actor_hp(7, 25);
    expect(remaster::actor_hp(7) == 25, "actor hp follows the later report");
    expect(remaster::actor_max_hp(7) == 40, "actor max stays at the highest hp seen");
    remaster::set_new_game_plus(true);
    expect(remaster::damage_dealt_q8() > 256, "new game plus raises damage dealt");
    remaster::set_new_game_plus(false);
    remaster::set_difficulty(remaster::Difficulty::Normal);

    remaster::set_aim_assist(100);
    expect(remaster::soften_aim(0.1f) == 0.1f, "aim assist leaves small sticks alone");
    expect(std::fabs(remaster::soften_aim(1.0f)) < 1.0f, "aim assist softens a flick");
    remaster::set_aim_mode(remaster::AimMode::Both);
    expect(remaster::aim_uses_gyro(), "both aim mode uses gyro");
    remaster::set_aim_mode(remaster::AimMode::Stick);
    expect(!remaster::aim_uses_gyro(), "stick aim does not require gyro");

    remaster::set_colorblind(remaster::Colorblind::Deuteranopia);
    expect(std::string(remaster::door_mark(0)).find("circle") != std::string::npos, "colorblind doors add a shape");
    remaster::set_colorblind(remaster::Colorblind::Off);

    remaster::set_narration(true);
    const std::string spoken = remaster::narration_command("Hello; rm -rf /");
    expect(spoken.find(';') == std::string::npos && spoken.find('/') == std::string::npos, "narration drops shell punctuation");
    expect(remaster::narration_command("Save menu").find("Save") != std::string::npos, "narration keeps menu words");
    remaster::set_narration(false);
    expect(remaster::narration_command("Save menu").empty(), "narration stays quiet while off");

    remaster::set_motion_comfort(true);
    expect(remaster::fade_steps() == 8, "motion comfort shortens fades");
    remaster::set_motion_comfort(false);
    expect(remaster::fade_steps() == 4, "fades return to the remaster pace");

    remaster::Loadout loadout;
    loadout.name = "ruins";
    loadout.parts[0] = 2;
    expect(remaster::save_loadout(loadout), "loadout saves");
    expect(remaster::loadouts().size() == 1, "loadout can be swapped back");

    remaster::mark_gate("north gate");
    remaster::mark_gate("north gate");
    expect(remaster::visited_gates().size() == 1, "fast travel remembers a gate once");

    remaster::set_quest_done("side_3", false, true);
    expect(remaster::missables_open() == 1, "missable stays open");
    remaster::set_quest_done("side_3", true, true);
    expect(remaster::quests_done() == 1, "quest can be completed");

    const remaster::StatBlock now{10, 4, 2};
    const remaster::StatBlock next{12, 4, 1};
    expect(remaster::compare_stats(now, next).find("attack 2") != std::string::npos, "development preview compares stats");

    remaster::set_randomizer_seed("deck");
    const auto first = remaster::randomizer_order(8);
    const auto second = remaster::randomizer_order(8);
    expect(first == second, "randomizer seed is shareable");
    expect(remaster::randomizer_share_code() == "MM64R-deck", "share code keeps the seed");
    remaster::set_randomizer_seed("");
    bool identity = true;
    const auto plain = remaster::randomizer_order(4);
    for (int i = 0; i < 4; i++) identity = identity && plain[static_cast<size_t>(i)] == i;
    expect(identity, "empty seed does not shuffle");

    remaster::start_speedrun();
    remaster::set_loading(true);
    remaster::tick_frame(1.0);
    expect(remaster::speedrun_clock() == "00:00:00.000", "load time is not counted");
    remaster::set_loading(false);
    remaster::tick_frame(1.5);
    remaster::split_speedrun("Gate");
    expect(remaster::livesplit_xml().find("Gate") != std::string::npos, "livesplit file contains the split");
    remaster::reset_speedrun();

    remaster::record_ghost_sample(0x8000, 10, -10);
    expect(remaster::ghost_sample_count() == 1, "ghost stores an input sample");
    expect(remaster::input_display(0x8000) == "A", "input display names the button");
    expect(remaster::build_stamp().find("Remaster") != std::string::npos, "build stamp says Remaster");

    expect(remaster::lock_on_move_enabled(), "lock-on movement stays available");
    remaster::cycle_weapon(1);
    expect(remaster::weapon_slot() == 1, "weapon wheel cycles");
    remaster::set_weapon_wheel_open(true);
    expect(remaster::weapon_wheel_open(), "weapon wheel can open");

    remaster::set_seamless(true);
    expect(remaster::seamless_budget_mb() > 0, "seamless mode gets a memory budget");
    remaster::set_seamless(false);
    expect(remaster::seamless_budget_mb() == 0, "seamless mode can be disabled");
    remaster::set_dynamic_lights(remaster::EffectLevel::Off);
    remaster::set_shadows(remaster::EffectLevel::Off);
    expect(remaster::dynamic_lights() == remaster::EffectLevel::Off, "lights default off");
    expect(remaster::shadows() == remaster::EffectLevel::Off, "shadows default off");

    remaster::set_enemy_hp(3, 5, 10);
    expect(remaster::actor_hp(3) == 5, "enemy bar stores the reported hp");
    expect(remaster::enemy_bar_count() >= 1, "enemy bar accepts a report");
    remaster::unlock_achievement("first_gate");
    remaster::unlock_achievement("first_gate");
    expect(remaster::achievements_unlocked() == 1, "achievements unlock once");
    remaster::note_bestiary(4);
    expect(remaster::bestiary_seen() == 1, "bestiary records a sighting");
    remaster::set_challenge_mask(0x5);
    expect(remaster::challenge_rank(600) > 0, "challenge rank uses the modifiers");

    remaster::set_boss_rush(2);
    expect(remaster::boss_rush_index() == 2, "boss rush remembers the selected rematch");
    remaster::set_boss_rush(-1);
    expect(remaster::boss_rush_index() == -1, "boss rush can be cleared");

    remaster::enter_photo_mode();
    expect(remaster::photo_mode(), "photo mode can start");
    remaster::exit_photo_mode();

    const uint8_t solid[8] = {0xFF, 0xFF, 0x00, 0x01, 0x00, 0x01, 0xF8, 0x01};
    remaster::upscale::Image tiny = remaster::upscale::from_rgba16(solid, 2, 2);
    remaster::upscale::Image doubled = remaster::upscale::epx2(tiny);
    expect(doubled.width == 4 && doubled.height == 4, "epx doubles a texture");
    const auto png = remaster::upscale::write_png(doubled);
    expect(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "upscale writes a png");
    expect(remaster::upscale::scan_cap_for_budget(512ull << 20, 2) > remaster::upscale::scan_cap_for_budget(512ull << 20, 4), "x4 scans fewer textures than x2");
    expect(remaster::upscale::scan_cap_for_budget(0, 2) == 0, "no budget means no upscale work");

    uint8_t rom[4096] = {};
    for (int i = 0; i < 32 * 32; i++) {
        const uint16_t pixel = (i % 3 == 0) ? 0xF801 : 0x07C1;
        rom[i * 2] = static_cast<uint8_t>(pixel >> 8);
        rom[i * 2 + 1] = static_cast<uint8_t>(pixel);
    }
    const auto hits = remaster::upscale::scan_rom(rom, sizeof(rom), 2, 2);
    expect(!hits.empty(), "rom scan keeps a varied block from the user's rom");
    expect(hits[0].image.width == 64, "scanned texture is upscaled on device");

    expect(!remaster::dual_audio_active(), "one disc is not dual audio");
    expect(remaster::voice_disc_index() == 0, "voices follow the only disc");
    expect(remaster::music_layers(80) == 1, "adaptive layers stay single until sequenced music is enabled");
    expect(remaster::compare_stats(now, next).size() > 0, "stat compare stays available");

    if (g_failed != 0) {
        std::cerr << g_failed << " idea failure(s)\n";
    }
    return g_failed;
}
