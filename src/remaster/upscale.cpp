#include "upscale.hpp"

#include <algorithm>
#include <cstring>

namespace remaster::upscale {
namespace {

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t adler32(const uint8_t* data, size_t size) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < size; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void png_chunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data, size_t size) {
    const uint32_t length = static_cast<uint32_t>(size);
    out.push_back(static_cast<uint8_t>(length >> 24));
    out.push_back(static_cast<uint8_t>(length >> 16));
    out.push_back(static_cast<uint8_t>(length >> 8));
    out.push_back(static_cast<uint8_t>(length));
    const size_t type_at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data, data + size);
    const uint32_t crc = crc32(out.data() + type_at, 4 + size);
    out.push_back(static_cast<uint8_t>(crc >> 24));
    out.push_back(static_cast<uint8_t>(crc >> 16));
    out.push_back(static_cast<uint8_t>(crc >> 8));
    out.push_back(static_cast<uint8_t>(crc));
}

Rgba at(const Image& image, int x, int y) {
    x = std::clamp(x, 0, image.width - 1);
    y = std::clamp(y, 0, image.height - 1);
    return image.pixels[static_cast<size_t>(y * image.width + x)];
}

bool same(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

} // namespace

Image from_rgba16(const uint8_t* bytes, int width, int height) {
    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width * height));
    for (int i = 0; i < width * height; i++) {
        const uint16_t pixel = static_cast<uint16_t>((bytes[i * 2] << 8) | bytes[i * 2 + 1]);
        Rgba& out = image.pixels[static_cast<size_t>(i)];
        out.r = static_cast<uint8_t>(((pixel >> 11) & 31) * 255 / 31);
        out.g = static_cast<uint8_t>(((pixel >> 6) & 31) * 255 / 31);
        out.b = static_cast<uint8_t>(((pixel >> 1) & 31) * 255 / 31);
        out.a = (pixel & 1) ? 255 : 0;
    }
    return image;
}

Image epx2(const Image& source) {
    Image dest;
    dest.width = source.width * 2;
    dest.height = source.height * 2;
    dest.pixels.resize(static_cast<size_t>(dest.width * dest.height));
    for (int y = 0; y < source.height; y++) {
        for (int x = 0; x < source.width; x++) {
            const Rgba p = at(source, x, y);
            const Rgba a = at(source, x, y - 1);
            const Rgba c = at(source, x - 1, y);
            const Rgba b = at(source, x + 1, y);
            const Rgba d = at(source, x, y + 1);
            Rgba p1 = p;
            Rgba p2 = p;
            Rgba p3 = p;
            Rgba p4 = p;
            if (same(c, a) && !same(c, b) && !same(a, d)) p1 = a;
            if (same(a, b) && !same(a, c) && !same(b, d)) p2 = b;
            if (same(d, c) && !same(d, b) && !same(c, a)) p3 = c;
            if (same(b, d) && !same(b, a) && !same(d, c)) p4 = d;
            const int dx = x * 2;
            const int dy = y * 2;
            dest.pixels[static_cast<size_t>(dy * dest.width + dx)] = p1;
            dest.pixels[static_cast<size_t>(dy * dest.width + dx + 1)] = p2;
            dest.pixels[static_cast<size_t>((dy + 1) * dest.width + dx)] = p3;
            dest.pixels[static_cast<size_t>((dy + 1) * dest.width + dx + 1)] = p4;
        }
    }
    return dest;
}

Image epx4(const Image& source) {
    return epx2(epx2(source));
}

bool looks_like_texture(const Image& image) {
    if (image.pixels.size() < 16) {
        return false;
    }
    int unique = 0;
    int opaque = 0;
    Rgba first = image.pixels[0];
    bool varied = false;
    for (const Rgba& pixel : image.pixels) {
        if (!same(pixel, first)) {
            varied = true;
        }
        if (pixel.a != 0) {
            opaque++;
        }
    }
    if (!varied || opaque < static_cast<int>(image.pixels.size()) / 5) {
        return false;
    }
    /* Reject blocks that are mostly one color with a few noisy pixels. */
    int buckets[8] = {};
    for (const Rgba& pixel : image.pixels) {
        const int bucket = ((pixel.r >> 6) << 2) | ((pixel.g >> 6) << 1) | (pixel.b >> 7);
        buckets[bucket & 7]++;
    }
    for (int count : buckets) {
        if (count > 8) {
            unique++;
        }
    }
    return unique >= 2;
}

std::vector<uint8_t> write_png(const Image& image) {
    std::vector<uint8_t> raw(static_cast<size_t>(image.height) * (1 + image.width * 4));
    for (int y = 0; y < image.height; y++) {
        uint8_t* row = raw.data() + static_cast<size_t>(y) * (1 + image.width * 4);
        row[0] = 0;
        for (int x = 0; x < image.width; x++) {
            const Rgba pixel = image.pixels[static_cast<size_t>(y * image.width + x)];
            row[1 + x * 4] = pixel.r;
            row[2 + x * 4] = pixel.g;
            row[3 + x * 4] = pixel.b;
            row[4 + x * 4] = pixel.a;
        }
    }
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t chunk = std::min<size_t>(65535, raw.size() - offset);
        const bool last = offset + chunk == raw.size();
        zlib.push_back(last ? 0x01 : 0x00);
        zlib.push_back(static_cast<uint8_t>(chunk));
        zlib.push_back(static_cast<uint8_t>(chunk >> 8));
        const uint16_t nlen = static_cast<uint16_t>(~chunk);
        zlib.push_back(static_cast<uint8_t>(nlen));
        zlib.push_back(static_cast<uint8_t>(nlen >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    const uint32_t adler = adler32(raw.data(), raw.size());
    zlib.push_back(static_cast<uint8_t>(adler >> 24));
    zlib.push_back(static_cast<uint8_t>(adler >> 16));
    zlib.push_back(static_cast<uint8_t>(adler >> 8));
    zlib.push_back(static_cast<uint8_t>(adler));

    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    uint8_t ihdr[13] = {};
    ihdr[0] = static_cast<uint8_t>(image.width >> 24);
    ihdr[1] = static_cast<uint8_t>(image.width >> 16);
    ihdr[2] = static_cast<uint8_t>(image.width >> 8);
    ihdr[3] = static_cast<uint8_t>(image.width);
    ihdr[4] = static_cast<uint8_t>(image.height >> 24);
    ihdr[5] = static_cast<uint8_t>(image.height >> 16);
    ihdr[6] = static_cast<uint8_t>(image.height >> 8);
    ihdr[7] = static_cast<uint8_t>(image.height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    png_chunk(png, "IHDR", ihdr, sizeof(ihdr));
    png_chunk(png, "IDAT", zlib.data(), zlib.size());
    png_chunk(png, "IEND", nullptr, 0);
    return png;
}

int scan_cap_for_budget(uint64_t budget_bytes, int scale) {
    if (scale <= 1 || budget_bytes == 0) {
        return 0;
    }
    const uint64_t mb = budget_bytes / (1024ull * 1024ull);
    int cap = 16;
    if (mb >= 256) cap = 32;
    if (mb >= 512) cap = 64;
    if (mb >= 1024) cap = 128;
    if (budget_bytes == UINT64_MAX) cap = 128;
    if (scale >= 4) {
        cap /= 2;
    }
    return cap;
}

std::vector<RomHit> scan_rom(const uint8_t* rom, size_t size, int cap, int scale) {
    std::vector<RomHit> hits;
    if (rom == nullptr || cap <= 0 || size < 32u * 32u * 2u) {
        return hits;
    }
    const size_t block = 32u * 32u * 2u;
    for (size_t offset = 0; offset + block <= size && static_cast<int>(hits.size()) < cap; offset += 2048) {
        Image decoded = from_rgba16(rom + offset, 32, 32);
        if (!looks_like_texture(decoded)) {
            continue;
        }
        RomHit hit;
        hit.offset = offset;
        hit.image = scale >= 4 ? epx4(decoded) : epx2(decoded);
        hits.push_back(std::move(hit));
    }
    return hits;
}

} // namespace remaster::upscale
