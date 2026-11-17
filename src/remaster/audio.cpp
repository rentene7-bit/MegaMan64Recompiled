#include "disc_internal.hpp"
#include "xa.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace remaster {
namespace {

struct Playback {
    bool voice = false;
    bool music = false;
    int source = 0;
    int stream = -1;
    int track = -1;
    uint32_t sector = 0;
    uint32_t sector_count = 0;
    uint32_t start_lba = 0;
    uint8_t channel = 0;
    int rate_hz = 37800;
    bool stereo = true;
    xa::ChannelState xa_state[2];
    std::vector<int16_t> decoded;
    size_t decoded_pos = 0;
    double resample_pos = 0.0;
};

std::mutex playback_mutex;
Playback playback;
float output_gain = 1.0f;

void reset_playback(Playback& play) {
    play = {};
}

bool pull_voice_frames(Playback& play, int16_t* dst, size_t frames) {
    size_t filled = 0;
    while (filled < frames) {
        if (play.decoded_pos >= play.decoded.size()) {
            play.decoded.clear();
            play.decoded_pos = 0;
            if (play.sector >= play.sector_count) {
                break;
            }
            uint8_t raw[2352];
            const bool got = disc::read_raw_of(play.source, play.start_lba + play.sector, raw);
            play.sector++;
            if (!got) {
                break;
            }
            if (raw[17] != play.channel || (raw[18] & 0x04) == 0) {
                continue;
            }
            play.decoded.resize(4096 * 2);
            int rate = play.rate_hz;
            bool stereo = play.stereo;
            const size_t produced = xa::decode_sector(raw, play.xa_state, play.decoded.data(), &rate, &stereo);
            play.decoded.resize(produced * 2);
            play.rate_hz = rate;
        }
        const size_t have = (play.decoded.size() - play.decoded_pos) / 2;
        const size_t take = std::min(have, frames - filled);
        std::copy_n(play.decoded.begin() + static_cast<std::ptrdiff_t>(play.decoded_pos), take * 2, dst + filled * 2);
        play.decoded_pos += take * 2;
        filled += take;
    }
    if (filled < frames) {
        std::fill(dst + filled * 2, dst + frames * 2, 0);
        play.voice = false;
    }
    return filled > 0;
}

bool pull_cdda_frames(Playback& play, int16_t* dst, size_t frames) {
    size_t filled = 0;
    while (filled < frames) {
        if (play.decoded_pos >= play.decoded.size()) {
            play.decoded.clear();
            play.decoded_pos = 0;
            if (play.sector >= play.sector_count) {
                break;
            }
            uint8_t raw[2352];
            const bool got = disc::read_cdda_of(play.source, play.track, play.sector, raw);
            play.sector++;
            if (!got) {
                break;
            }
            play.decoded.resize((2352 / 4) * 2);
            for (int i = 0; i < 2352 / 4; i++) {
                int16_t left = static_cast<int16_t>(raw[i * 4] | (raw[i * 4 + 1] << 8));
                int16_t right = static_cast<int16_t>(raw[i * 4 + 2] | (raw[i * 4 + 3] << 8));
                play.decoded[i * 2] = left;
                play.decoded[i * 2 + 1] = right;
            }
            play.rate_hz = 44100;
        }
        const size_t have = (play.decoded.size() - play.decoded_pos) / 2;
        const size_t take = std::min(have, frames - filled);
        std::copy_n(play.decoded.begin() + static_cast<std::ptrdiff_t>(play.decoded_pos), take * 2, dst + filled * 2);
        play.decoded_pos += take * 2;
        filled += take;
    }
    if (filled < frames) {
        std::fill(dst + filled * 2, dst + frames * 2, 0);
        play.music = false;
    }
    return filled > 0;
}

void mix_source(float* dst, size_t dst_frames, uint32_t dst_rate, const int16_t* src, size_t src_frames, int src_rate, float gain) {
    if (dst == nullptr || src == nullptr || dst_frames == 0 || src_frames == 0 || dst_rate == 0 || src_rate <= 0 || gain == 0.0f) {
        return;
    }
    const double step = static_cast<double>(src_rate) / static_cast<double>(dst_rate);
    double cursor = 0.0;
    for (size_t i = 0; i < dst_frames; i++) {
        if (cursor + 1.0 >= static_cast<double>(src_frames)) {
            break;
        }
        const size_t index = static_cast<size_t>(cursor);
        const double frac = cursor - static_cast<double>(index);
        for (int channel = 0; channel < 2; channel++) {
            const float a = src[index * 2 + channel] / 32768.0f;
            const float b = src[(index + 1) * 2 + channel] / 32768.0f;
            dst[i * 2 + channel] += (a + (b - a) * static_cast<float>(frac)) * gain;
        }
        cursor += step;
    }
}

} // namespace

void set_output_gain(float gain) {
    std::lock_guard lock(playback_mutex);
    output_gain = std::clamp(gain, 0.0f, 1.0f);
}

void stop_ps1_audio() {
    std::lock_guard lock(playback_mutex);
    reset_playback(playback);
}

bool play_voice(int id) {
    const int source = voice_disc_index();
    const auto streams = disc::copy_xa(source);
    int index = id;
    if (source == 0) {
        std::lock_guard disc_lock(disc::mutex());
        index = disc::voice_stream_index(id);
    }
    if (index < 0 || index >= static_cast<int>(streams.size())) {
        return false;
    }
    const disc::XaStream stream = streams[static_cast<size_t>(index)];
    std::lock_guard play_lock(playback_mutex);
    reset_playback(playback);
    playback.voice = true;
    playback.source = source;
    playback.stream = index;
    playback.start_lba = stream.start_lba;
    playback.sector_count = stream.sector_count;
    playback.channel = stream.channel;
    playback.rate_hz = stream.rate_hz;
    playback.stereo = stream.stereo;
    return true;
}

bool play_cdda_from(int disc_index, int track_index) {
    const auto tracks = disc::copy_cdda(disc_index);
    if (track_index < 0 || track_index >= static_cast<int>(tracks.size())) {
        return false;
    }
    std::lock_guard play_lock(playback_mutex);
    reset_playback(playback);
    playback.music = true;
    playback.source = disc_index;
    playback.track = track_index;
    playback.sector_count = tracks[static_cast<size_t>(track_index)].sector_count;
    playback.rate_hz = 44100;
    return true;
}

bool play_cdda_track(int track_index) {
    if (!ps1_music_enabled()) {
        return false;
    }
    return play_cdda_from(0, track_index);
}

bool voice_active() {
    std::lock_guard lock(playback_mutex);
    return playback.voice;
}

void mix_audio(float* interleaved_stereo, size_t frames, uint32_t sample_rate) {
    if (interleaved_stereo == nullptr || frames == 0 || sample_rate == 0) {
        return;
    }
    std::lock_guard lock(playback_mutex);
    if (!playback.voice && !playback.music) {
        return;
    }
    /* Pull a little extra so resampling does not run dry on this callback. */
    const size_t src_frames = frames + 8;
    std::vector<int16_t> src(src_frames * 2);
    bool produced = false;
    int rate = playback.rate_hz;
    if (playback.voice) {
        produced = pull_voice_frames(playback, src.data(), src_frames);
    } else if (playback.music && ps1_music_enabled()) {
        produced = pull_cdda_frames(playback, src.data(), src_frames);
    }
    if (!produced) {
        return;
    }
    if (playback.voice && !motion_comfort()) {
        /* One short delay tap. Cheap enough for a shared-memory GPU machine, and skipped by motion comfort. */
        for (size_t i = 4; i < src.size(); i++) {
            const int mixed = src[i] + src[i - 4] / 6;
            src[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
        }
    }
    const float gain = output_gain * (playback.voice ? 0.9f : 0.55f);
    mix_source(interleaved_stereo, frames, sample_rate, src.data(), src_frames, rate, gain);
}

} // namespace remaster
