#ifndef REMASTER_UPSCALE_HPP
#define REMASTER_UPSCALE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace remaster::upscale {

struct Rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<Rgba> pixels;
};

Image from_rgba16(const uint8_t* bytes, int width, int height);
Image epx2(const Image& source);
Image epx4(const Image& source);
bool looks_like_texture(const Image& image);
std::vector<uint8_t> write_png(const Image& image);

/* How many 32x32 blocks a machine should scan. Scaled by the texture budget. */
int scan_cap_for_budget(uint64_t budget_bytes, int scale);

struct RomHit {
    uint64_t offset = 0;
    Image image;
};

/* Reads at most `cap` likely 32x32 RGBA16 blocks. Never writes them back into the ROM. */
std::vector<RomHit> scan_rom(const uint8_t* rom, size_t size, int cap, int scale);

} // namespace remaster::upscale

#endif
