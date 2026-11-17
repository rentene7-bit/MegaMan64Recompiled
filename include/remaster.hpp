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
bool voice_active();
void stop_ps1_audio();
void set_output_gain(float gain);
void mix_audio(float* interleaved_stereo, size_t frames, uint32_t sample_rate);

void tick_autosave();

std::vector<TexturePackCandidate> select_texture_packs(std::vector<TexturePackCandidate> packs, uint64_t budget_bytes);

} // namespace remaster

#endif
