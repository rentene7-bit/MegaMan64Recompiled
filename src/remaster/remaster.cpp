#include "disc_internal.hpp"
#include "remaster.hpp"
#include "remaster_flags.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace remaster {
namespace {

std::filesystem::path g_app_folder;
std::atomic<int> g_draw_distance{static_cast<int>(DrawDistance::Off)};
std::atomic<int> g_texture_budget{static_cast<int>(TextureBudget::Mb512)};
std::atomic<bool> g_ps1_music{false};
std::atomic<bool> g_cutscene_skip{false};
std::atomic<bool> g_skip_request{false};
std::atomic<bool> g_cutscene_active{false};
std::atomic<uint64_t> g_pack_generation{1};
std::chrono::steady_clock::time_point g_last_autosave{};

uint64_t directory_bytes(const std::filesystem::path& path, int depth, int& files_left) {
    if (depth > 4 || files_left <= 0) {
        return 0;
    }
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        files_left--;
        return std::filesystem::file_size(path, ec);
    }
    uint64_t total = 0;
    for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end && files_left > 0; it.increment(ec)) {
        total += directory_bytes(it->path(), depth + 1, files_left);
        if (total > (4ull << 30)) {
            break;
        }
    }
    return total;
}

bool is_save_name(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext == ".sav" || ext == ".srm" || ext == ".sram" || ext == ".eep" || ext == ".fla" || ext == ".flash" || ext == ".save";
}

bool copy_file_limited(const std::filesystem::path& from, const std::filesystem::path& to, uint64_t& budget) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(from, ec);
    if (ec || size > budget || size > (8ull << 20)) {
        return false;
    }
    std::filesystem::create_directories(to.parent_path(), ec);
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out) {
        return false;
    }
    char buffer[16 * 1024];
    uint64_t copied = 0;
    while (in && copied < size) {
        in.read(buffer, sizeof(buffer));
        const auto got = in.gcount();
        if (got > 0) {
            out.write(buffer, got);
            copied += static_cast<uint64_t>(got);
        }
    }
    if (!out) {
        return false;
    }
    budget -= copied;
    return true;
}

} // namespace

void set_app_folder(const std::filesystem::path& folder) {
    g_app_folder = folder;
}

std::filesystem::path app_folder() {
    return g_app_folder;
}

void set_disc_path(const std::filesystem::path& path) {
    stop_ps1_audio();
    disc::load(path);
}

std::filesystem::path disc_path() {
    return disc::copy_report().path;
}

const DiscReport& disc_report() {
    /* The report is copied into a function-local static so callers can hold a
     * reference until the next query. The audio thread does not use it. */
    static DiscReport report;
    report = disc::copy_report();
    return report;
}

void set_draw_distance(DrawDistance level) {
    g_draw_distance.store(static_cast<int>(level));
}

DrawDistance draw_distance() {
    return static_cast<DrawDistance>(g_draw_distance.load());
}

const char* draw_distance_name(DrawDistance level) {
    switch (level) {
    case DrawDistance::Low: return "Low";
    case DrawDistance::Medium: return "Medium";
    case DrawDistance::Off:
    default: return "Off";
    }
}

bool set_draw_distance_name(const std::string& name) {
    if (name == "Low") {
        set_draw_distance(DrawDistance::Low);
        return true;
    }
    if (name == "Medium") {
        set_draw_distance(DrawDistance::Medium);
        return true;
    }
    if (name == "Off") {
        set_draw_distance(DrawDistance::Off);
        return true;
    }
    return false;
}

void set_texture_budget(TextureBudget budget) {
    g_texture_budget.store(static_cast<int>(budget));
    g_pack_generation.fetch_add(1);
}

TextureBudget texture_budget() {
    return static_cast<TextureBudget>(g_texture_budget.load());
}

const char* texture_budget_name(TextureBudget budget) {
    switch (budget) {
    case TextureBudget::Mb256: return "256 MB";
    case TextureBudget::Mb1024: return "1 GB";
    case TextureBudget::Unlimited: return "Unlimited";
    case TextureBudget::Mb512:
    default: return "512 MB";
    }
}

bool set_texture_budget_name(const std::string& name) {
    if (name == "256 MB") {
        set_texture_budget(TextureBudget::Mb256);
        return true;
    }
    if (name == "512 MB") {
        set_texture_budget(TextureBudget::Mb512);
        return true;
    }
    if (name == "1 GB") {
        set_texture_budget(TextureBudget::Mb1024);
        return true;
    }
    if (name == "Unlimited") {
        set_texture_budget(TextureBudget::Unlimited);
        return true;
    }
    return false;
}

uint64_t texture_budget_bytes(TextureBudget budget) {
    switch (budget) {
    case TextureBudget::Mb256: return 256ull << 20;
    case TextureBudget::Mb1024: return 1024ull << 20;
    case TextureBudget::Unlimited: return UINT64_MAX;
    case TextureBudget::Mb512:
    default: return 512ull << 20;
    }
}

uint64_t texture_pack_generation() {
    return g_pack_generation.load();
}

std::vector<TexturePackCandidate> select_texture_packs(std::vector<TexturePackCandidate> packs, uint64_t budget_bytes) {
    std::sort(packs.begin(), packs.end(), [](const TexturePackCandidate& a, const TexturePackCandidate& b) {
        if (a.bytes != b.bytes) {
            return a.bytes < b.bytes;
        }
        return a.path.generic_string() < b.path.generic_string();
    });
    std::vector<TexturePackCandidate> chosen;
    uint64_t used = 0;
    for (const TexturePackCandidate& pack : packs) {
        if (pack.bytes == 0) {
            continue;
        }
        if (budget_bytes != UINT64_MAX && (pack.bytes > budget_bytes || used > budget_bytes - pack.bytes)) {
            continue;
        }
        chosen.push_back(pack);
        if (budget_bytes != UINT64_MAX) {
            used += pack.bytes;
        }
    }
    return chosen;
}

std::vector<std::filesystem::path> active_texture_pack_paths() {
    std::vector<TexturePackCandidate> found;
    const auto root = g_app_folder / "texture_packs";
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        return {};
    }
    int files_left = 4000;
    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const auto path = it->path();
        std::string ext = path.extension().string();
        for (char& ch : ext) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        const bool pack = std::filesystem::is_directory(path, ec) || ext == ".rtz" || ext == ".zip";
        if (!pack) {
            continue;
        }
        TexturePackCandidate candidate;
        candidate.path = path;
        int remaining = files_left;
        candidate.bytes = directory_bytes(path, 0, remaining);
        files_left = remaining;
        found.push_back(candidate);
        if (files_left <= 0) {
            break;
        }
    }
    const auto chosen = select_texture_packs(found, texture_budget_bytes(texture_budget()));
    std::vector<std::filesystem::path> paths;
    paths.reserve(chosen.size());
    for (const TexturePackCandidate& pack : chosen) {
        paths.push_back(pack.path);
    }
    return paths;
}

void set_ps1_music_enabled(bool enabled) {
    g_ps1_music.store(enabled);
    if (!enabled) {
        stop_ps1_audio();
    }
}

bool ps1_music_enabled() {
    return g_ps1_music.load();
}

void set_cutscene_skip_enabled(bool enabled) {
    g_cutscene_skip.store(enabled);
    if (!enabled) {
        g_skip_request.store(false);
    }
}

bool cutscene_skip_enabled() {
    return g_cutscene_skip.load();
}

void request_cutscene_skip() {
    if (g_cutscene_skip.load()) {
        g_skip_request.store(true);
    }
}

bool consume_cutscene_skip() {
    if (!g_cutscene_skip.load()) {
        g_skip_request.store(false);
        return false;
    }
    return g_skip_request.exchange(false);
}

void set_cutscene_active(bool active) {
    g_cutscene_active.store(active);
}

bool cutscene_active() {
    return g_cutscene_active.load();
}

uint32_t patch_flags() {
    uint32_t flags = REMASTER_FLAG_SEAMS | REMASTER_FLAG_WIDESCREEN | REMASTER_FLAG_CUTSCENE_SYNC |
        REMASTER_FLAG_PARTICLES | REMASTER_FLAG_LOADING_FADES | REMASTER_FLAG_TEXTURE_FILTER |
        REMASTER_FLAG_INTERPOLATION;
    if (draw_distance() != DrawDistance::Off) {
        flags |= REMASTER_FLAG_DRAW_DISTANCE;
    }
    if (cutscene_skip_enabled()) {
        flags |= REMASTER_FLAG_CUTSCENE_SKIP;
    }
    return flags;
}

int fade_steps() {
    return 4;
}

int bilerp_mode() {
    /* G_EX_BILERP_ALL. Point-sampled textures are filtered with the rest. */
    return 2;
}

void adjust_fog(int16_t& start, int16_t& end) {
    const DrawDistance level = draw_distance();
    if (level == DrawDistance::Off) {
        return;
    }
    int fog_start = start;
    int fog_end = end;
    if (fog_start < 0) {
        fog_start = 0;
    }
    if (fog_end < 0) {
        fog_end = 0;
    }
    if (fog_end > 1000) {
        fog_end = 1000;
    }
    if (fog_start > fog_end) {
        fog_start = fog_end;
    }
    if (fog_end <= fog_start) {
        start = static_cast<int16_t>(fog_start);
        end = static_cast<int16_t>(fog_end);
        return;
    }
    /* Low is a small, fill-rate-friendly push. Medium goes further and is still capped. */
    const int scale_q8 = level == DrawDistance::Low ? 294 : 346;
    int extra = ((fog_end - fog_start) * (scale_q8 - 256)) / 256;
    if (extra < 0) {
        extra = 0;
    }
    if (extra > 400) {
        extra = 400;
    }
    fog_end += extra;
    if (fog_end > 1000) {
        fog_end = 1000;
    }
    start = static_cast<int16_t>(fog_start);
    end = static_cast<int16_t>(fog_end);
}

int widescreen_scale_q8(float window_aspect) {
    constexpr float kOriginal = 4.0f / 3.0f;
    if (!(window_aspect > kOriginal + 0.02f)) {
        return 256;
    }
    float scale = kOriginal / window_aspect;
    /* Keep extreme ultrawide from opening the frustum without limit. 16:10 and 16:9 are unchanged. */
    if (scale < 0.50f) {
        scale = 0.50f;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    return std::clamp(static_cast<int>(scale * 256.0f + 0.5f), 128, 256);
}

bool scale_projection_matrix(void* mtx64, int scale_q8) {
    if (mtx64 == nullptr || scale_q8 == 256) {
        return false;
    }
    scale_q8 = std::clamp(scale_q8, 128, 256);
    auto* bytes = static_cast<uint8_t*>(mtx64);
    int16_t ints[4];
    uint16_t frac = 0;
    std::memcpy(ints, bytes, sizeof(ints));
    std::memcpy(&frac, bytes + 32, sizeof(frac));
    if (ints[1] != 0 || ints[2] != 0 || ints[3] != 0 || ints[0] == 0) {
        return false;
    }
    if (ints[0] > 32 || ints[0] < -32) {
        return false;
    }
    const int32_t fixed = (static_cast<int32_t>(ints[0]) << 16) | frac;
    const int64_t scaled = (static_cast<int64_t>(fixed) * scale_q8) / 256;
    if (scaled > 0x7FFFFFFFLL || scaled < -0x80000000LL) {
        return false;
    }
    const auto result = static_cast<int32_t>(scaled);
    const int16_t new_int = static_cast<int16_t>(result >> 16);
    const uint16_t new_frac = static_cast<uint16_t>(result & 0xFFFF);
    std::memcpy(bytes, &new_int, sizeof(new_int));
    std::memcpy(bytes + 32, &new_frac, sizeof(new_frac));
    return true;
}

WindowFit fit_window_to_display(int desired_w, int desired_h, int display_w, int display_h) {
    WindowFit fit;
    fit.width = desired_w;
    fit.height = desired_h;
    if (display_w <= 0 || display_h <= 0) {
        return fit;
    }
    int max_w = display_w;
    int max_h = display_h;
    if (display_w > 400) {
        max_w = display_w - 32;
    }
    if (display_h > 300) {
        max_h = display_h - 64;
    }
    if (fit.width > max_w) {
        fit.width = max_w;
        fit.clamped = true;
    }
    if (fit.height > max_h) {
        fit.height = max_h;
        fit.clamped = true;
    }
    if (fit.width < 1) {
        fit.width = 1;
    }
    if (fit.height < 1) {
        fit.height = 1;
    }
    return fit;
}

void tick_autosave() {
    if (g_app_folder.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_last_autosave.time_since_epoch().count() != 0 && now - g_last_autosave < std::chrono::seconds(60)) {
        return;
    }
    g_last_autosave = now;
    std::error_code ec;
    std::vector<std::filesystem::path> saves;
    auto consider = [&](const std::filesystem::path& path) {
        if (std::filesystem::is_regular_file(path, ec) && is_save_name(path)) {
            saves.push_back(path);
        }
    };
    for (std::filesystem::directory_iterator it(g_app_folder, ec), end; !ec && it != end; it.increment(ec)) {
        consider(it->path());
    }
    const auto save_dir = g_app_folder / "saves";
    if (std::filesystem::is_directory(save_dir, ec)) {
        for (std::filesystem::directory_iterator it(save_dir, ec), end; !ec && it != end; it.increment(ec)) {
            consider(it->path());
        }
    }
    if (saves.empty()) {
        return;
    }
    const auto dest_root = g_app_folder / "autosave";
    std::filesystem::create_directories(dest_root, ec);
    std::filesystem::rename(dest_root / "slot2", dest_root / "slot3", ec);
    ec.clear();
    std::filesystem::rename(dest_root / "slot1", dest_root / "slot2", ec);
    ec.clear();
    std::filesystem::rename(dest_root / "slot0", dest_root / "slot1", ec);
    uint64_t budget = 32ull << 20;
    for (const auto& save : saves) {
        std::filesystem::path relative = save.lexically_relative(g_app_folder);
        if (relative.empty() || *relative.begin() == "..") {
            relative = save.filename();
        }
        copy_file_limited(save, dest_root / "slot0" / relative, budget);
        if (budget == 0) {
            break;
        }
    }
}

} // namespace remaster
