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
    bool used_early_range = false;
    for (int id : flags) {
        if (id >= kEarlyAddonFlagLo && id <= kEarlyAddonFlagHi) used_early_range = true;
        expect(id >= kFlagLo && id <= kFlagHi, "new flags stay in the add-on B range");
    }
    expect(!used_early_range, "flags reserved for add-ons 1-23 are not written");

    const std::string saved = serialize();
    reset();
    expect(zenny() == 0, "reset clears zenny");
    expect(deserialize(saved), "save text loads");
    expect(checklist().find("#29 done") != std::string::npos, "sparring is marked done");
    expect(checklist().find("#28 partial") != std::string::npos, "the mimic stays partial");

    if (g_failed != 0) {
        std::cerr << g_failed << " failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "addons ok\n";
    return EXIT_SUCCESS;
}
