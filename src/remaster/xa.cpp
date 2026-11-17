#include "xa.hpp"

#include <algorithm>

namespace remaster::xa {
namespace {

/* Sony XA coefficients. XA uses filters 0..3. SPU also uses filter 4. */
constexpr int kPos[5] = {0, 60, 115, 98, 122};
constexpr int kNeg[5] = {0, 0, -52, -55, -60};

int clamp16(int sample) {
    return std::clamp(sample, -32768, 32767);
}

int signed_nibble(int nibble) {
    nibble &= 0x0F;
    return (nibble & 0x08) ? nibble - 16 : nibble;
}

int apply_filter(int t, int filter, int shift, ChannelState& state) {
    if (filter < 0 || filter > 4) {
        filter = 0;
    }
    if (shift < 0) {
        shift = 0;
    }
    int sample = (t << shift) + ((state.old * kPos[filter] + state.older * kNeg[filter] + 32) / 64);
    sample = clamp16(sample);
    state.older = state.old;
    state.old = sample;
    return sample;
}

void decode_28(const uint8_t* group, int block, int nibble, ChannelState& state, int16_t* dst, int dst_step) {
    const int header = group[4 + block * 2 + nibble];
    int stored_shift = header & 0x0F;
    if (stored_shift > 12) {
        stored_shift = 9;
    }
    const int shift = 12 - stored_shift;
    const int filter = (header >> 4) & 0x03;
    for (int j = 0; j < 28; j++) {
        const int t = signed_nibble(group[16 + block + j * 4] >> (nibble * 4));
        dst[j * dst_step] = static_cast<int16_t>(apply_filter(t, filter, shift, state));
    }
}

} // namespace

size_t decode_sector(const uint8_t* raw2352, ChannelState state[2], int16_t* dst, int* rate_hz, bool* stereo_out) {
    if (raw2352 == nullptr || state == nullptr || dst == nullptr) {
        return 0;
    }
    const uint8_t submode = raw2352[18];
    const uint8_t coding = raw2352[19];
    if ((submode & 0x04) == 0) {
        return 0;
    }
    const bool stereo = (coding & 0x03) == 1;
    if (stereo_out != nullptr) {
        *stereo_out = stereo;
    }
    if (rate_hz != nullptr) {
        *rate_hz = (coding & 0x04) ? 18900 : 37800;
    }

    const uint8_t* src = raw2352 + 24;
    size_t frames = 0;
    for (int group = 0; group < 18; group++) {
        for (int block = 0; block < 4; block++) {
            if (stereo) {
                decode_28(src, block, 0, state[0], dst + frames * 2, 2);
                decode_28(src, block, 1, state[1], dst + frames * 2 + 1, 2);
                frames += 28;
            } else {
                int16_t mono[56];
                decode_28(src, block, 0, state[0], mono, 1);
                decode_28(src, block, 1, state[0], mono + 28, 1);
                for (int i = 0; i < 56; i++) {
                    dst[(frames + i) * 2] = mono[i];
                    dst[(frames + i) * 2 + 1] = mono[i];
                }
                frames += 56;
            }
        }
        src += 128;
    }
    return frames;
}

size_t decode_spu_block(const uint8_t* block16, ChannelState& state, int16_t* dst28) {
    if (block16 == nullptr || dst28 == nullptr) {
        return 0;
    }
    int stored_shift = block16[0] & 0x0F;
    if (stored_shift > 12) {
        stored_shift = 9;
    }
    const int shift = 12 - stored_shift;
    int filter = (block16[0] >> 4) & 0x0F;
    if (filter > 4) {
        filter = 0;
    }
    for (int i = 0; i < 28; i++) {
        const uint8_t byte = block16[2 + i / 2];
        const int nibble = (i & 1) ? (byte >> 4) : (byte & 0x0F);
        dst28[i] = static_cast<int16_t>(apply_filter(signed_nibble(nibble), filter, shift, state));
    }
    return 28;
}

} // namespace remaster::xa
