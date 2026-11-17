#ifndef REMASTER_HPP
#define REMASTER_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/* This branch is the remaster. Working presentation changes are always on.
 * Draw distance, PS1 music, and cutscene skip stay experimental because they
 * are incomplete or can cost a weak GPU. Nothing here is specific to one
 * machine: window size, aspect, and texture budget follow the display and the
 * settings, on Windows, Linux, and macOS. */

namespace remaster {

enum class DrawDistance {
    Off,
    Low,
    Medium
};

enum class TextureBudget {
    Mb256,
    Mb512,
    Mb1024,
    Unlimited
};

struct DiscReport {
    bool loaded = false;
    bool has_raw_audio = false;
    std::string path;
    std::string error;
    std::string format;
    std::string serial;
    std::string volume_id;
    std::string region;
    std::string language;
    int xa_streams = 0;
    int cdda_tracks = 0;
    int seq_files = 0;
    int seq_notes = 0;
    int unused_audio_hits = 0;
    std::string summary;
};

struct WindowFit {
    int width = 0;
    int height = 0;
    bool clamped = false;
};

struct TexturePackCandidate {
    std::filesystem::path path;
    uint64_t bytes = 0;
};

void set_app_folder(const std::filesystem::path& folder);
std::filesystem::path app_folder();

void set_disc_path(const std::filesystem::path& path);
std::filesystem::path disc_path();
const DiscReport& disc_report();

void set_draw_distance(DrawDistance level);
DrawDistance draw_distance();
const char* draw_distance_name(DrawDistance level);
bool set_draw_distance_name(const std::string& name);

void set_texture_budget(TextureBudget budget);
TextureBudget texture_budget();
const char* texture_budget_name(TextureBudget budget);
bool set_texture_budget_name(const std::string& name);
uint64_t texture_budget_bytes(TextureBudget budget);
/* Bumped when the budget or the pack folder changes, so the renderer reloads once. */
uint64_t texture_pack_generation();
std::vector<std::filesystem::path> active_texture_pack_paths();

void set_ps1_music_enabled(bool enabled);
bool ps1_music_enabled();
void set_cutscene_skip_enabled(bool enabled);
bool cutscene_skip_enabled();

void request_cutscene_skip();
bool consume_cutscene_skip();
void set_cutscene_active(bool active);
bool cutscene_active();

uint32_t patch_flags();
int fade_steps();
int bilerp_mode();
void adjust_fog(int16_t& start, int16_t& end);
int widescreen_scale_q8(float window_aspect);
/* scale_q8 is 256 for no change. Values below 256 widen the horizontal FOV.
 * Returns false when the matrix is not a simple perspective and is left alone. */
bool scale_projection_matrix(void* mtx64, int scale_q8);

WindowFit fit_window_to_display(int desired_w, int desired_h, int display_w, int display_h);

bool play_voice(int id);
bool play_cdda_track(int track_index);
bool play_cdda_from(int disc_index, int track_index);
bool voice_active();
void stop_ps1_audio();
void set_output_gain(float gain);
void mix_audio(float* interleaved_stereo, size_t frames, uint32_t sample_rate);

void tick_autosave();
void tick_frame(double dt_seconds);

std::vector<TexturePackCandidate> select_texture_packs(std::vector<TexturePackCandidate> packs, uint64_t budget_bytes);

/* Player choices inside the remaster. They are not separate remaster toggles. */
enum class Difficulty { Easy, Normal, Hard };
enum class AimMode { Stick, Gyro, Both };
enum class Colorblind { Off, Deuteranopia, Protanopia, Tritanopia };
enum class TextSpeed { Normal, Fast, Instant };
enum class EffectLevel { Off, Low, Medium };
enum class UpscaleQuality { Off, X2, X4 };

void set_difficulty(Difficulty value);
Difficulty difficulty();
const char* difficulty_name(Difficulty value);
bool set_difficulty_name(const std::string& name);
int damage_taken_q8();
int damage_dealt_q8();
/* Scales one hit. known_victim is false until the player object has been seen,
 * so damage is left alone during boot. A positive hit never scales down to zero. */
int scale_damage_amount(bool known_victim, bool victim_is_player, int amount);

void set_new_game_plus(bool enabled);
bool new_game_plus();

void set_game_speed_percent(int percent);
int game_speed_percent();
int game_speed_q8();

void set_aim_mode(AimMode mode);
AimMode aim_mode();
const char* aim_mode_name(AimMode mode);
bool set_aim_mode_name(const std::string& name);
bool aim_uses_gyro();
void set_aim_assist(int percent);
int aim_assist();
float soften_aim(float value);
void set_one_handed(bool enabled);
bool one_handed();

void set_colorblind(Colorblind mode);
Colorblind colorblind();
const char* colorblind_name(Colorblind mode);
bool set_colorblind_name(const std::string& name);
const char* door_mark(int color_index);

void set_motion_comfort(bool enabled);
bool motion_comfort();
void set_narration(bool enabled);
bool narration();
std::string narration_command(const std::string& text);

void set_text_speed(TextSpeed speed);
TextSpeed text_speed();
const char* text_speed_name(TextSpeed speed);
bool set_text_speed_name(const std::string& name);
/* Glyphs are the bytes pc1 measured ('a' = 0x30, space = 0x4F). Bytes outside
 * 0x20..0x7E are left for the next tick so a control code is not consumed. */
bool dialogue_glyph(uint8_t value);
int dialogue_extra_advance(const uint8_t* upcoming, int count);

void set_dynamic_lights(EffectLevel level);
EffectLevel dynamic_lights();
void set_shadows(EffectLevel level);
EffectLevel shadows();
void set_outline(bool enabled);
bool outline();
void set_crt(EffectLevel level);
EffectLevel crt();
void set_seamless(bool enabled);
bool seamless();
int seamless_budget_mb();
const char* effect_level_name(EffectLevel level);
bool set_effect_level_name(EffectLevel& destination, const std::string& name);

void set_upscale_quality(UpscaleQuality quality);
UpscaleQuality upscale_quality();
const char* upscale_quality_name(UpscaleQuality quality);
bool set_upscale_quality_name(const std::string& name);
void note_rom_path(const std::filesystem::path& path);
std::filesystem::path rom_path();
int upscale_step();

void set_secondary_disc_path(const std::filesystem::path& path);
std::filesystem::path secondary_disc_path();
void set_bonus_disc_path(const std::filesystem::path& path);
std::filesystem::path bonus_disc_path();
bool dual_audio_active();
std::string audio_language_summary();
int voice_disc_index();

bool lock_on_move_enabled();
void cycle_weapon(int delta);
int weapon_slot();
bool weapon_wheel_open();
void set_weapon_wheel_open(bool open);

struct TrackerNote {
    int chests = 0;
    int chest_goal = 0;
    int door_color = 0;
    int items_left = 0;
};
TrackerNote tracker();
void set_tracker(const TrackerNote& note);

struct Loadout {
    std::string name;
    int parts[8] = {};
};
bool save_loadout(const Loadout& loadout);
std::vector<Loadout> loadouts();

void mark_gate(const std::string& name);
std::vector<std::string> visited_gates();

void set_quest_done(const std::string& id, bool done, bool missable);
int quests_done();
int missables_open();

struct StatBlock { int attack = 0; int energy = 0; int range = 0; };
std::string compare_stats(const StatBlock& current, const StatBlock& next);

void set_randomizer_seed(const std::string& seed);
std::string randomizer_seed();
std::vector<int> randomizer_order(int count);
std::string randomizer_share_code();

void start_speedrun();
void split_speedrun(const std::string& name);
void reset_speedrun();
void set_loading(bool loading);
bool speedrun_active();
std::string speedrun_clock();
std::string livesplit_xml();

void record_ghost_sample(uint16_t buttons, int8_t x, int8_t y);
int ghost_sample_count();
void clear_ghost();

std::string input_display(uint16_t buttons);
std::string build_stamp();

void set_enemy_hp(int id, int hp, int max_hp);
void note_actor_hp(int id, int hp);
int actor_hp(int id);
int actor_max_hp(int id);
int enemy_bar_count();

void unlock_achievement(const std::string& id);
int achievements_unlocked();
void note_bestiary(int id);
int bestiary_seen();

void set_challenge_mask(uint32_t mask);
uint32_t challenge_mask();
int challenge_rank(int seconds);

void enter_photo_mode();
void exit_photo_mode();
bool photo_mode();

int music_layers(int intensity);
int jukebox_track_count(int disc);
bool play_jukebox(int disc, int track);
int unused_audio_count();
void set_boss_rush(int index);
int boss_rush_index();
std::string version_audit();
bool snapshot_retry();

std::string ideas_status_line();

} // namespace remaster

#endif
