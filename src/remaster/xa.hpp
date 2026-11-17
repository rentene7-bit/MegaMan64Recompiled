#ifndef REMASTER_XA_HPP
#define REMASTER_XA_HPP

#include <cstddef>
#include <cstdint>

namespace remaster::xa {

struct ChannelState {
    int32_t older = 0;
    int32_t old = 0;
};

/* Decode one Mode 2 XA sector. dst receives interleaved stereo frames.
 * Mono is duplicated. Returns the frame count, or 0 when the sector is not audio. */
size_t decode_sector(const uint8_t* raw2352, ChannelState state[2], int16_t* dst, int* rate_hz, bool* stereo);

/* One 16-byte SPU/VAG ADPCM block into 28 mono samples. */
size_t decode_spu_block(const uint8_t* block16, ChannelState& state, int16_t* dst28);

} // namespace remaster::xa

#endif
