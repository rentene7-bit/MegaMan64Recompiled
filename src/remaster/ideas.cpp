#include "disc_internal.hpp"
#include "remaster.hpp"
#include "upscale.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>

namespace remaster {
namespace {

std::atomic<int> g_difficulty{1};
std::atomic<bool> g_ng_plus{false};
std::atomic<int> g_speed{100};
std::atomic<int> g_aim{0};
std::atomic<int> g_aim_assist{0};
std::atomic<bool> g_one_handed{false};
std::atomic<int> g_colorblind{0};
std::atomic<bool> g_comfort{false};
std::atomic<bool> g_narration{false};
std::atomic<int> g_text{1};
std::atomic<int> g_lights{0};
std::atomic<int> g_shadows{0};
std::atomic<bool> g_outline{false};
std::atomic<int> g_crt{0};
std::atomic<bool> g_seamless{false};
std::atomic<int> g_upscale{1};
std::atomic<int> g_weapon{0};
std::atomic<int> g_weapon_write{-1};
std::atomic<bool> g_wheel{false};
std::atomic<bool> g_photo{false};
std::atomic<bool> g_loading{false};
std::atomic<uint32_t> g_challenges{0};
std::atomic<int> g_boss{-1};

std::string g_seed;
std::string g_rom;
std::filesystem::path g_secondary;
std::filesystem::path g_bonus;
TrackerNote g_tracker;
std::vector<Loadout> g_loadouts;
std::vector<std::string> g_gates;
std::vector<std::pair<std::string, bool>> g_quests;
std::vector<std::string> g_missables_open;
std::vector<std::string> g_achievements;
std::vector<int> g_bestiary;
std::mutex g_actor_mu;
struct ActorHp {
    int id = 0;
    int hp = 0;
    int max_hp = 1;
};
std::vector<ActorHp> g_actors;
std::vector<std::string> g_splits;
std::vector<double> g_split_times;
double g_run_seconds = 0;
bool g_run = false;
struct GhostSample { uint16_t buttons; int8_t x; int8_t y; };
std::vector<GhostSample> g_ghost;
int g_upscale_cursor = 0;
bool g_upscale_done = false;

std::string sanitize_speech(const std::string& text) {
    std::string clean;
    clean.reserve(text.size());
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ',') {
            clean.push_back(static_cast<char>(c));
        }
    }
    if (clean.size() > 180) {
        clean.resize(180);
    }
    return clean;
}

std::string clock_string(double seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    const int whole = static_cast<int>(seconds);
    const int ms = static_cast<int>((seconds - whole) * 1000.0);
    const int h = whole / 3600;
    const int m = (whole / 60) % 60;
    const int s = whole % 60;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", h, m, s, ms);
    return buffer;
}

uint64_t mix_seed(const std::string& seed) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : seed) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

} // namespace

void set_difficulty(Difficulty value) { g_difficulty.store(static_cast<int>(value)); }
Difficulty difficulty() { return static_cast<Difficulty>(g_difficulty.load()); }
const char* difficulty_name(Difficulty value) {
    switch (value) {
    case Difficulty::Easy: return "Easy";
    case Difficulty::Hard: return "Hard";
    case Difficulty::Normal:
    default: return "Normal";
    }
}
bool set_difficulty_name(const std::string& name) {
    if (name == "Easy") { set_difficulty(Difficulty::Easy); return true; }
    if (name == "Hard") { set_difficulty(Difficulty::Hard); return true; }
    if (name == "Normal") { set_difficulty(Difficulty::Normal); return true; }
    return false;
}
int damage_taken_q8() {
    int taken = 256;
    if (difficulty() == Difficulty::Easy) taken = 192;
    if (difficulty() == Difficulty::Hard) taken = 320;
    if (new_game_plus()) taken -= 32;
    return std::clamp(taken, 64, 512);
}
int damage_dealt_q8() {
    int dealt = 256;
    if (difficulty() == Difficulty::Easy) dealt = 320;
    if (difficulty() == Difficulty::Hard) dealt = 192;
    if (new_game_plus()) dealt += 64;
    return std::clamp(dealt, 64, 512);
}

int scale_damage_amount(bool known_victim, bool victim_is_player, int amount) {
    if (!known_victim || amount == 0) {
        return amount;
    }
    if (amount > 10000 || amount < -10000) {
        return amount;
    }
    const int scale = victim_is_player ? damage_taken_q8() : damage_dealt_q8();
    int scaled = static_cast<int>((static_cast<long long>(amount) * scale) >> 8);
    if (amount > 0 && scaled < 1) {
        scaled = 1;
    }
    if (amount < 0 && scaled > -1) {
        scaled = -1;
    }
    return std::clamp(scaled, -32768, 32767);
}

void set_new_game_plus(bool enabled) { g_ng_plus.store(enabled); }
bool new_game_plus() { return g_ng_plus.load(); }

void set_game_speed_percent(int percent) { g_speed.store(std::clamp(percent, 50, 150)); }
int game_speed_percent() { return g_speed.load(); }
int game_speed_q8() { return game_speed_percent() * 256 / 100; }

void set_aim_mode(AimMode mode) { g_aim.store(static_cast<int>(mode)); }
AimMode aim_mode() { return static_cast<AimMode>(g_aim.load()); }
const char* aim_mode_name(AimMode mode) {
    switch (mode) {
    case AimMode::Gyro: return "Gyro";
    case AimMode::Both: return "Both";
    case AimMode::Stick:
    default: return "Stick";
    }
}
bool set_aim_mode_name(const std::string& name) {
    if (name == "Gyro") { set_aim_mode(AimMode::Gyro); return true; }
    if (name == "Both") { set_aim_mode(AimMode::Both); return true; }
    if (name == "Stick") { set_aim_mode(AimMode::Stick); return true; }
    return false;
}
bool aim_uses_gyro() { return aim_mode() != AimMode::Stick; }
void set_aim_assist(int percent) { g_aim_assist.store(std::clamp(percent, 0, 100)); }
int aim_assist() { return g_aim_assist.load(); }
float soften_aim(float value) {
    const float assist = aim_assist() / 100.0f;
    if (assist <= 0.0f) {
        return value;
    }
    const float magnitude = std::fabs(value);
    if (magnitude < 0.35f) {
        return value;
    }
    return value * (1.0f - 0.45f * assist);
}
void set_one_handed(bool enabled) { g_one_handed.store(enabled); }
bool one_handed() { return g_one_handed.load(); }

void set_colorblind(Colorblind mode) { g_colorblind.store(static_cast<int>(mode)); }
Colorblind colorblind() { return static_cast<Colorblind>(g_colorblind.load()); }
const char* colorblind_name(Colorblind mode) {
    switch (mode) {
    case Colorblind::Deuteranopia: return "Deuteranopia";
    case Colorblind::Protanopia: return "Protanopia";
    case Colorblind::Tritanopia: return "Tritanopia";
    case Colorblind::Off:
    default: return "Off";
    }
}
bool set_colorblind_name(const std::string& name) {
    if (name == "Deuteranopia") { set_colorblind(Colorblind::Deuteranopia); return true; }
    if (name == "Protanopia") { set_colorblind(Colorblind::Protanopia); return true; }
    if (name == "Tritanopia") { set_colorblind(Colorblind::Tritanopia); return true; }
    if (name == "Off") { set_colorblind(Colorblind::Off); return true; }
    return false;
}
const char* door_mark(int color_index) {
    static const char* colors[] = {"red", "blue", "yellow", "green"};
    static const char* shapes[] = {"circle", "square", "triangle", "diamond"};
    const int index = std::clamp(color_index, 0, 3);
    if (colorblind() == Colorblind::Off) {
        return colors[index];
    }
    static std::string marked[4];
    marked[index] = std::string(shapes[index]) + " " + colors[index];
    return marked[index].c_str();
}

void set_motion_comfort(bool enabled) { g_comfort.store(enabled); }
bool motion_comfort() { return g_comfort.load(); }
void set_narration(bool enabled) { g_narration.store(enabled); }
bool narration() { return g_narration.load(); }
std::string narration_command(const std::string& text) {
    if (!narration()) {
        return {};
    }
    const std::string clean = sanitize_speech(text);
    if (clean.empty()) {
        return {};
    }
    return clean;
}

void set_text_speed(TextSpeed speed) { g_text.store(static_cast<int>(speed)); }
TextSpeed text_speed() { return static_cast<TextSpeed>(g_text.load()); }
const char* text_speed_name(TextSpeed speed) {
    switch (speed) {
    case TextSpeed::Normal: return "Normal";
    case TextSpeed::Instant: return "Instant";
    case TextSpeed::Fast:
    default: return "Fast";
    }
}
bool dialogue_glyph(uint8_t value) {
    return value >= 0x20 && value <= 0x7E;
}

int dialogue_extra_advance(const uint8_t* upcoming, int count) {
    int extra = 0;
    const int cap = std::min(count, 2);
    while (extra < cap && dialogue_glyph(upcoming[extra])) {
        extra++;
    }
    return extra;
}

bool set_text_speed_name(const std::string& name) {
    if (name == "Normal") { set_text_speed(TextSpeed::Normal); return true; }
    if (name == "Instant") { set_text_speed(TextSpeed::Instant); return true; }
    if (name == "Fast") { set_text_speed(TextSpeed::Fast); return true; }
    return false;
}

const char* effect_level_name(EffectLevel level) {
    switch (level) {
    case EffectLevel::Low: return "Low";
    case EffectLevel::Medium: return "Medium";
    case EffectLevel::Off:
    default: return "Off";
    }
}
bool set_effect_level_name(EffectLevel& destination, const std::string& name) {
    if (name == "Low") { destination = EffectLevel::Low; return true; }
    if (name == "Medium") { destination = EffectLevel::Medium; return true; }
    if (name == "Off") { destination = EffectLevel::Off; return true; }
    return false;
}
void set_dynamic_lights(EffectLevel level) { g_lights.store(static_cast<int>(level)); }
EffectLevel dynamic_lights() { return static_cast<EffectLevel>(g_lights.load()); }
void set_shadows(EffectLevel level) { g_shadows.store(static_cast<int>(level)); }
EffectLevel shadows() { return static_cast<EffectLevel>(g_shadows.load()); }
void set_outline(bool enabled) { g_outline.store(enabled); }
bool outline() { return g_outline.load(); }
void set_crt(EffectLevel level) { g_crt.store(static_cast<int>(level)); }
EffectLevel crt() { return static_cast<EffectLevel>(g_crt.load()); }
void set_seamless(bool enabled) { g_seamless.store(enabled); }
bool seamless() { return g_seamless.load(); }
int seamless_budget_mb() {
    if (!seamless()) {
        return 0;
    }
    const uint64_t bytes = texture_budget_bytes(texture_budget());
    if (bytes == UINT64_MAX) {
        return 256;
    }
    return static_cast<int>(std::min<uint64_t>(256, bytes / (1024ull * 1024ull) / 4));
}

void set_upscale_quality(UpscaleQuality quality) {
    g_upscale.store(static_cast<int>(quality));
    g_upscale_done = false;
    g_upscale_cursor = 0;
}
UpscaleQuality upscale_quality() { return static_cast<UpscaleQuality>(g_upscale.load()); }
const char* upscale_quality_name(UpscaleQuality quality) {
    switch (quality) {
    case UpscaleQuality::Off: return "Off";
    case UpscaleQuality::X4: return "x4";
    case UpscaleQuality::X2:
    default: return "x2";
    }
}
bool set_upscale_quality_name(const std::string& name) {
    if (name == "Off") { set_upscale_quality(UpscaleQuality::Off); return true; }
    if (name == "x4") { set_upscale_quality(UpscaleQuality::X4); return true; }
    if (name == "x2") { set_upscale_quality(UpscaleQuality::X2); return true; }
    return false;
}
void note_rom_path(const std::filesystem::path& path) { g_rom = path.string(); g_upscale_done = false; }
std::filesystem::path rom_path() { return g_rom; }

int upscale_step() {
    if (g_upscale_done || upscale_quality() == UpscaleQuality::Off || g_rom.empty() || app_folder().empty()) {
        return 0;
    }
    std::ifstream in(g_rom, std::ios::binary);
    if (!in) {
        g_upscale_done = true;
        return 0;
    }
    in.seekg(0, std::ios::end);
    const auto length = static_cast<size_t>(std::max<std::streamoff>(0, in.tellg()));
    if (length < 64 * 1024) {
        g_upscale_done = true;
        return 0;
    }
    const int scale = upscale_quality() == UpscaleQuality::X4 ? 4 : 2;
    const int cap = upscale::scan_cap_for_budget(texture_budget_bytes(texture_budget()), scale);
    if (g_upscale_cursor >= cap) {
        g_upscale_done = true;
        return 0;
    }
    const size_t block = 32u * 32u * 2u;
    const size_t offset = static_cast<size_t>(g_upscale_cursor) * 4096u;
    g_upscale_cursor++;
    if (offset + block > length) {
        g_upscale_done = true;
        return 0;
    }
    std::vector<uint8_t> bytes(block);
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (in.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return 0;
    }
    upscale::Image decoded = upscale::from_rgba16(bytes.data(), 32, 32);
    if (!upscale::looks_like_texture(decoded)) {
        return 0;
    }
    upscale::Image scaled = scale >= 4 ? upscale::epx4(decoded) : upscale::epx2(decoded);
    const auto png = upscale::write_png(scaled);
    const auto dir = app_folder() / "texture_packs" / "generated";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / ("rom_" + std::to_string(offset) + ".png"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out ? 1 : 0;
}

void set_secondary_disc_path(const std::filesystem::path& path) {
    g_secondary = path;
    disc::load_secondary(path);
}
std::filesystem::path secondary_disc_path() { return g_secondary; }
void set_bonus_disc_path(const std::filesystem::path& path) {
    g_bonus = path;
    disc::load_bonus(path);
}
std::filesystem::path bonus_disc_path() { return g_bonus; }
bool dual_audio_active() {
    const DiscReport primary = disc::copy_report_of(0);
    const DiscReport secondary = disc::copy_report_of(1);
    return primary.loaded && secondary.loaded && !primary.language.empty() && primary.language != secondary.language;
}
int voice_disc_index() {
    if (!dual_audio_active()) {
        return 0;
    }
    const DiscReport secondary = disc::copy_report_of(1);
    return secondary.language == "japanese" ? 1 : 0;
}
std::string audio_language_summary() {
    const DiscReport primary = disc::copy_report_of(0);
    if (!primary.loaded) {
        return "No disc. Voices stay on the N64 soundtrack.";
    }
    if (!dual_audio_active()) {
        return "Voices follow the " + primary.language + " disc.";
    }
    const DiscReport secondary = disc::copy_report_of(1);
    return "Dual audio: " + secondary.language + " voices with the loaded game's text.";
}

bool lock_on_move_enabled() { return true; }

/* pc1: 0 is Remove, 2 is Machine Buster, and 4 and 5 change the HUD and the
 * arm. No other index has been verified, so the swap does not visit them. */
constexpr int kVerifiedWeapons[] = {0, 2, 4, 5};

int next_verified_weapon(int current, int delta) {
    if (delta >= 0) {
        for (int value : kVerifiedWeapons) {
            if (value > current) {
                return value;
            }
        }
        return kVerifiedWeapons[0];
    }
    for (int i = 3; i >= 0; i--) {
        if (kVerifiedWeapons[i] < current) {
            return kVerifiedWeapons[i];
        }
    }
    return kVerifiedWeapons[3];
}

void cycle_weapon(int delta) {
    const int next = next_verified_weapon(g_weapon.load(), delta);
    g_weapon.store(next);
    g_weapon_write.store(next);
}
int weapon_slot() { return g_weapon.load(); }
void note_equipped_weapon(int value) {
    if (value >= 0 && value <= 255) {
        g_weapon.store(value);
    }
}
int take_equipped_weapon_write() {
    return g_weapon_write.exchange(-1);
}
const char* equipped_weapon_name(int value) {
    switch (value) {
    case 0: return "None";
    case 2: return "Machine Buster";
    case 4: return "4";
    case 5: return "5";
    default: return "Unverified";
    }
}
bool weapon_wheel_open() { return g_wheel.load(); }
void set_weapon_wheel_open(bool open) { g_wheel.store(open); }

TrackerNote tracker() { return g_tracker; }
void set_tracker(const TrackerNote& note) {
    g_tracker = note;
    g_tracker.chests = std::clamp(g_tracker.chests, 0, 99);
    g_tracker.chest_goal = std::clamp(g_tracker.chest_goal, 0, 99);
    g_tracker.door_color = std::clamp(g_tracker.door_color, 0, 3);
    g_tracker.items_left = std::clamp(g_tracker.items_left, 0, 99);
}

bool save_loadout(const Loadout& loadout) {
    if (loadout.name.empty()) {
        return false;
    }
    for (Loadout& have : g_loadouts) {
        if (have.name == loadout.name) {
            have = loadout;
            return true;
        }
    }
    if (g_loadouts.size() >= 12) {
        return false;
    }
    g_loadouts.push_back(loadout);
    return true;
}
std::vector<Loadout> loadouts() { return g_loadouts; }

void mark_gate(const std::string& name) {
    if (name.empty()) {
        return;
    }
    if (std::find(g_gates.begin(), g_gates.end(), name) == g_gates.end() && g_gates.size() < 32) {
        g_gates.push_back(name);
    }
}
std::vector<std::string> visited_gates() { return g_gates; }

void set_quest_done(const std::string& id, bool done, bool missable) {
    for (auto& quest : g_quests) {
        if (quest.first == id) {
            quest.second = done;
            return;
        }
    }
    if (g_quests.size() < 64) {
        g_quests.emplace_back(id, done);
    }
    if (missable && !done) {
        if (std::find(g_missables_open.begin(), g_missables_open.end(), id) == g_missables_open.end()) {
            g_missables_open.push_back(id);
        }
    }
}
int quests_done() {
    int count = 0;
    for (const auto& quest : g_quests) {
        if (quest.second) count++;
    }
    return count;
}
int missables_open() { return static_cast<int>(g_missables_open.size()); }

std::string compare_stats(const StatBlock& current, const StatBlock& next) {
    std::ostringstream out;
    out << "attack " << (next.attack - current.attack) << ", energy " << (next.energy - current.energy) << ", range " << (next.range - current.range);
    return out.str();
}

void set_randomizer_seed(const std::string& seed) { g_seed = seed; }
std::string randomizer_seed() { return g_seed; }
std::vector<int> randomizer_order(int count) {
    count = std::clamp(count, 0, 64);
    std::vector<int> order(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) order[static_cast<size_t>(i)] = i;
    uint64_t state = mix_seed(g_seed.empty() ? "off" : g_seed);
    if (g_seed.empty()) {
        return order;
    }
    for (int i = count - 1; i > 0; i--) {
        state = state * 6364136223846793005ull + 1;
        const int j = static_cast<int>(state % static_cast<uint64_t>(i + 1));
        std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(j)]);
    }
    return order;
}
std::string randomizer_share_code() { return g_seed.empty() ? "" : "MM64R-" + g_seed; }

void start_speedrun() { g_run = true; g_run_seconds = 0; g_splits.clear(); g_split_times.clear(); }
void split_speedrun(const std::string& name) {
    if (!g_run) return;
    g_splits.push_back(name.empty() ? "Split" : name);
    g_split_times.push_back(g_run_seconds);
}
void reset_speedrun() { g_run = false; g_run_seconds = 0; g_splits.clear(); g_split_times.clear(); }
void set_loading(bool loading) { g_loading.store(loading); }
bool speedrun_active() { return g_run; }
std::string speedrun_clock() { return clock_string(g_run_seconds); }
std::string livesplit_xml() {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Run version=\"1.7.0\">\n";
    out << "  <GameName>Mega Man 64 Remaster</GameName>\n  <CategoryName>Any%</CategoryName>\n";
    out << "  <Segments>\n";
    for (size_t i = 0; i < g_splits.size(); i++) {
        out << "    <Segment><Name>" << g_splits[i] << "</Name><SplitTimes><SplitTime name=\"Personal Best\"><RealTime>";
        out << clock_string(g_split_times[i]) << "</RealTime></SplitTime></SplitTimes></Segment>\n";
    }
    out << "  </Segments>\n</Run>\n";
    return out.str();
}

void tick_frame(double dt_seconds) {
    if (g_run && !g_loading.load() && !photo_mode()) {
        g_run_seconds += std::clamp(dt_seconds, 0.0, 0.1) * (game_speed_percent() / 100.0);
    }
    if ((g_upscale_cursor % 4) == 0) {
        upscale_step();
    } else {
        g_upscale_cursor++;
    }
}

void record_ghost_sample(uint16_t buttons, int8_t x, int8_t y) {
    if (g_ghost.size() >= 60u * 60u * 10u) {
        return;
    }
    g_ghost.push_back({buttons, x, y});
}
int ghost_sample_count() { return static_cast<int>(g_ghost.size()); }
void clear_ghost() { g_ghost.clear(); }

std::string input_display(uint16_t buttons) {
    std::string text;
    auto add = [&](uint16_t bit, const char* name) {
        if ((buttons & bit) != 0) {
            if (!text.empty()) text += " ";
            text += name;
        }
    };
    add(0x8000, "A");
    add(0x4000, "B");
    add(0x2000, "Z");
    add(0x0010, "R");
    add(0x1000, "Start");
    return text.empty() ? "none" : text;
}
std::string build_stamp() { return "Mega Man 64 Remaster"; }

void set_enemy_hp(int id, int hp, int max_hp) {
    std::lock_guard<std::mutex> lock(g_actor_mu);
    const int capped = std::clamp(hp, 0, std::max(1, max_hp));
    for (ActorHp& actor : g_actors) {
        if (actor.id == id) {
            actor.hp = capped;
            actor.max_hp = std::max(actor.max_hp, std::max(1, max_hp));
            return;
        }
    }
    if (g_actors.size() < 16) {
        g_actors.push_back(ActorHp{id, capped, std::max(1, max_hp)});
    }
}

void note_actor_hp(int id, int hp) {
    if (hp < 0 || hp > 9999) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_actor_mu);
    for (ActorHp& actor : g_actors) {
        if (actor.id == id) {
            actor.hp = hp;
            if (hp > actor.max_hp) {
                actor.max_hp = hp;
            }
            return;
        }
    }
    if (g_actors.size() < 16) {
        g_actors.push_back(ActorHp{id, hp, std::max(hp, 1)});
    }
}

int actor_hp(int id) {
    std::lock_guard<std::mutex> lock(g_actor_mu);
    for (const ActorHp& actor : g_actors) {
        if (actor.id == id) {
            return actor.hp;
        }
    }
    return -1;
}

int actor_max_hp(int id) {
    std::lock_guard<std::mutex> lock(g_actor_mu);
    for (const ActorHp& actor : g_actors) {
        if (actor.id == id) {
            return actor.max_hp;
        }
    }
    return -1;
}

int enemy_bar_count() {
    std::lock_guard<std::mutex> lock(g_actor_mu);
    return static_cast<int>(g_actors.size());
}

void unlock_achievement(const std::string& id) {
    if (id.empty()) return;
    if (std::find(g_achievements.begin(), g_achievements.end(), id) == g_achievements.end() && g_achievements.size() < 64) {
        g_achievements.push_back(id);
    }
}
int achievements_unlocked() { return static_cast<int>(g_achievements.size()); }
void note_bestiary(int id) {
    if (std::find(g_bestiary.begin(), g_bestiary.end(), id) == g_bestiary.end() && g_bestiary.size() < 128) {
        g_bestiary.push_back(id);
    }
}
int bestiary_seen() { return static_cast<int>(g_bestiary.size()); }

void set_challenge_mask(uint32_t mask) { g_challenges.store(mask); }
uint32_t challenge_mask() { return g_challenges.load(); }
int challenge_rank(int seconds) {
    int bits = 0;
    uint32_t mask = challenge_mask();
    while (mask != 0) { bits += mask & 1u; mask >>= 1u; }
    int rank = bits * 10;
    if (seconds > 0 && seconds < 3600) rank += (3600 - seconds) / 120;
    return rank;
}

void enter_photo_mode() { g_photo.store(true); }
void exit_photo_mode() { g_photo.store(false); }
bool photo_mode() { return g_photo.load(); }

int music_layers(int intensity) {
    if (!ps1_music_enabled()) return 1;
    const int notes = disc::copy_report().seq_notes;
    if (notes <= 0) return 1;
    if (intensity >= 70) return 3;
    if (intensity >= 30) return 2;
    return 1;
}
int jukebox_track_count(int disc_index) {
    return static_cast<int>(disc::copy_cdda(disc_index).size());
}
bool play_jukebox(int disc_index, int track) {
    const auto tracks = disc::copy_cdda(disc_index);
    if (track < 0 || track >= static_cast<int>(tracks.size())) return false;
    return play_cdda_from(disc_index, track);
}
int unused_audio_count() { return disc::copy_report().unused_audio_hits + disc::copy_report_of(2).unused_audio_hits; }
void set_boss_rush(int index) { g_boss.store(std::clamp(index, -1, 31)); }
int boss_rush_index() { return g_boss.load(); }
std::string version_audit() {
    const DiscReport primary = disc::copy_report();
    const DiscReport secondary = disc::copy_report_of(1);
    if (!primary.loaded) return "Load a disc to compare regions. The N64 text stays whatever ROM you launched.";
    std::string text = "Primary disc " + primary.language + " (" + primary.region + ").";
    if (secondary.loaded) text += " Second disc " + secondary.language + ".";
    if (dual_audio_active()) text += " Dual audio is available.";
    return text;
}
bool snapshot_retry() {
    if (app_folder().empty()) return false;
    const auto from = app_folder() / "autosave" / "slot0";
    const auto to = app_folder() / "retry";
    std::error_code ec;
    if (!std::filesystem::is_directory(from, ec)) return false;
    std::filesystem::create_directories(to, ec);
    bool copied = false;
    for (std::filesystem::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        std::filesystem::copy_file(it->path(), to / it->path().filename(), std::filesystem::copy_options::overwrite_existing, ec);
        copied = copied || !ec;
    }
    return copied;
}

std::string ideas_status_line() {
    return audio_language_summary();
}

} // namespace remaster
