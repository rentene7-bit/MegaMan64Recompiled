#include "remaster.hpp"
#include "remaster_flags.h"
#include "xa.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL " << message << "\n";
        g_failed++;
    }
}

void put_u32_le(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

void put_u32_be(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

void put_u32_both(uint8_t* p, uint32_t value) {
    put_u32_le(p, value);
    put_u32_be(p + 4, value);
}

uint8_t write_record(uint8_t* p, uint32_t lba, uint32_t length, bool directory, const std::string& name) {
    const uint8_t name_len = static_cast<uint8_t>(name.size());
    uint8_t rec_len = static_cast<uint8_t>(33 + name_len);
    if (rec_len & 1) {
        rec_len++;
    }
    std::memset(p, 0, rec_len);
    p[0] = rec_len;
    put_u32_le(p + 2, lba);
    put_u32_be(p + 6, lba);
    put_u32_le(p + 10, length);
    put_u32_be(p + 14, length);
    p[25] = directory ? 0x02 : 0;
    p[32] = name_len;
    std::memcpy(p + 33, name.data(), name_len);
    return rec_len;
}

void write_mode2(uint8_t* raw, const uint8_t logical[2048], uint8_t file, uint8_t channel, uint8_t submode, uint8_t coding) {
    std::memset(raw, 0, 2352);
    raw[0] = 0x00;
    std::memset(raw + 1, 0xFF, 10);
    raw[11] = 0x00;
    raw[15] = 0x02;
    raw[16] = file;
    raw[17] = channel;
    raw[18] = submode;
    raw[19] = coding;
    std::memcpy(raw + 20, raw + 16, 4);
    std::memcpy(raw + 24, logical, 2048);
}

std::vector<uint8_t> make_logical_image(const char* boot, const char* volume, const std::string& seq) {
    constexpr int kSectors = 22;
    std::vector<uint8_t> image(kSectors * 2048, 0);
    auto sector = [&](int index) { return image.data() + index * 2048; };

    uint8_t* pvd = sector(16);
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    std::memset(pvd + 40, ' ', 32);
    std::memcpy(pvd + 40, volume, std::strlen(volume));
    put_u32_both(pvd + 80, kSectors);
    write_record(pvd + 156, 18, 2048, true, std::string(1, '\0'));

    uint8_t* dir = sector(18);
    uint8_t* cursor = dir;
    cursor += write_record(cursor, 19, static_cast<uint32_t>(std::strlen(boot)), false, "SYSTEM.CNF;1");
    cursor += write_record(cursor, 20, 2048, false, "VOICE.XA;1");
    write_record(cursor, 21, static_cast<uint32_t>(seq.size()), false, "BGM.SEQ;1");
    std::memcpy(sector(19), boot, std::strlen(boot));
    std::memcpy(sector(21), seq.data(), seq.size());
    return image;
}

std::vector<uint8_t> wrap_mode2(const std::vector<uint8_t>& logical, int xa_sector) {
    const int sectors = static_cast<int>(logical.size() / 2048);
    std::vector<uint8_t> bin(static_cast<size_t>(sectors) * 2352, 0);
    for (int i = 0; i < sectors; i++) {
        uint8_t submode = 0x08;
        uint8_t coding = 0;
        if (i == xa_sector) {
            submode = 0x04;
            coding = 0x01;
        }
        write_mode2(bin.data() + static_cast<size_t>(i) * 2352, logical.data() + i * 2048, 1, 0, submode, coding);
    }
    uint8_t* group = bin.data() + static_cast<size_t>(xa_sector) * 2352 + 24;
    group[0] = group[4] = 0;
    group[1] = group[5] = 0;
    group[16] = 0x01;
    return bin;
}

std::string seq_bytes() {
    std::string seq = "pQES";
    seq.append(12, '\0');
    seq.push_back(0); /* delta */
    seq.push_back(static_cast<char>(0x90));
    seq.push_back(60);
    seq.push_back(100);
    seq.push_back(10); /* delta */
    seq.push_back(static_cast<char>(0x80));
    seq.push_back(60);
    seq.push_back(0);
    seq.push_back(0);
    seq.push_back(static_cast<char>(0xFF));
    seq.push_back(0x2F);
    seq.push_back(0);
    return seq;
}

void test_xa_and_spu() {
    uint8_t logical[2048] = {};
    uint8_t raw[2352];
    write_mode2(raw, logical, 1, 0, 0x04, 0x01);
    uint8_t* group = raw + 24;
    group[4] = 0;
    group[5] = 0;
    group[16] = 0x01;
    remaster::xa::ChannelState state[2];
    int16_t pcm[4096 * 2] = {};
    int rate = 0;
    bool stereo = false;
    const size_t frames = remaster::xa::decode_sector(raw, state, pcm, &rate, &stereo);
    expect(frames == 18u * 4u * 28u, "xa stereo frame count");
    expect(stereo, "xa stereo flag");
    expect(rate == 37800, "xa rate");
    expect(pcm[0] == 4096, "xa first left sample");
    expect(pcm[1] == 0, "xa first right sample");

    uint8_t block[16] = {};
    block[2] = 0x01;
    int16_t spu[28] = {};
    remaster::xa::ChannelState spu_state;
    expect(remaster::xa::decode_spu_block(block, spu_state, spu) == 28, "spu block length");
    expect(spu[0] == 4096, "spu first sample");
}

void test_disc_and_audio(const std::filesystem::path& dir) {
    const std::string boot = "BOOT = cdrom:\\SLUS_005.61;1\n";
    const std::string seq = seq_bytes();
    auto logical = make_logical_image(boot.c_str(), "MEGAMAN_LEGENDS", seq);
    auto bin = wrap_mode2(logical, 20);
    const int audio_sectors = 2;
    bin.resize(bin.size() + static_cast<size_t>(audio_sectors) * 2352, 0);
    for (int i = 0; i < audio_sectors * (2352 / 4); i++) {
        uint8_t* sample = bin.data() + logical.size() / 2048 * 2352 + i * 4;
        sample[0] = 0x00;
        sample[1] = 0x10;
        sample[2] = 0x00;
        sample[3] = 0x20;
    }
    const auto bin_path = dir / "game.bin";
    const auto cue_path = dir / "game.cue";
    {
        std::ofstream out(bin_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }
    {
        std::ofstream cue(cue_path);
        cue << "FILE \"game.bin\" BINARY\n";
        cue << "  TRACK 01 MODE2/2352\n";
        cue << "    INDEX 01 00:00:00\n";
        cue << "  TRACK 02 AUDIO\n";
        cue << "    INDEX 01 00:00:22\n";
    }
    {
        std::ofstream map(dir / "voice_map.json");
        map << "{ \"voices\": [ { \"id\": 7, \"stream\": \"VOICE\" } ] }\n";
    }

    remaster::set_disc_path(cue_path);
    const remaster::DiscReport& report = remaster::disc_report();
    expect(report.loaded, "cue disc loads: " + report.summary);
    expect(report.language == "english", "english language from SLUS");
    expect(report.region == "ntsc-u", "ntsc-u region");
    expect(report.serial == "SLUS-00561", "serial " + report.serial);
    expect(report.xa_streams == 1, "one xa stream");
    expect(report.cdda_tracks == 1, "one cdda track");
    expect(report.seq_files == 1, "one seq file");
    expect(report.seq_notes == 1, "one seq note-on, got " + std::to_string(report.seq_notes));
    expect(report.has_raw_audio, "bin has raw audio");
    expect(remaster::play_voice(7), "mapped voice plays");
    expect(remaster::voice_active(), "voice is active");
    std::vector<float> mix(256 * 2, 0.0f);
    remaster::set_output_gain(1.0f);
    remaster::mix_audio(mix.data(), 256, 48000);
    bool audible = false;
    for (float sample : mix) {
        if (sample != 0.0f) {
            audible = true;
            break;
        }
    }
    expect(audible, "xa voice reaches the mixer");

    remaster::set_ps1_music_enabled(true);
    expect(remaster::play_cdda_track(0), "cdda track plays when music is enabled");
    std::fill(mix.begin(), mix.end(), 0.0f);
    remaster::mix_audio(mix.data(), 256, 48000);
    audible = false;
    for (float sample : mix) {
        if (sample != 0.0f) {
            audible = true;
            break;
        }
    }
    expect(audible, "cdda reaches the mixer");
    remaster::set_ps1_music_enabled(false);
    expect(!remaster::play_cdda_track(0), "cdda stays off when music is disabled");

    const auto iso_path = dir / "game.iso";
    {
        std::ofstream out(iso_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(logical.data()), static_cast<std::streamsize>(logical.size()));
    }
    remaster::set_disc_path(iso_path);
    const remaster::DiscReport iso = remaster::disc_report();
    expect(iso.loaded, "iso loads");
    expect(!iso.has_raw_audio, "iso has no xa payload");
    expect(iso.xa_streams == 0, "iso indexes no xa");
    expect(iso.summary.find("no raw XA") != std::string::npos, "iso explains the missing audio");

    auto jp_logical = make_logical_image("BOOT = cdrom:\\SLPS_011.07;1\n", "ROCKMAN_DASH", seq);
    const auto jp_iso = dir / "jp.iso";
    {
        std::ofstream out(jp_iso, std::ios::binary);
        out.write(reinterpret_cast<const char*>(jp_logical.data()), static_cast<std::streamsize>(jp_logical.size()));
    }
    remaster::set_disc_path(jp_iso);
    const remaster::DiscReport jp = remaster::disc_report();
    expect(jp.loaded, "japanese iso loads");
    expect(jp.language == "japanese", "japanese from SLPS");
    expect(jp.region == "ntsc-j", "ntsc-j region");

    remaster::set_disc_path(iso_path);
    remaster::set_secondary_disc_path(jp_iso);
    expect(remaster::dual_audio_active(), "english plus japanese discs enable dual audio");
    expect(remaster::voice_disc_index() == 1, "japanese disc supplies the voices");
    remaster::set_secondary_disc_path({});
    expect(!remaster::dual_audio_active(), "clearing the second disc leaves a single language");

    remaster::set_disc_path(dir / "missing.cue");
    const remaster::DiscReport missing = remaster::disc_report();
    expect(!missing.loaded, "missing disc does not load");
    expect(missing.summary.find("N64 soundtrack") != std::string::npos, "missing disc falls back");

    remaster::set_disc_path({});
    expect(!remaster::disc_report().loaded, "cleared disc unloads");
    expect(!remaster::play_voice(0), "voice without a disc is a no-op");
}

void write_hunk(std::vector<uint8_t>& file, uint32_t hunk_bytes, uint32_t entry, const uint8_t* data) {
    const uint64_t offset = static_cast<uint64_t>(entry) * hunk_bytes;
    if (file.size() < offset + hunk_bytes) {
        file.resize(static_cast<size_t>(offset + hunk_bytes), 0);
    }
    std::memcpy(file.data() + offset, data, hunk_bytes);
}

void test_chd(const std::filesystem::path& dir) {
    constexpr uint32_t kHunk = 2048;
    auto logical = make_logical_image("BOOT = cdrom:\\SLUS_005.61;1\n", "MEGAMAN_LEGENDS", seq_bytes());
    const int kSectors = static_cast<int>(logical.size() / 2048);
    std::vector<uint8_t> chd(124, 0);
    std::memcpy(chd.data(), "MComprHD", 8);
    put_u32_be(chd.data() + 8, 124);
    put_u32_be(chd.data() + 12, 5);
    auto put_u64_be = [](uint8_t* p, uint64_t value) {
        for (int i = 7; i >= 0; i--) {
            p[i] = static_cast<uint8_t>(value);
            value >>= 8;
        }
    };
    put_u64_be(chd.data() + 32, static_cast<uint64_t>(kSectors) * kHunk);
    put_u64_be(chd.data() + 40, 124);
    put_u32_be(chd.data() + 56, kHunk);
    put_u32_be(chd.data() + 60, kHunk);
    chd.resize(124 + kSectors * 4, 0);
    for (int i = 0; i < kSectors; i++) {
        put_u32_be(chd.data() + 124 + i * 4, static_cast<uint32_t>(i + 1));
        write_hunk(chd, kHunk, static_cast<uint32_t>(i + 1), logical.data() + i * 2048);
    }
    const auto path = dir / "game.chd";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(chd.data()), static_cast<std::streamsize>(chd.size()));
    }
    remaster::set_disc_path(path);
    const remaster::DiscReport report = remaster::disc_report();
    expect(report.loaded, "uncompressed chd loads: " + report.summary);
    expect(report.format == "CHD", "chd format");
    expect(report.language == "english", "chd region");
    expect(!report.has_raw_audio, "2048 chd has no raw audio");

    std::vector<uint8_t> compressed = chd;
    put_u32_be(compressed.data() + 16, 0x7A6C6962u); /* 'zlib' */
    const auto compressed_path = dir / "compressed.chd";
    {
        std::ofstream out(compressed_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
    }
    remaster::set_disc_path(compressed_path);
    const remaster::DiscReport denied = remaster::disc_report();
    expect(!denied.loaded, "compressed chd is refused");
    expect(denied.summary.find("Compressed CHD") != std::string::npos, "compressed chd explains itself");
}

void test_presentation() {
    expect((remaster::patch_flags() & REMASTER_FLAG_SEAMS) != 0, "seams are always on");
    expect((remaster::patch_flags() & REMASTER_FLAG_TEXTURE_FILTER) != 0, "filtering is always on");
    expect((remaster::patch_flags() & REMASTER_FLAG_DRAW_DISTANCE) == 0, "draw distance defaults off");
    expect(remaster::fade_steps() == 4, "loading fades are trimmed");
    expect(remaster::bilerp_mode() == 2, "textures use full filtering");

    int16_t start = 100;
    int16_t end = 900;
    remaster::adjust_fog(start, end);
    expect(start == 100 && end == 900, "fog is unchanged while draw distance is off");
    remaster::set_draw_distance(remaster::DrawDistance::Low);
    remaster::adjust_fog(start, end);
    expect(end > 900 && end <= 1000, "low draw distance extends fog");
    start = 100;
    end = 1000;
    remaster::adjust_fog(start, end);
    expect(end == 1000, "fog never passes 1000");
    remaster::set_draw_distance(remaster::DrawDistance::Off);
    expect((remaster::patch_flags() & REMASTER_FLAG_DRAW_DISTANCE) == 0, "draw distance can be disabled");

    expect(remaster::widescreen_scale_q8(4.0f / 3.0f) == 256, "4:3 projection is untouched");
    const int deck = remaster::widescreen_scale_q8(1280.0f / 800.0f);
    expect(deck < 256 && deck > 128, "1280x800 widens the view");
    expect(remaster::widescreen_scale_q8(32.0f / 9.0f) == 128, "ultrawide fov is capped");

    uint8_t matrix[64] = {};
    int16_t xx = 1;
    std::memcpy(matrix, &xx, sizeof(xx));
    expect(remaster::scale_projection_matrix(matrix, 256) == false, "identity scale does not edit the matrix");
    expect(remaster::scale_projection_matrix(matrix, 128), "valid matrix can be widened");
    int16_t scaled = 0;
    std::memcpy(&scaled, matrix, sizeof(scaled));
    expect(scaled == 0, "integer part halved");
    int16_t xy = 5;
    std::memcpy(matrix + 2, &xy, sizeof(xy));
    std::memcpy(matrix, &xx, sizeof(xx));
    expect(!remaster::scale_projection_matrix(matrix, 128), "non-perspective matrix is left alone");

    const remaster::WindowFit deck_window = remaster::fit_window_to_display(1600, 960, 1280, 800);
    expect(deck_window.clamped && deck_window.width <= 1248 && deck_window.height <= 736, "window fits a 1280x800 display");
    const remaster::WindowFit desktop = remaster::fit_window_to_display(1600, 960, 1920, 1080);
    expect(!desktop.clamped && desktop.width == 1600 && desktop.height == 960, "1080p keeps the preferred window");
    const remaster::WindowFit large = remaster::fit_window_to_display(1600, 960, 3840, 2160);
    expect(!large.clamped, "a large display is not treated as a handheld");

    std::vector<remaster::TexturePackCandidate> packs = {
        {"big", 600ull << 20},
        {"small", 100ull << 20},
        {"mid", 300ull << 20},
    };
    auto chosen = remaster::select_texture_packs(packs, 512ull << 20);
    expect(chosen.size() == 2, "budget keeps the packs that fit");
    expect(chosen[0].bytes == (100ull << 20) && chosen[1].bytes == (300ull << 20), "smaller packs are preferred");
    expect(remaster::select_texture_packs(packs, UINT64_MAX).size() == 3, "unlimited budget keeps every pack");
    expect(remaster::texture_budget() == remaster::TextureBudget::Mb512, "default texture budget is 512 MB");
}

void test_autosave(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    {
        std::ofstream save(dir / "game.sav", std::ios::binary);
        save << "SAVE";
    }
    remaster::set_app_folder(dir);
    remaster::tick_autosave();
    expect(std::filesystem::exists(dir / "autosave" / "slot0" / "game.sav"), "autosave copies a save file");
}

} // namespace

int run_idea_tests();
int run_addon_tests();

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "mm64-remaster-tests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    test_xa_and_spu();
    test_disc_and_audio(dir);
    test_chd(dir);
    test_presentation();
    test_autosave(dir / "app");
    g_failed += run_idea_tests();
    g_failed += run_addon_tests();
    std::filesystem::remove_all(dir);
    if (g_failed != 0) {
        std::cerr << g_failed << " failure(s)\n";
        return 1;
    }
    std::cout << "remaster tests passed\n";
    return 0;
}
