#ifndef REMASTER_DISC_INTERNAL_HPP
#define REMASTER_DISC_INTERNAL_HPP

#include "remaster.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace remaster::disc {

struct XaStream {
    std::string name;
    uint8_t file = 0;
    uint8_t channel = 0;
    uint32_t start_lba = 0;
    uint32_t sector_count = 0;
    int rate_hz = 37800;
    bool stereo = true;
};

struct CddaTrack {
    int number = 0;
    uint32_t file_offset_sectors = 0;
    uint32_t sector_count = 0;
    std::string file;
    int sector_bytes = 2352;
};

struct SeqFile {
    std::string name;
    int note_ons = 0;
};

bool read_raw(uint32_t lba, uint8_t* dst2352);
bool read_cdda_sector(int track_index, uint32_t sector_in_track, uint8_t* dst2352);
const std::vector<XaStream>& xa_streams();
const std::vector<CddaTrack>& cdda_tracks();
int voice_stream_index(int voice_id);
std::mutex& mutex();
void load(const std::filesystem::path& path);
DiscReport copy_report();

} // namespace remaster::disc

#endif
