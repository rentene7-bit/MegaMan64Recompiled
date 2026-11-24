#include "remaster_addons.hpp"

#include <cstdlib>
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

int g_tracks = 0;
bool g_played = false;
int count_tracks(int) { return g_tracks; }
bool play_track(int, int track) {
    g_played = track >= 0;
    return g_played;
}

} // namespace

int main() {
    using namespace remaster::addons;
    reset();
    expect(!sparring_available(), "sparring waits for the story");
    note_story_complete(true);
    note_zenny(50000);
    expect(start_sparring(0), "first machine starts");
    expect(!start_sparring(1), "one match at a time");
    expect(strike_sparring(40) == 0, "the drill goes down");
    expect(sparring_prize_part(0) == 41, "prize is a part, not a story flag");
    expect(start_sparring(0), "a cleared machine can be fought again");
    strike_sparring(40);

    expect(accept_contract(0), "guild contract accepted");
    expect(!accept_contract(1), "one contract at a time");
    expect(!turn_in_contract(), "an unfinished contract pays nothing");
    expect(report_contract_progress(4), "hunt goal reached");
    const int before = zenny();
    expect(turn_in_contract(), "contract pays");
    expect(zenny() == before + 800, "hunt payout is 800");
    expect(accept_contract(0), "a finished contract can be taken again");

    expect(find_servbot(0), "first servbot returns");
    expect(!find_servbot(0), "the same servbot is not paid twice");
    for (int i = 1; i < 6; i++) find_servbot(i);
    expect(servbots_returned() == 6, "six servbots");
    expect(servbot_reward_zenny() == 500, "first tier pays 500");

    expect(!fund_rebuild(1), "the first stage needs two supplies");
    expect(fund_rebuild(2), "rubble stage is funded");
    expect(rebuild_stage() == 1, "stage advances");
    expect(std::string(rebuild_shop(0)) == "Scrap counter", "the finished stage opens its shop");
    note_zenny(100000);
    expect(fund_rebuild(3) && fund_rebuild(4) && fund_rebuild(5) && fund_rebuild(6), "the district can be finished");
    expect(rebuild_stage() == 5, "five stages");
    expect(paint_slot_unlocked(3), "finishing the city unlocks the leg palette");

    expect(set_paint(0, Rgb{10, 20, 30}), "helmet paint is open");
    expect(paint(0).b == 30, "paint stores the blue channel");
    expect(!set_paint(1, Rgb{1, 2, 3}), "a locked slot rejects paint");
    unlock_paint_slot(1);
    expect(set_paint(1, Rgb{300, -4, 8}), "colors clamp into 0..255");
    expect(paint(1).r == 255 && paint(1).g == 0, "clamp holds");

    expect(!complete_photo(0, 0x1A, false), "photo jobs need photo mode");
    expect(complete_photo(0, 0x1A, true), "the gate shot pays");
    expect(!complete_photo(0, 0x1A, true), "a shot is not paid twice");

    note_zenny(1000);
    const Appraisal junk = appraise(3);
    expect(junk.ok && junk.fee == 300, "rarity 3 costs 300");
    expect(junk.name.find("coil") != std::string::npos, "the appraiser names the junk");
    expect(sell_refractors(25, 1) == 20 * 55, "the daily cap is 20 shards");
    expect(sell_refractors(5, 1) == 0, "the cap holds for the rest of the day");

    expect(record_range_score(2, 40) == 40, "range stores a score");
    expect(record_range_score(2, 10) == 40, "a lower score does not replace the best");
    expect(read_journal(0), "a journal page can be read");
    for (int i = 1; i < 8; i++) read_journal(i);
    expect(journals_complete(), "eight pages open the vault flag");
    expect(std::string(journal_page(2)).find("Zenny") != std::string::npos, "journal text is original");

    set_disc_player(count_tracks, play_track);
    expect(find_music_disc(1), "a disc can be found");
    expect(!play_music_disc(1).ready, "no disc image means no audio");
    g_tracks = 6;
    expect(play_music_disc(1).ready && g_played, "a loaded disc plays the track");

    reset();
    expect(epilogue().find("dark") != std::string::npos, "an empty record gets the quiet ending");
    note_zenny(100000);
    reset();
    note_story_complete(true);
    note_zenny(100000);
    for (int i = 0; i < 5; i++) fund_rebuild(6);
    for (int i = 0; i < 4; i++) {
        start_sparring(0);
        strike_sparring(40);
    }
    expect(epilogue().find("lights") != std::string::npos, "a rebuilt city gets the bright ending");

    reset();
    expect(sunken_move_scale() == 1.0f, "dry ground is full speed");
    enter_sunken();
    expect(sunken_move_scale() < 0.5f, "floodwater slows movement");
    equip_water_gear();
    expect(sunken_move_scale() > 0.5f && sunken_move_scale() < 1.0f, "water gear helps and does not fully restore speed");
    expect(!sunken_chest_marked(), "sonar is not assumed");
    ToolHooks hooks;
    hooks.sonar = true;
    set_tool_hooks(hooks);
    expect(sunken_chest_marked(), "the sonar stub marks the sunken chest");

    note_zenny(0);
    expect(start_collapse(900, 1.0), "a collapse dig starts");
    tick(0.4);
    expect(escape_collapse() && zenny() == 900, "leaving in time banks the prize");
    expect(start_collapse(900, 0.2), "another dig starts");
    tick(0.1);
    tick(0.1);
    expect(!escape_collapse(), "a sealed dig pays nothing");

    const auto remix = mirror_remix(3);
    expect(remix.size() == 4 && remix[0].count >= 1, "mirror ruins reuse enemy counts");
    const EliteHit blocked = strike_elite(EliteKind::Shield, 20, 5, true, true);
    expect(!blocked.damaged && blocked.hp == 20, "a shield ignores a front hit");
    const EliteHit split = strike_elite(EliteKind::Split, 20, 12, false, true);
    expect(split.spawned == 2, "a split elite leaves two");
    expect(open_chest(true) == ChestOpen::Safe, "sonar makes a mimic safe");
    hooks.sonar = false;
    set_tool_hooks(hooks);
    expect(open_chest(true) == ChestOpen::Fight, "a hidden mimic fights");
    expect(open_chest(false) == ChestOpen::Loot, "a real chest is loot");

    note_deep_gate_floor(20);
    const Custodian boss = custodian_for_floor(deep_gate_floor());
    expect(boss.active && boss.hp > 80, "the custodian appears on a tenth floor");
    expect(!custodian_for_floor(19).active, "other floors are quiet");
    expect(!titan_due(100), "the titan waits");
    expect(titan_due(600), "the titan is due after ten minutes");
    const TitanDrop plain = defeat_titan();
    expect(!plain.material && plain.zenny == 2500, "without fusion the titan pays zenny");
    hooks.fusion = true;
    set_tool_hooks(hooks);
    expect(defeat_titan().material, "fusion turns the drop into material");

    const RaceResult won = finish_race(10, 12, 1000);
    expect(won.won && won.buyout == 0, "a fast race wins the prize");
    const RaceResult lost = finish_race(20, 12, 1000);
    expect(!lost.won && lost.buyout == 1500, "a loss offers a markup");

    expect(add_flutter_part(7).added, "a flutter part is stored");
    add_flutter_part(7);
    add_flutter_part(8);
    expect(add_flutter_part(9).lab && lab_ready(), "three parts open the lab for fusion");
    expect(flutter_trophy(), "the trophy room opens with the lab");

    note_zenny(0);
    const Delivery late = finish_delivery(30, 20, true);
    expect(late.finished && late.pay == 400, "a late skate run pays the base");
    const Delivery early = finish_delivery(10, 20, true);
    expect(early.pay == 650, "a fast run pays the bonus");
    expect(!finish_delivery(10, 20, false).finished, "deliveries need skates");

    const Catch fish = reel_fish(1, 1.0);
    expect(fish.name[0] != '\0', "a cast returns something");
    expect(std::string(reel_fish(2, 0.1).name).find("snapped") != std::string::npos, "a short reel snaps");

    const auto flags = flag_writes();
    bool used_neighbor_range = false;
    for (int id : flags) {
        if (id >= kNeighborFlagLo && id <= kNeighborFlagHi) used_neighbor_range = true;
        expect(id >= kFlagLo && id <= kFlagHi, "new flags stay in the add-on B range");
    }
    expect(!used_neighbor_range, "add-on A flags 0xC00-0xCFF are not written");
    NeighborView bits;
    bits.sonar = true;
    note_neighbor(bits);
    expect(tool_hooks().sonar, "sonar flag 0xC02 turns the stub on");
    bits = NeighborView{};
    bits.fusion = true;
    bits.deep_gate = true;
    note_deep_gate_floor(3);
    note_neighbor(bits);
    expect(tool_hooks().fusion, "fusion flag 0xC0D turns the stub on");
    expect(deep_gate_floor() == 3, "the 0xCA0 bit does not replace the floor");
    reset();
    note_zenny(0);
    on_world(0x1A, 2, 0);
    expect(spawn_request_count() == 1, "the rival is queued; remix enemies are not invented");
    expect(on_hurt(5, false, 5) == 0, "the tagged actor's damage stays on the encounter");
    expect(claim_actor(5) > 0, "that actor keeps the elite health");
    expect(on_hurt(9, false, 5) == 5, "a different actor is not the elite");
    on_world(0x1B, 6, 0);
    expect(zenny() == 1000, "leaving the gate finishes the rival race");
    expect(on_hurt(5, false, 5) == 5, "leaving the gate releases the tagged actor");

    const std::string saved = serialize();
    reset();
    expect(zenny() == 0, "reset clears zenny");
    expect(deserialize(saved), "save text loads");
    expect(checklist().find("#29 done") != std::string::npos, "sparring is marked done");
    expect(checklist().find("#28 done") != std::string::npos, "the mimic hook is marked done");
    expect(checklist().find("#24 done") != std::string::npos, "sunken movement is marked done");
    expect(checklist().find("#26 done") != std::string::npos, "enemy remix is marked done");
    expect(checklist().find("#30 done") != std::string::npos, "the custodian room is marked done");
    expect(checklist().find("#41 done") != std::string::npos, "the lake shore is marked done");
    reset();
    NeighborView deep;
    deep.deep_gate = true;
    note_neighbor(deep);
    expect(claim_actor(4) < 0, "the custodian does not replace an actor in an unknown room");
    on_world(0x1A, 0, 0);
    expect(on_hurt(7, false, 5) == 0, "an actor outside the titan timer is not killed");
    reset();
    on_world(0x1A, 0, 36000);
    expect(on_hurt(3, false, 200) == 32767, "the killing blow is applied to the titan actor");
    expect(zenny() == 2500, "defeating that actor pays the titan drop");
    expect(settle_zenny(400) == 2900, "the titan payout is added to the game zenny");
    reset();
    note_zenny(2500);
    on_world(0x12, 1, 0);
    expect(rebuild_stage() == 1, "entering the old city funds one stage");
    expect(zenny() == 500, "the rubble stage costs 2000");
    expect(scene_tint().active && scene_tint().r == 150, "the rebuild warms the scene");
    expect(settle_zenny(2500) == 500, "the game zenny is charged the stage cost");
    reset();
    NeighborView dock;
    dock.grapple = true;
    dock.sonar = true;
    dock.fusion = true;
    note_neighbor(dock);
    expect(!lab_ready(), "the lab stays shut away from the Flutter");
    on_world(0x1B, 1, 0);
    expect(lab_ready(), "docking with grapple, sonar, and fusion opens the lab");

    reset();
    expect(kPlayerPressedGuest - kPlayerGuest == static_cast<uint32_t>(kPlayerButtonOffset), "the action word is player+0x11E");
    expect(kPlayerButtonOffset != 0x7C, "buttons are not read from player+0x7C");
    expect(kActionButton == 0x2000 && kJumpButton == 0x4000 && kFireButton == 0x8000, "C-down acts, A jumps, and B fires");
    expect(sunken_area(0x1D) && !sunken_area(0x19) && !sunken_area(0x13), "water is 0x1D, not the streets or the dry corridors");
    expect(!sunken_room(0x1D, 3) && sunken_room(0x1D, 0) && sunken_room(0x1D, 2), "only rooms 0-2 are flooded");
    expect(scale_axis(0x1A, 0, false, 100) == 100, "dry ground keeps its speed");
    expect(scale_axis(0x19, 0, false, 100) == 100, "city streets are not slowed");
    expect(scale_axis(0x1D, 0, false, 100) == 45, "floodwater scales the axis");
    expect(scale_axis(0x1D, 1, true, 100) == 85, "water gear keeps more speed");
    expect(scale_axis(0x1D, 2, false, -100) == -45, "the scale keeps the sign");
    expect(scale_axis(0x1D, 5, false, 100) == 100, "a dry room in the area is full speed");
    reset();
    enter_sunken();
    expect(on_hurt(1, true, 40) == 16, "a bare underwater hit is capped");
    expect(sunken_hp_after(80, 48) == 64, "a 32 point bypass is pulled back to the cap");
    equip_water_gear();
    expect(on_hurt(1, true, 40) == 8, "water gear softens the hit further");
    expect(kEnemyLiveMask == 0x801D8EB4u, "the live mask is the sign-extended symbol");

    reset();
    expect(designated_mimic_chest() == 0x41, "flag 0x441 is the mimic");
    expect(!consider_chest(0x40).fight && consider_chest(0x41).fight, "0x440 stays loot and 0x441 fights");
    expect(consider_chest(0x41, 0x00, 2).skip_reward, "the mimic is area 0x00 room 2");
    expect(!consider_chest(0x41, 0x00, 1).fight, "the same id in another room is not the mimic");
    expect(!consider_chest(0x40, 0x00, 2).skip_reward, "its twin in the same room stays a normal chest");
    designate_mimic_chest(0x40);
    expect(!consider_chest(0x40).skip_reward, "0x40 cannot be turned into a mimic");
    designate_mimic_chest(7);
    expect(consider_chest(7).skip_reward && consider_chest(7).fight, "the designated chest skips the reward");
    set_tool_hooks(ToolHooks{true, false, false, false});
    expect(!consider_chest(7).fight, "sonar makes that chest safe");
    set_tool_hooks({});
    arm_mimic_at(9, 8, 7);
    expect(mimic_pending() && mimic_pending_x() == 9, "the fight waits for a live enemy");
    note_mimic_placed(2);
    expect(!mimic_pending() && mimic_slot() == 2, "the first live enemy is pulled to the chest");
    note_mimic_alive(true);
    expect(mimic_slot() == 2, "the mimic stays up while that slot is live");
    note_mimic_alive(false);
    expect(mimic_slot() < 0, "a cleared slot ends the mimic");

    reset();
    const uint32_t live = 0x80000000u | (0x80000000u >> 2);
    expect(enemy_live(live, 0) && enemy_live(live, 2) && !enemy_live(live, 1), "the live mask is bit 31-slot");
    expect(first_live_enemy(live) == 0, "the first live slot is the mimic");
    const RemixPoint points[] = {{10, 20, 30}, {40, 50, 60}};
    EnemyMove moves[4];
    expect(plan_enemy_moves(live, points, 2, moves, 4) == 2, "live enemies take remix points in order");
    expect(moves[0].slot == 0 && moves[0].x == 10 && moves[1].slot == 2 && moves[1].z == 60, "slots keep the point they were given");
    int slot_hp[18] = {};
    slot_hp[0] = 0;
    slot_hp[2] = 160;
    const uint32_t fought = mirror_candidates(live, slot_hp, 18);
    expect(fought == (0x80000000u >> 2), "a 0-HP live slot is not a mirror target");
    on_world(0x1A, 4, 0);
    expect(mirror_point_count() == 4, "the gate has four remix positions");
    expect(take_mirror_plan(fought, moves, 4) == 1, "only the current room's spot is applied");
    expect(moves[0].slot == 2 && moves[0].x == static_cast<int>(0xF9800000u), "room 4 stands 640 units off the arrival X");
    expect(moves[0].y == static_cast<int>(0xFF3F0000u), "room 4 Y is the warp frame");
    expect(take_mirror_plan(fought, moves, 4) == 1, "the spot is held while the room is current");
    on_world(0x1B, 0, 0);
    expect(take_mirror_plan(fought, moves, 4) == 0, "leaving the gate clears a pending remix");

    reset();
    note_deep_gate_floor(10);
    expect(buff_existing_boss(100, 10) == 200, "a tenth floor adds the custodian's health");
    expect(buff_existing_boss(100, 19) == 100, "other floors leave the boss alone");
    BossRoom rooms[] = {{5, 2}};
    expect(custodian_room(5, 2, rooms, 1), "a listed room is the custodian fight");
    expect(!custodian_room(5, 3, rooms, 1), "a different entrance is not");
    expect(custodian_room_count() == 1, "the custodian uses one room");
    on_world(0x17, 0, 0);
    expect(custodian_buff_due(4, 100), "room 0 of area 0x17 is the custodian");
    int stand_x = 0;
    int stand_y = 0;
    int stand_z = 0;
    int stand_hp = 0;
    custodian_spot(&stand_x, &stand_y, &stand_z, &stand_hp);
    expect(stand_x == static_cast<int>(0x02A10000u) && stand_y == static_cast<int>(0xFFFF0000u) && stand_z == 0, "the custodian stands clear of the arrival point");
    expect(stand_hp > 0, "the spawned custodian has health");
    on_world(0x17, 2, 0);
    expect(!custodian_buff_due(4, 100), "room 2 is the void and is not used");
    on_world(0x17, 3, 0);
    expect(!custodian_buff_due(4, 100), "room 3 is the airship boss and is not used");

    reset();
    const int shore_x = static_cast<int>(0x24000000u);
    const int shore_y = static_cast<int>(0xFFFF0000u);
    const int shore_z = static_cast<int>(0xF4000000u);
    expect(player_at_shore(0x0B, 3, shore_x, shore_y, shore_z), "the grass bank is the fishing spot");
    expect(!player_at_shore(0x0B, 2, shore_x, shore_y, shore_z), "the cliff room is not the shore");
    expect(!player_at_shore(0x1D, 0, shore_x, shore_y, shore_z), "the sunken sub-gate is not the lake");
    const ShorePoint shore{0x12, 100, 0, 0, 50};
    expect(at_shore(0x12, -1, 100, 0, 0, &shore, 1), "a filled shoreline accepts the player");
    expect(!at_shore(0x12, -1, 1000, 0, 0, &shore, 1), "a far point is not the shore");
    CastUpdate cast = update_cast(true, kJumpButton, kJumpButton, 0, 1.0);
    expect(!cast.started, "jump does not cast");
    cast = update_cast(true, kFireButton, kFireButton, 0, 1.0);
    expect(!cast.started, "fire is not the action button");
    cast = update_cast(false, kActionButton, kActionButton, 0, 1.0);
    expect(!cast.started, "C-down away from shore does not cast");
    cast = update_cast(true, kActionButton, kActionButton, 0, 0.5);
    expect(cast.started && !cast.finished, "C-down starts a cast");
    cast = update_cast(true, 0, kActionButton, 0, 0.5);
    expect(!cast.finished, "holding the line keeps the cast");
    cast = update_cast(true, 0, 0, kActionButton, 0.0);
    expect(cast.finished && std::string(cast.caught.name).find("snapped") == std::string::npos, "releasing the line lands a fish");

    if (g_failed != 0) {
        std::cerr << g_failed << " failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "addons ok\n";
    return EXIT_SUCCESS;
}
