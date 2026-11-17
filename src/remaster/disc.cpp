#include "disc_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <unordered_map>

namespace remaster::disc {
namespace {

uint32_t ru32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t ru32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint64_t ru64be(const uint8_t* p) {
    return ((uint64_t)ru32be(p) << 32) | ru32be(p + 4);
}

std::string trim_copy(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        start++;
    }
    return text.substr(start);
}

std::string upper_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

int msf_to_lba(int m, int s, int f) {
    if (m < 0 || s < 0 || f < 0) {
        return 0;
    }
    return (m * 60 + s) * 75 + f;
}

bool parse_msf(const std::string& text, int& lba) {
    int m = 0;
    int s = 0;
    int f = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &m, &s, &f) != 3) {
        return false;
    }
    lba = msf_to_lba(m, s, f);
    return true;
}

class BinaryFile {
public:
    bool open(const std::filesystem::path& path) {
        file_.close();
        file_.open(path, std::ios::binary);
        if (!file_) {
            size_ = 0;
            return false;
        }
        file_.seekg(0, std::ios::end);
        size_ = static_cast<uint64_t>(file_.tellg());
        file_.clear();
        return true;
    }

    uint64_t size() const { return size_; }

    bool read_at(uint64_t offset, uint8_t* dst, size_t bytes) {
        if (dst == nullptr || bytes == 0) {
            return false;
        }
        if (offset > size_ || bytes > size_ - offset) {
            return false;
        }
        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(bytes));
        return file_.good() || file_.gcount() == static_cast<std::streamsize>(bytes);
    }

private:
    std::ifstream file_;
    uint64_t size_ = 0;
};

bool extract_user2048(const uint8_t* raw, size_t raw_size, uint8_t* dst) {
    if (raw == nullptr || dst == nullptr) {
        return false;
    }
    if (raw_size == 2048) {
        std::memcpy(dst, raw, 2048);
        return true;
    }
    if (raw_size < 2352) {
        return false;
    }
    const int mode = raw[15];
    if (mode == 0x01) {
        std::memcpy(dst, raw + 16, 2048);
        return true;
    }
    std::memcpy(dst, raw + 24, 2048);
    return true;
}

struct Track {
    int number = 0;
    std::string type;
    std::filesystem::path file;
    int sector_bytes = 2352;
    uint64_t file_offset = 0;
    uint32_t sector_count = 0;
    bool data = false;
    bool audio = false;
    bool first_in_file = false;
};

int sector_bytes_for(const std::string& type) {
    if (type == "MODE1/2048" || type == "MODE2/2048") {
        return 2048;
    }
    if (type == "MODE2/2336") {
        return 2336;
    }
    return 2352;
}

bool is_data_type(const std::string& type) {
    return type.rfind("MODE", 0) == 0;
}

std::vector<Track> parse_cue(const std::filesystem::path& cue_path, std::string& error) {
    std::ifstream in(cue_path);
    if (!in) {
        error = "Could not open the CUE sheet.";
        return {};
    }
    std::vector<Track> tracks;
    std::filesystem::path current_file;
    bool file_has_track = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed.rfind("REM", 0) == 0) {
            continue;
        }
        std::string upper = upper_copy(trimmed);
        if (upper.rfind("FILE ", 0) == 0) {
            std::string rest = trim_copy(trimmed.substr(5));
            std::string name;
            if (!rest.empty() && rest.front() == '"') {
                auto end = rest.find('"', 1);
                if (end == std::string::npos) {
                    error = "The CUE sheet has a broken FILE name.";
                    return {};
                }
                name = rest.substr(1, end - 1);
            } else {
                auto space = rest.find(' ');
                name = rest.substr(0, space);
            }
            current_file = cue_path.parent_path() / std::filesystem::path(name);
            file_has_track = false;
            continue;
        }
        if (upper.rfind("TRACK ", 0) == 0) {
            if (current_file.empty()) {
                error = "The CUE sheet has a TRACK before a FILE.";
                return {};
            }
            int number = 0;
            char type_buf[64] = {};
            if (std::sscanf(upper.c_str(), "TRACK %d %63s", &number, type_buf) < 2) {
                error = "The CUE sheet has a TRACK line that could not be read.";
                return {};
            }
            Track track;
            track.number = number;
            track.type = type_buf;
            track.file = current_file;
            track.sector_bytes = sector_bytes_for(track.type);
            track.data = is_data_type(track.type);
            track.audio = track.type == "AUDIO";
            track.first_in_file = !file_has_track;
            file_has_track = true;
            tracks.push_back(track);
            continue;
        }
        if (upper.rfind("INDEX ", 0) == 0) {
            if (tracks.empty()) {
                continue;
            }
            int index = 0;
            char msf[32] = {};
            if (std::sscanf(upper.c_str(), "INDEX %d %31s", &index, msf) < 2) {
                continue;
            }
            if (index != 1 && !(index == 0 && tracks.back().file_offset == 0)) {
                continue;
            }
            int lba = 0;
            if (!parse_msf(msf, lba)) {
                continue;
            }
            if (index == 1 || tracks.back().file_offset == 0) {
                tracks.back().file_offset = static_cast<uint64_t>(lba) * static_cast<uint64_t>(tracks.back().sector_bytes);
            }
        }
    }
    if (tracks.empty()) {
        error = "The CUE sheet has no tracks.";
        return {};
    }

    for (size_t i = 0; i < tracks.size(); i++) {
        std::error_code ec;
        const auto file_size = std::filesystem::file_size(tracks[i].file, ec);
        if (ec) {
            error = "A file named by the CUE sheet is missing.";
            return {};
        }
        if (tracks[i].first_in_file && tracks[i].file_offset >= file_size) {
            tracks[i].file_offset = 0;
        }
        uint64_t end = file_size;
        for (size_t j = i + 1; j < tracks.size(); j++) {
            if (tracks[j].file == tracks[i].file) {
                end = tracks[j].file_offset;
                break;
            }
        }
        if (end < tracks[i].file_offset || tracks[i].sector_bytes <= 0) {
            tracks[i].sector_count = 0;
        } else {
            tracks[i].sector_count = static_cast<uint32_t>((end - tracks[i].file_offset) / static_cast<uint64_t>(tracks[i].sector_bytes));
        }
    }
    return tracks;
}

class Image {
public:
    virtual ~Image() = default;
    virtual bool read_logical(uint32_t lba, uint8_t* dst2048) = 0;
    virtual bool read_raw(uint32_t lba, uint8_t* dst2352) = 0;
    virtual bool read_cdda(int track_index, uint32_t sector, uint8_t* dst2352) = 0;
    virtual uint32_t logical_count() const = 0;
    virtual bool has_raw() const = 0;
};

class BinCueImage final : public Image {
public:
    bool open(const std::filesystem::path& cue_path, std::string& error) {
        tracks_ = parse_cue(cue_path, error);
        if (tracks_.empty()) {
            return false;
        }
        for (const Track& track : tracks_) {
            if (files_.find(track.file.string()) == files_.end()) {
                BinaryFile file;
                if (!file.open(track.file)) {
                    error = "Could not open a BIN named by the CUE sheet.";
                    return false;
                }
                files_.emplace(track.file.string(), std::move(file));
            }
        }
        for (size_t i = 0; i < tracks_.size(); i++) {
            if (tracks_[i].data) {
                data_track_ = static_cast<int>(i);
                break;
            }
        }
        if (data_track_ < 0) {
            error = "The CUE sheet has no data track.";
            return false;
        }
        for (size_t i = 0; i < tracks_.size(); i++) {
            if (tracks_[i].audio && tracks_[i].sector_count > 0) {
                CddaTrack audio;
                audio.number = tracks_[i].number;
                audio.file = tracks_[i].file.string();
                audio.file_offset_sectors = static_cast<uint32_t>(tracks_[i].file_offset / static_cast<uint64_t>(tracks_[i].sector_bytes));
                audio.sector_count = tracks_[i].sector_count;
                audio.sector_bytes = tracks_[i].sector_bytes;
                cdda_.push_back(audio);
            }
        }
        return true;
    }

    bool read_logical(uint32_t lba, uint8_t* dst2048) override {
        const Track& track = tracks_[data_track_];
        if (lba >= track.sector_count) {
            return false;
        }
        std::vector<uint8_t> raw(static_cast<size_t>(track.sector_bytes));
        const uint64_t offset = track.file_offset + static_cast<uint64_t>(lba) * static_cast<uint64_t>(track.sector_bytes);
        if (!files_.at(track.file.string()).read_at(offset, raw.data(), raw.size())) {
            return false;
        }
        return extract_user2048(raw.data(), raw.size(), dst2048);
    }

    bool read_raw(uint32_t lba, uint8_t* dst2352) override {
        const Track& track = tracks_[data_track_];
        if (track.sector_bytes != 2352 || lba >= track.sector_count) {
            return false;
        }
        const uint64_t offset = track.file_offset + static_cast<uint64_t>(lba) * 2352u;
        return files_.at(track.file.string()).read_at(offset, dst2352, 2352);
    }

    bool read_cdda(int track_index, uint32_t sector, uint8_t* dst2352) override {
        if (track_index < 0 || track_index >= static_cast<int>(cdda_.size())) {
            return false;
        }
        const CddaTrack& track = cdda_[track_index];
        if (sector >= track.sector_count || track.sector_bytes != 2352) {
            return false;
        }
        auto it = files_.find(track.file);
        if (it == files_.end()) {
            return false;
        }
        const uint64_t offset = (static_cast<uint64_t>(track.file_offset_sectors) + sector) * 2352u;
        return it->second.read_at(offset, dst2352, 2352);
    }

    uint32_t logical_count() const override { return tracks_[data_track_].sector_count; }
    bool has_raw() const override { return tracks_[data_track_].sector_bytes == 2352; }
    const std::vector<CddaTrack>& cdda() const { return cdda_; }

private:
    std::vector<Track> tracks_;
    std::vector<CddaTrack> cdda_;
    std::unordered_map<std::string, BinaryFile> files_;
    int data_track_ = -1;
};

class IsoImage final : public Image {
public:
    bool open(const std::filesystem::path& path, std::string& error) {
        if (!file_.open(path)) {
            error = "Could not open the ISO.";
            return false;
        }
        if (file_.size() < 2048 || (file_.size() % 2048) != 0) {
            error = "The ISO is not a 2048-byte sector image.";
            return false;
        }
        count_ = static_cast<uint32_t>(file_.size() / 2048);
        return true;
    }

    bool read_logical(uint32_t lba, uint8_t* dst2048) override {
        if (lba >= count_) {
            return false;
        }
        return file_.read_at(static_cast<uint64_t>(lba) * 2048u, dst2048, 2048);
    }

    bool read_raw(uint32_t, uint8_t*) override { return false; }
    bool read_cdda(int, uint32_t, uint8_t*) override { return false; }
    uint32_t logical_count() const override { return count_; }
    bool has_raw() const override { return false; }

private:
    BinaryFile file_;
    uint32_t count_ = 0;
};

class ChdImage final : public Image {
public:
    bool open(const std::filesystem::path& path, std::string& error) {
        if (!file_.open(path)) {
            error = "Could not open the CHD.";
            return false;
        }
        uint8_t header[124] = {};
        if (!file_.read_at(0, header, sizeof(header)) || std::memcmp(header, "MComprHD", 8) != 0) {
            error = "This is not a CHD v5 image.";
            return false;
        }
        version_ = ru32be(header + 12);
        if (version_ != 5) {
            error = "Only CHD v5 images are supported.";
            return false;
        }
        for (int i = 0; i < 4; i++) {
            compression_[i] = ru32be(header + 16 + i * 4);
        }
        if (compression_[0] != 0 || compression_[1] != 0 || compression_[2] != 0 || compression_[3] != 0) {
            error = "Compressed CHD is not supported yet. Use BIN/CUE, ISO, or an uncompressed CHD.";
            return false;
        }
        logical_bytes_ = ru64be(header + 32);
        map_offset_ = ru64be(header + 40);
        meta_offset_ = ru64be(header + 48);
        hunk_bytes_ = ru32be(header + 56);
        unit_bytes_ = ru32be(header + 60);
        if (hunk_bytes_ == 0 || unit_bytes_ == 0 || hunk_bytes_ > 8u * 1024u * 1024u || (hunk_bytes_ % unit_bytes_) != 0) {
            error = "The CHD hunk size is not usable.";
            return false;
        }
        hunk_count_ = static_cast<uint32_t>((logical_bytes_ + hunk_bytes_ - 1) / hunk_bytes_);
        if (hunk_count_ == 0 || hunk_count_ > 2000000u) {
            error = "The CHD is larger than this reader will open.";
            return false;
        }
        units_per_hunk_ = hunk_bytes_ / unit_bytes_;
        if (unit_bytes_ != 2048 && unit_bytes_ != 2352 && unit_bytes_ != 2448) {
            error = "The CHD sector size is not 2048, 2352, or 2448.";
            return false;
        }
        unit_count_ = static_cast<uint32_t>((logical_bytes_ + unit_bytes_ - 1) / unit_bytes_);
        parse_metadata();
        return true;
    }

    bool read_logical(uint32_t lba, uint8_t* dst2048) override {
        std::vector<uint8_t> unit(unit_bytes_);
        if (!read_unit(lba, unit.data())) {
            return false;
        }
        return extract_user2048(unit.data(), unit.size() >= 2352 ? 2352 : unit.size(), dst2048);
    }

    bool read_raw(uint32_t lba, uint8_t* dst2352) override {
        if (unit_bytes_ < 2352) {
            return false;
        }
        std::vector<uint8_t> unit(unit_bytes_);
        if (!read_unit(lba, unit.data())) {
            return false;
        }
        std::memcpy(dst2352, unit.data(), 2352);
        return true;
    }

    bool read_cdda(int track_index, uint32_t sector, uint8_t* dst2352) override {
        if (track_index < 0 || track_index >= static_cast<int>(cdda_.size())) {
            return false;
        }
        const CddaTrack& track = cdda_[track_index];
        if (sector >= track.sector_count) {
            return false;
        }
        return read_raw(track.file_offset_sectors + sector, dst2352);
    }

    uint32_t logical_count() const override { return unit_count_; }
    bool has_raw() const override { return unit_bytes_ >= 2352; }
    const std::vector<CddaTrack>& cdda() const { return cdda_; }

private:
    bool read_hunk(uint32_t hunk, uint8_t* dst) {
        if (hunk >= hunk_count_) {
            return false;
        }
        if (cached_hunk_ == hunk && cached_.size() == hunk_bytes_) {
            std::memcpy(dst, cached_.data(), hunk_bytes_);
            return true;
        }
        uint8_t map[4] = {};
        const uint64_t map_at = map_offset_ + static_cast<uint64_t>(hunk) * 4u;
        if (!file_.read_at(map_at, map, 4)) {
            return false;
        }
        const uint32_t entry = ru32be(map);
        if (entry == 0) {
            std::memset(dst, 0, hunk_bytes_);
        } else {
            const uint64_t offset = static_cast<uint64_t>(entry) * hunk_bytes_;
            if (!file_.read_at(offset, dst, hunk_bytes_)) {
                return false;
            }
        }
        cached_.assign(dst, dst + hunk_bytes_);
        cached_hunk_ = hunk;
        return true;
    }

    bool read_unit(uint32_t unit, uint8_t* dst) {
        if (unit >= unit_count_) {
            return false;
        }
        const uint32_t hunk = unit / units_per_hunk_;
        const uint32_t within = unit % units_per_hunk_;
        std::vector<uint8_t> hunk_data(hunk_bytes_);
        if (!read_hunk(hunk, hunk_data.data())) {
            return false;
        }
        std::memcpy(dst, hunk_data.data() + static_cast<size_t>(within) * unit_bytes_, unit_bytes_);
        return true;
    }

    void parse_metadata() {
        uint64_t offset = meta_offset_;
        for (int guard = 0; guard < 64 && offset != 0; guard++) {
            uint8_t head[16] = {};
            if (!file_.read_at(offset, head, sizeof(head))) {
                break;
            }
            const uint32_t tag = ru32be(head);
            uint32_t length = ru32be(head + 4) & 0x00FFFFFFu;
            const uint64_t next = ru64be(head + 8);
            if (length > 65536u) {
                break;
            }
            std::string text(length, '\0');
            if (length > 0 && !file_.read_at(offset + 16, reinterpret_cast<uint8_t*>(text.data()), length)) {
                break;
            }
            /* 'CHTR' or 'CHT2' track lists are plain text. */
            if (tag == 0x43485452u || tag == 0x43485432u) {
                parse_track_text(text);
            }
            if (next == offset) {
                break;
            }
            offset = next;
        }
    }

    void parse_track_text(const std::string& text) {
        uint32_t cursor = 0;
        std::string line;
        auto flush = [&]() {
            std::string upper = upper_copy(line);
            if (upper.find("TYPE:AUDIO") != std::string::npos) {
                CddaTrack track;
                track.number = static_cast<int>(cdda_.size()) + 2;
                track.file_offset_sectors = cursor;
                auto frames = upper.find("FRAMES:");
                uint32_t count = 0;
                if (frames != std::string::npos) {
                    count = static_cast<uint32_t>(std::strtoul(upper.c_str() + frames + 7, nullptr, 10));
                }
                track.sector_count = count;
                if (count > 0 && unit_bytes_ >= 2352) {
                    cdda_.push_back(track);
                }
                cursor += count;
            } else if (upper.find("TYPE:") != std::string::npos) {
                auto frames = upper.find("FRAMES:");
                if (frames != std::string::npos) {
                    cursor += static_cast<uint32_t>(std::strtoul(upper.c_str() + frames + 7, nullptr, 10));
                }
            }
            line.clear();
        };
        for (char ch : text) {
            if (ch == '\n' || ch == '\r') {
                if (!line.empty()) {
                    flush();
                }
            } else {
                line.push_back(ch);
            }
        }
        if (!line.empty()) {
            flush();
        }
    }

    BinaryFile file_;
    uint32_t version_ = 0;
    uint32_t compression_[4] = {};
    uint64_t logical_bytes_ = 0;
    uint64_t map_offset_ = 0;
    uint64_t meta_offset_ = 0;
    uint32_t hunk_bytes_ = 0;
    uint32_t unit_bytes_ = 0;
    uint32_t hunk_count_ = 0;
    uint32_t units_per_hunk_ = 0;
    uint32_t unit_count_ = 0;
    std::vector<uint8_t> cached_;
    uint32_t cached_hunk_ = 0xFFFFFFFFu;
    std::vector<CddaTrack> cdda_;
};

struct DirectoryRecord {
    uint32_t lba = 0;
    uint32_t length = 0;
    bool directory = false;
    std::string name;
};

bool read_records(Image& image, uint32_t lba, uint32_t bytes, std::vector<DirectoryRecord>& out) {
    uint32_t remaining = bytes;
    uint32_t sector = lba;
    while (remaining > 0) {
        uint8_t data[2048];
        if (!image.read_logical(sector, data)) {
            return false;
        }
        const uint32_t take = std::min<uint32_t>(remaining, 2048);
        uint32_t pos = 0;
        while (pos + 33 < take) {
            const uint8_t rec_len = data[pos];
            if (rec_len == 0) {
                break;
            }
            if (pos + rec_len > take || rec_len < 33) {
                return false;
            }
            DirectoryRecord rec;
            rec.lba = ru32le(data + pos + 2);
            rec.length = ru32le(data + pos + 10);
            rec.directory = (data[pos + 25] & 0x02) != 0;
            const uint8_t name_len = data[pos + 32];
            if (33u + name_len > rec_len) {
                return false;
            }
            rec.name.assign(reinterpret_cast<const char*>(data + pos + 33), name_len);
            if (!(name_len == 1 && (data[pos + 33] == 0 || data[pos + 33] == 1))) {
                out.push_back(rec);
            }
            pos += rec_len;
        }
        if (remaining <= 2048) {
            break;
        }
        remaining -= 2048;
        sector++;
        if (sector > lba + 64) {
            break;
        }
    }
    return true;
}

std::string serial_from_boot(const std::string& boot) {
    std::string name = boot;
    auto slash = name.find_last_of("\\/");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    auto semi = name.find(';');
    if (semi != std::string::npos) {
        name = name.substr(0, semi);
    }
    name = upper_copy(name);
    auto us = name.find('_');
    auto dot = name.find('.');
    if (us != std::string::npos && dot != std::string::npos && dot > us) {
        return name.substr(0, us) + "-" + name.substr(us + 1, dot - us - 1) + name.substr(dot + 1);
    }
    return name;
}

void classify_region(const std::string& serial, const std::string& volume, DiscReport& report) {
    const std::string id = upper_copy(serial);
    const std::string vol = upper_copy(volume);
    std::string prefix = id;
    auto dash = prefix.find('-');
    if (dash != std::string::npos) {
        prefix = prefix.substr(0, dash);
    }
    if (prefix == "SLUS" || prefix == "SCUS") {
        report.region = "ntsc-u";
        report.language = "english";
    } else if (prefix == "SLPS" || prefix == "SCPS" || prefix == "SLPM" || prefix == "PAPX" || prefix == "SLKA") {
        report.region = "ntsc-j";
        report.language = "japanese";
    } else if (prefix == "SLES" || prefix == "SCES") {
        report.region = "pal";
        if (vol.find("FRAN") != std::string::npos) {
            report.language = "french";
        } else if (vol.find("DEUT") != std::string::npos || vol.find("GERMAN") != std::string::npos) {
            report.language = "german";
        } else if (vol.find("ITAL") != std::string::npos) {
            report.language = "italian";
        } else if (vol.find("ESPAN") != std::string::npos || vol.find("SPAN") != std::string::npos) {
            report.language = "spanish";
        } else {
            report.language = "english";
        }
    } else if (vol.find("ROCKMAN") != std::string::npos) {
        report.region = "unknown";
        report.language = "japanese";
    } else if (vol.find("MEGAMAN") != std::string::npos || vol.find("MEGA MAN") != std::string::npos) {
        report.region = "unknown";
        report.language = "english";
    } else {
        report.region = "unknown";
        report.language = "unknown";
    }
}

std::optional<std::string> read_file_text(Image& image, uint32_t lba, uint32_t length) {
    if (length > 256 * 1024) {
        return std::nullopt;
    }
    std::string text;
    text.reserve(length);
    uint32_t left = length;
    uint32_t sector = lba;
    while (left > 0) {
        uint8_t data[2048];
        if (!image.read_logical(sector, data)) {
            return std::nullopt;
        }
        const uint32_t take = std::min<uint32_t>(left, 2048);
        text.append(reinterpret_cast<const char*>(data), take);
        left -= take;
        sector++;
    }
    return text;
}

int count_seq_notes(const std::string& data) {
    if (data.size() < 16 || data.compare(0, 4, "pQES") != 0) {
        return 0;
    }
    size_t pos = 16;
    int running = 0;
    int notes = 0;
    for (int guard = 0; guard < 100000 && pos < data.size(); guard++) {
        uint32_t delta = 0;
        int shifts = 0;
        while (pos < data.size()) {
            uint8_t byte = static_cast<uint8_t>(data[pos++]);
            delta = (delta << 7) | (byte & 0x7F);
            shifts++;
            if ((byte & 0x80) == 0 || shifts > 4) {
                break;
            }
        }
        (void)delta;
        if (pos >= data.size()) {
            break;
        }
        uint8_t status = static_cast<uint8_t>(data[pos]);
        if (status < 0x80) {
            status = static_cast<uint8_t>(running);
        } else {
            running = status;
            pos++;
        }
        if (status == 0xFF) {
            if (pos >= data.size()) {
                break;
            }
            uint8_t meta = static_cast<uint8_t>(data[pos++]);
            if (meta == 0x2F) {
                break;
            }
            if (pos >= data.size()) {
                break;
            }
            uint8_t len = static_cast<uint8_t>(data[pos++]);
            pos += len;
            continue;
        }
        const int kind = status & 0xF0;
        int data_bytes = 2;
        if (kind == 0xC0 || kind == 0xD0) {
            data_bytes = 1;
        }
        if (kind == 0x90 && pos < data.size() + 2) {
            notes++;
        }
        pos += static_cast<size_t>(data_bytes);
    }
    return notes;
}

struct Session {
    std::mutex mutex;
    std::unique_ptr<Image> image;
    std::unique_ptr<BinCueImage> bincue;
    std::unique_ptr<ChdImage> chd;
    DiscReport report;
    std::vector<XaStream> xa;
    std::vector<CddaTrack> cdda;
    std::vector<SeqFile> seq;
    std::unordered_map<int, int> voice_map;
};

Session& session() {
    static Session state;
    return state;
}

void index_xa(Image& image, const std::vector<DirectoryRecord>& files, std::vector<XaStream>& out) {
    if (!image.has_raw()) {
        return;
    }
    for (const DirectoryRecord& file : files) {
        if (file.directory || file.length == 0) {
            continue;
        }
        const uint32_t sectors = (file.length + 2047u) / 2048u;
        const uint32_t probe = std::min<uint32_t>(sectors, 8);
        for (uint32_t i = 0; i < probe; i++) {
            uint8_t raw[2352];
            if (!image.read_raw(file.lba + i, raw)) {
                break;
            }
            if ((raw[18] & 0x04) == 0) {
                continue;
            }
            XaStream stream;
            stream.name = file.name;
            auto semi = stream.name.find(';');
            if (semi != std::string::npos) {
                stream.name = stream.name.substr(0, semi);
            }
            stream.file = raw[16];
            stream.channel = raw[17];
            stream.start_lba = file.lba;
            stream.sector_count = sectors;
            stream.stereo = (raw[19] & 0x03) == 1;
            stream.rate_hz = (raw[19] & 0x04) ? 18900 : 37800;
            bool exists = false;
            for (const XaStream& have : out) {
                if (have.start_lba == stream.start_lba && have.channel == stream.channel) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                out.push_back(stream);
            }
        }
    }
}

void load_voice_map(const std::filesystem::path& disc_path, const std::vector<XaStream>& streams, std::unordered_map<int, int>& out) {
    const auto map_path = disc_path.parent_path() / "voice_map.json";
    std::ifstream in(map_path);
    if (!in) {
        return;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while (pos < text.size()) {
        auto id_at = text.find("\"id\"", pos);
        if (id_at == std::string::npos) {
            break;
        }
        auto id_colon = text.find(':', id_at);
        auto stream_at = text.find("\"stream\"", id_at);
        if (id_colon == std::string::npos || stream_at == std::string::npos) {
            break;
        }
        int id = std::atoi(text.c_str() + id_colon + 1);
        auto quote = text.find('"', text.find(':', stream_at) + 1);
        auto end = quote == std::string::npos ? std::string::npos : text.find('"', quote + 1);
        if (quote == std::string::npos || end == std::string::npos) {
            break;
        }
        std::string name = upper_copy(text.substr(quote + 1, end - quote - 1));
        for (size_t i = 0; i < streams.size(); i++) {
            std::string stream_name = upper_copy(streams[i].name);
            auto dot = stream_name.find('.');
            std::string stem = dot == std::string::npos ? stream_name : stream_name.substr(0, dot);
            if (stream_name == name || stem == name) {
                out[id] = static_cast<int>(i);
                break;
            }
        }
        pos = end + 1;
    }
}

std::string summary_for(const DiscReport& report) {
    if (!report.path.empty() && !report.loaded) {
        return report.error.empty()
            ? "The disc image could not be read. The N64 soundtrack is used."
            : report.error + " The N64 soundtrack is used.";
    }
    if (!report.loaded) {
        return "No PS1 disc selected. The N64 soundtrack is used.";
    }
    std::string language = report.language;
    if (!language.empty()) {
        language[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(language[0])));
    }
    std::string text = language + " (" + report.region + ")";
    if (!report.serial.empty()) {
        text += " " + report.serial;
    }
    text += ". " + std::to_string(report.xa_streams) + " XA streams, " + std::to_string(report.cdda_tracks) + " CD tracks";
    if (report.seq_files > 0) {
        text += ", " + std::to_string(report.seq_files) + " SEQ files";
    }
    text += ". " + report.format + ".";
    if (!report.has_raw_audio) {
        text += " This image has no raw XA or CD audio, so voices stay on the N64 soundtrack.";
    }
    return text;
}

void fill_report_from_image(Image& image, const std::filesystem::path& path, const std::string& format, DiscReport& report, std::vector<XaStream>& xa, std::vector<SeqFile>& seq) {
    report.loaded = true;
    report.path = path.string();
    report.format = format;
    report.has_raw_audio = image.has_raw();
    uint8_t pvd[2048] = {};
    if (!image.read_logical(16, pvd) || std::memcmp(pvd + 1, "CD001", 5) != 0) {
        report.loaded = false;
        report.error = "The image does not contain an ISO9660 filesystem.";
        return;
    }
    report.volume_id = trim_copy(std::string(reinterpret_cast<const char*>(pvd + 40), 32));
    std::vector<DirectoryRecord> root_records;
    const uint32_t root_lba = ru32le(pvd + 158);
    const uint32_t root_len = ru32le(pvd + 166);
    if (!read_records(image, root_lba, root_len == 0 ? 2048 : root_len, root_records)) {
        report.loaded = false;
        report.error = "The disc directory could not be read.";
        return;
    }
    std::vector<DirectoryRecord> pending = root_records;
    std::vector<DirectoryRecord> files;
    std::vector<uint32_t> seen;
    for (int depth = 0; depth < 8 && !pending.empty() && files.size() < 4096; depth++) {
        std::vector<DirectoryRecord> next;
        for (const DirectoryRecord& rec : pending) {
            if (std::find(seen.begin(), seen.end(), rec.lba) != seen.end()) {
                continue;
            }
            seen.push_back(rec.lba);
            if (rec.directory) {
                std::vector<DirectoryRecord> children;
                if (read_records(image, rec.lba, rec.length == 0 ? 2048 : rec.length, children)) {
                    next.insert(next.end(), children.begin(), children.end());
                }
            } else {
                files.push_back(rec);
            }
        }
        pending.swap(next);
    }
    for (const DirectoryRecord& file : files) {
        std::string name = upper_copy(file.name);
        auto version = name.find(';');
        if (version != std::string::npos) {
            name = name.substr(0, version);
        }
        if (name.rfind("SYSTEM.CNF", 0) == 0) {
            if (auto text = read_file_text(image, file.lba, std::min(file.length, 8192u))) {
                std::string upper = upper_copy(*text);
                auto boot = upper.find("BOOT");
                if (boot != std::string::npos) {
                    auto eq = upper.find('=', boot);
                    if (eq != std::string::npos) {
                        std::string value = trim_copy(upper.substr(eq + 1));
                        auto end = value.find_first_of("\r\n");
                        if (end != std::string::npos) {
                            value = value.substr(0, end);
                        }
                        report.serial = serial_from_boot(value);
                    }
                }
            }
        }
        if (name.find("GAMEOVER") != std::string::npos || name.find("UNUSED") != std::string::npos) {
            report.unused_audio_hits++;
        }
        if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".SEQ") == 0) {
            if (auto text = read_file_text(image, file.lba, std::min(file.length, 256u * 1024u))) {
                SeqFile entry;
                entry.name = file.name;
                entry.note_ons = count_seq_notes(*text);
                seq.push_back(entry);
            }
        }
    }
    classify_region(report.serial, report.volume_id, report);
    index_xa(image, files, xa);
    report.xa_streams = static_cast<int>(xa.size());
    report.seq_files = static_cast<int>(seq.size());
    report.seq_notes = 0;
    for (const SeqFile& entry : seq) {
        report.seq_notes += entry.note_ons;
    }
}

Session& secondary_session() {
    static Session state;
    return state;
}

Session& bonus_session() {
    static Session state;
    return state;
}

Session& session_by_index(int which) {
    if (which == 1) {
        return secondary_session();
    }
    if (which == 2) {
        return bonus_session();
    }
    return session();
}

void load_into(Session& state, const std::filesystem::path& path) {
    std::lock_guard lock(state.mutex);
    state.image.reset();
    state.bincue.reset();
    state.chd.reset();
    state.xa.clear();
    state.cdda.clear();
    state.seq.clear();
    state.voice_map.clear();
    state.report = {};
    state.report.path = path.string();
    if (path.empty()) {
        state.report.summary = summary_for(state.report);
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        state.report.error = "The disc image does not exist.";
        state.report.summary = summary_for(state.report);
        return;
    }
    std::string ext = upper_copy(path.extension().string());
    std::string error;
    if (ext == ".CUE") {
        auto image = std::make_unique<BinCueImage>();
        if (!image->open(path, error)) {
            state.report.error = error;
            state.report.summary = summary_for(state.report);
            return;
        }
        state.cdda = image->cdda();
        state.image = std::move(image);
        fill_report_from_image(*state.image, path, "BIN/CUE", state.report, state.xa, state.seq);
    } else if (ext == ".CHD") {
        auto image = std::make_unique<ChdImage>();
        if (!image->open(path, error)) {
            state.report.error = error;
            state.report.summary = summary_for(state.report);
            return;
        }
        state.cdda = image->cdda();
        state.image = std::move(image);
        fill_report_from_image(*state.image, path, "CHD", state.report, state.xa, state.seq);
    } else if (ext == ".ISO" || ext == ".BIN") {
        auto image = std::make_unique<IsoImage>();
        if (!image->open(path, error)) {
            state.report.error = error.empty() ? "Could not open the image." : error;
            state.report.summary = summary_for(state.report);
            return;
        }
        state.image = std::move(image);
        fill_report_from_image(*state.image, path, ext == ".BIN" ? "BIN" : "ISO", state.report, state.xa, state.seq);
        if (state.report.loaded && ext == ".BIN") {
            state.report.error = "A bare BIN has no CUE, so track layout is guessed as 2048-byte sectors. Prefer a CUE sheet.";
        }
    } else {
        state.report.error = "Use a .cue, .chd, .iso, or .bin disc image.";
        state.report.summary = summary_for(state.report);
        return;
    }
    state.report.cdda_tracks = static_cast<int>(state.cdda.size());
    if (state.report.loaded) {
        load_voice_map(path, state.xa, state.voice_map);
    }
    state.report.summary = summary_for(state.report);
}

} // namespace

void load(const std::filesystem::path& path) {
    load_into(session(), path);
}

void load_secondary(const std::filesystem::path& path) {
    load_into(secondary_session(), path);
}

void load_bonus(const std::filesystem::path& path) {
    load_into(bonus_session(), path);
}

DiscReport copy_report() {
    std::lock_guard lock(session().mutex);
    return session().report;
}

DiscReport copy_report_of(int which) {
    Session& state = session_by_index(which);
    std::lock_guard lock(state.mutex);
    return state.report;
}

std::mutex& mutex() {
    return session().mutex;
}

bool read_raw(uint32_t lba, uint8_t* dst2352) {
    Session& state = session();
    if (!state.image) {
        return false;
    }
    return state.image->read_raw(lba, dst2352);
}

bool read_raw_of(int which, uint32_t lba, uint8_t* dst2352) {
    Session& state = session_by_index(which);
    std::lock_guard lock(state.mutex);
    if (!state.image) {
        return false;
    }
    return state.image->read_raw(lba, dst2352);
}

bool read_cdda_sector(int track_index, uint32_t sector_in_track, uint8_t* dst2352) {
    Session& state = session();
    if (!state.image) {
        return false;
    }
    return state.image->read_cdda(track_index, sector_in_track, dst2352);
}

bool read_cdda_of(int which, int track_index, uint32_t sector_in_track, uint8_t* dst2352) {
    Session& state = session_by_index(which);
    std::lock_guard lock(state.mutex);
    if (!state.image) {
        return false;
    }
    return state.image->read_cdda(track_index, sector_in_track, dst2352);
}

std::vector<XaStream> copy_xa(int which) {
    Session& state = session_by_index(which);
    std::lock_guard lock(state.mutex);
    return state.xa;
}

std::vector<CddaTrack> copy_cdda(int which) {
    Session& state = session_by_index(which);
    std::lock_guard lock(state.mutex);
    return state.cdda;
}

const std::vector<XaStream>& xa_streams() {
    return session().xa;
}

const std::vector<CddaTrack>& cdda_tracks() {
    return session().cdda;
}

int voice_stream_index(int voice_id) {
    auto& state = session();
    auto it = state.voice_map.find(voice_id);
    if (it != state.voice_map.end()) {
        return it->second;
    }
    if (voice_id >= 0 && voice_id < static_cast<int>(state.xa.size())) {
        return voice_id;
    }
    return -1;
}

} // namespace remaster::disc
