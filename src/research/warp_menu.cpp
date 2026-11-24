#include "research.hpp"
#include "recomp.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kPad = 0x801BC380u;
constexpr uint32_t kMode = 0x801BC3E8u;
constexpr uint32_t kSub = 0x801BC3E9u;
constexpr uint32_t kArea = 0x801BC438u;
constexpr uint32_t kRoom = 0x801BC439u;
constexpr uint32_t kLoaded = 0x801BC450u;
constexpr uint32_t kCutLock = 0x801BC5B0u;
constexpr uint32_t kFade = 0x801FF39Au;
constexpr uint32_t kMessage = 0x80203FF0u;
constexpr uint32_t kPlayer = 0x802049B0u;
constexpr uint32_t kWarpX = 0x802047C0u;
constexpr uint32_t kWarpState = 0x802047CCu;
constexpr uint32_t kIntX = 0x80162830u;
constexpr uint32_t kIntY = 0x80162834u;
constexpr uint32_t kIntZ = 0x80162838u;

constexpr uint16_t kA = 0x8000;
constexpr uint16_t kB = 0x4000;
constexpr uint16_t kL = 0x0020;
constexpr uint16_t kDUp = 0x0800;
constexpr uint16_t kDDown = 0x0400;
constexpr uint16_t kDLeft = 0x0200;
constexpr uint16_t kDRight = 0x0100;
constexpr uint16_t kHide = kA | kB | kL | kDUp | kDDown | kDLeft | kDRight;

struct Target {
    const char* name;
    int area;
    int room;
    int x;
    int y;
    int z;
    int yaw;
    int live;
};

const Target kTargets[] = {
#include "warp_targets.inc"
};

struct Bookmark {
    std::string name;
    int area = 0;
    int room = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int yaw = 0;
};

int64_t guest_at(uint32_t address) {
    return static_cast<int64_t>(static_cast<int32_t>(address));
}

struct Spot {
    int area = 0;
    int room = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int yaw = 0;
    bool valid = false;
};

bool g_open = false;
bool g_warped = false;
int g_page = 0;
int g_index = 0;
int g_frame = 0;
int g_room_frame = -1000;
int g_last_area = -1;
int g_last_room = -1;
int g_last_chest = -1;
int g_down_edges = 0;
bool g_l_down = false;
std::chrono::steady_clock::time_point g_l_since{};
std::chrono::steady_clock::time_point g_pending_since{};
std::chrono::steady_clock::time_point g_menu_log{};
uint16_t g_prev_pad = 0;
bool g_have_pad = false;
Spot g_pending{};
bool g_have_pending = false;
Spot g_return{};
std::vector<Bookmark> g_bookmarks;
std::vector<std::vector<int>> g_pages;
bool g_ready = false;
std::string g_menu_line;
std::string g_busy;

std::filesystem::path out_dir() {
    if (const char* env = std::getenv("MM64R_OUT")) {
        if (env[0] != '\0') {
            return env;
        }
    }
    return "research/out";
}

void load_bookmarks() {
    g_bookmarks.clear();
    std::ifstream in(out_dir() / "warp_bookmarks.csv");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        Bookmark mark;
        unsigned area = 0;
        if (std::sscanf(line.c_str(), "%i,%d,%d,%d,%d,%d", &area, &mark.room, &mark.x, &mark.y, &mark.z, &mark.yaw) != 6) {
            continue;
        }
        mark.area = static_cast<int>(area);
        char label[64];
        std::snprintf(label, sizeof(label), "bookmark 0x%02X [%d] room %d", mark.area, mark.area, mark.room);
        mark.name = label;
        g_bookmarks.push_back(mark);
    }
}

void build_pages() {
    g_pages.clear();
    std::vector<int> named;
    for (int i = 0; i < 14; ++i) {
        named.push_back(i);
    }
    g_pages.push_back(named);
    std::vector<int> page;
    int area = -1;
    for (int i = 14; i < static_cast<int>(sizeof(kTargets) / sizeof(kTargets[0])); ++i) {
        if (area >= 0 && kTargets[i].area != area && !page.empty()) {
            g_pages.push_back(page);
            page.clear();
        }
        area = kTargets[i].area;
        page.push_back(i);
    }
    if (!page.empty()) {
        g_pages.push_back(page);
    }
    std::vector<int> marks;
    for (int i = 0; i < static_cast<int>(g_bookmarks.size()); ++i) {
        marks.push_back(100000 + i);
    }
    if (!marks.empty()) {
        g_pages.push_back(marks);
    }
    if (g_page >= static_cast<int>(g_pages.size())) {
        g_page = 0;
    }
    if (g_index >= static_cast<int>(g_pages[g_page].size())) {
        g_index = 0;
    }
}

void ensure_ready() {
    if (g_ready) {
        return;
    }
    g_ready = true;
    load_bookmarks();
    build_pages();
}

const char* current_name() {
    static std::string label;
    if (!g_open || g_pages.empty()) {
        return "";
    }
    const int id = g_pages[g_page][g_index];
    if (id >= 100000) {
        label = g_bookmarks[id - 100000].name;
    } else {
        label = kTargets[id].name;
    }
    return label.c_str();
}

void refresh_menu_line() {
    if (!g_open) {
        g_menu_line.clear();
        return;
    }
    char line[192];
    std::snprintf(line, sizeof(line), "WARP %d/%d  %s%s", g_page + 1, static_cast<int>(g_pages.size()),
        current_name(), g_busy.empty() ? "" : g_busy.c_str());
    g_menu_line = line;
}

bool target_of(int id, Spot& spot) {
    if (id >= 100000) {
        const Bookmark& mark = g_bookmarks[id - 100000];
        spot.area = mark.area;
        spot.room = mark.room;
        spot.x = mark.x;
        spot.y = mark.y;
        spot.z = mark.z;
        spot.yaw = mark.yaw;
        spot.valid = true;
        return true;
    }
    const Target& target = kTargets[id];
    spot.area = target.area;
    spot.room = target.room;
    spot.x = target.x;
    spot.y = target.y;
    spot.z = target.z;
    spot.yaw = target.yaw;
    spot.valid = target.live != 0;
    return spot.valid;
}

void log_state(uint8_t* rdram, const char* why) {
    const int area = MEM_BU(0, guest_at(kArea));
    const int room = MEM_BU(0, guest_at(kRoom));
    const int loaded = MEM_HU(0, guest_at(kLoaded));
    const int x = MEM_W(0, guest_at(kIntX));
    const int y = MEM_W(0, guest_at(kIntY));
    const int z = MEM_W(0, guest_at(kIntZ));
    const int mode = MEM_BU(0, guest_at(kMode));
    const int sub = MEM_BU(0, guest_at(kSub));
    char line[320];
    std::snprintf(line, sizeof(line),
        "%s area=%02X room=%d loaded=%u xyz=%d,%d,%d warp=%d,%d,%d,%d yaw=%u st=%d area2=%u room2=%u fade_in=%u fade_out=%u mode=%d sub=%d chest=%d",
        why, area, room, loaded, x, y, z,
        MEM_H(0, guest_at(kWarpX)), MEM_H(2, guest_at(kWarpX)), MEM_H(4, guest_at(kWarpX)), MEM_H(6, guest_at(kWarpX)),
        MEM_HU(0xA, guest_at(kWarpX)), MEM_B(0, guest_at(kWarpState)),
        MEM_BU(2, guest_at(kWarpState)), MEM_BU(3, guest_at(kWarpState)),
        MEM_BU(4, guest_at(kWarpState)), MEM_BU(5, guest_at(kWarpState)),
        mode, sub, g_last_chest);
    mm64r::log_line(line);
}

bool gates(uint8_t* rdram) {
    if (MEM_BU(0, guest_at(kMode)) != 3 || MEM_BU(0, guest_at(kSub)) != 2) {
        return false;
    }
    if (MEM_B(0, guest_at(kWarpState)) != 0) {
        return false;
    }
    if (MEM_H(0, guest_at(kFade)) != 0) {
        return false;
    }
    if ((static_cast<uint32_t>(MEM_W(0, guest_at(kMessage))) & 0x0F000000u) != 0) {
        return false;
    }
    if (MEM_BU(0, guest_at(kCutLock)) != 0) {
        return false;
    }
    const int64_t player = guest_at(kPlayer);
    if (MEM_H(0x6E, player) < 0 || MEM_BU(0xBD, player) != 0 || MEM_BU(0x08, player) != 1) {
        return false;
    }
    return true;
}

void write_warp(uint8_t* rdram, const Spot& spot) {
    const int64_t base = guest_at(kWarpX);
    MEM_H(0, base) = static_cast<int16_t>(spot.x);
    MEM_H(2, base) = static_cast<int16_t>(spot.y);
    MEM_H(4, base) = static_cast<int16_t>(spot.z);
    MEM_H(6, base) = static_cast<int16_t>(spot.yaw);
    MEM_HU(0xA, base) = static_cast<uint16_t>(spot.yaw);
    MEM_B(0, guest_at(0x802047CDu)) = 0;
    MEM_B(0, guest_at(0x802047CEu)) = static_cast<int8_t>(spot.area);
    MEM_B(0, guest_at(0x802047CFu)) = static_cast<int8_t>(spot.room);
    MEM_B(0, guest_at(0x802047D0u)) = 2;
    MEM_B(0, guest_at(0x802047D1u)) = static_cast<int8_t>(0x15);
    MEM_B(0, guest_at(kWarpState)) = 2;
    g_warped = true;
    g_have_pending = false;
    g_busy.clear();
    log_state(rdram, "warp");
}

void queue_spot(const Spot& spot) {
    if (!spot.valid) {
        g_busy = "  no arrival";
        refresh_menu_line();
        mm64r::log_line("warp rejected, no arrival for this target");
        return;
    }
    g_pending = spot;
    g_have_pending = true;
    g_pending_since = std::chrono::steady_clock::now();
    g_busy = "  busy";
    refresh_menu_line();
}

void queue_current() {
    const int id = g_pages[g_page][g_index];
    Spot spot;
    if (!target_of(id, spot)) {
        queue_spot(spot);
        return;
    }
    queue_spot(spot);
}

Spot capture_spot(uint8_t* rdram) {
    Spot spot;
    const int64_t player = guest_at(kPlayer);
    spot.area = MEM_BU(0, guest_at(kArea));
    spot.room = MEM_BU(0, guest_at(kRoom));
    spot.x = MEM_H(0x14, player);
    spot.y = MEM_H(0x16, player);
    spot.z = MEM_H(0x18, player);
    spot.yaw = MEM_H(0x56, player);
    spot.valid = true;
    return spot;
}

void save_bookmark(uint8_t* rdram) {
    const Spot spot = capture_spot(rdram);
    std::error_code ec;
    std::filesystem::create_directories(out_dir(), ec);
    std::ofstream out(out_dir() / "warp_bookmarks.csv", std::ios::app);
    out << spot.area << "," << spot.room << "," << spot.x << "," << spot.y << "," << spot.z << "," << spot.yaw << "\n";
    Bookmark mark;
    mark.area = spot.area;
    mark.room = spot.room;
    mark.x = spot.x;
    mark.y = spot.y;
    mark.z = spot.z;
    mark.yaw = spot.yaw;
    char label[64];
    std::snprintf(label, sizeof(label), "bookmark 0x%02X [%d] room %d", mark.area, mark.area, mark.room);
    mark.name = label;
    g_bookmarks.push_back(mark);
    build_pages();
    g_page = static_cast<int>(g_pages.size()) - 1;
    g_index = static_cast<int>(g_pages[g_page].size()) - 1;
    mm64r::log_line("bookmark saved");
    log_state(rdram, "bookmark");
}

void open_menu(uint8_t* rdram) {
    ensure_ready();
    g_open = true;
    g_return = capture_spot(rdram);
    g_busy.clear();
    refresh_menu_line();
    log_state(rdram, "menu");
}

void move_selection(int delta) {
    if (g_pages.empty()) {
        return;
    }
    const int count = static_cast<int>(g_pages[g_page].size());
    g_index = (g_index + delta + count) % count;
    refresh_menu_line();
}

void move_page(int delta) {
    if (g_pages.empty()) {
        return;
    }
    const int count = static_cast<int>(g_pages.size());
    g_page = (g_page + delta + count) % count;
    g_index = 0;
    refresh_menu_line();
}

void on_pad(uint8_t* rdram) {
    ensure_ready();
    const int64_t pad = guest_at(kPad);
    const uint16_t raw = MEM_HU(0, pad);
    const uint16_t edge = g_have_pad ? static_cast<uint16_t>(raw & ~g_prev_pad) : raw;
    g_prev_pad = raw;
    g_have_pad = true;
    const auto now = std::chrono::steady_clock::now();
    if ((raw & kL) != 0) {
        if (!g_l_down) {
            g_l_since = now;
        }
        g_l_down = true;
    } else {
        g_l_down = false;
        g_down_edges = 0;
    }
    const bool l_ready = g_l_down && now - g_l_since >= std::chrono::milliseconds(500);
    bool just_opened = false;
    if (!g_open) {
        if (l_ready && (edge & kDUp) != 0) {
            open_menu(rdram);
            just_opened = true;
        } else if (g_l_down && (edge & kDDown) != 0) {
            g_down_edges += 1;
            if (g_down_edges >= 2) {
                g_down_edges = 0;
                save_bookmark(rdram);
            }
        }
    }
    if (g_open) {
        if (!just_opened) {
        if ((edge & kDUp) != 0) {
            move_selection(-1);
        }
        if ((edge & kDDown) != 0) {
            if ((raw & kL) != 0 && g_return.valid) {
                queue_spot(g_return);
            } else {
                move_selection(1);
            }
        }
        if ((edge & kDLeft) != 0) {
            move_page(-1);
        }
        if ((edge & kDRight) != 0) {
            move_page(1);
        }
        if ((edge & kA) != 0) {
            queue_current();
        }
        if ((edge & kB) != 0) {
            g_open = false;
            g_busy.clear();
            g_menu_line.clear();
        }
        }
        MEM_HU(0, pad) = static_cast<uint16_t>(raw & ~kHide);
    }
    if (g_open) {
        if (g_menu_log.time_since_epoch().count() == 0 || now - g_menu_log >= std::chrono::seconds(1)) {
            g_menu_log = now;
            log_state(rdram, "menu");
        }
    }
}

void on_frame(uint8_t* rdram) {
    ensure_ready();
    g_frame += 1;
    const int area = MEM_BU(0, guest_at(kArea));
    const int room = MEM_BU(0, guest_at(kRoom));
    if (area != g_last_area || room != g_last_room) {
        g_last_area = area;
        g_last_room = room;
        g_room_frame = g_frame;
        log_state(rdram, "room");
    }
    if (!g_have_pending) {
        return;
    }
    if (std::chrono::steady_clock::now() - g_pending_since > std::chrono::seconds(10)) {
        g_have_pending = false;
        g_busy = "  timed out";
        refresh_menu_line();
        mm64r::log_line("warp timed out, gates stayed closed");
        return;
    }
    if (gates(rdram)) {
        write_warp(rdram, g_pending);
        refresh_menu_line();
    }
}

void on_flag(uint8_t* rdram, int id, const char* verb) {
    if (id < 0x400 || id > 0x4FF) {
        return;
    }
    g_last_chest = id;
    if (verb[0] == 'T' && g_frame - g_room_frame >= 60) {
        return;
    }
    char line[64];
    std::snprintf(line, sizeof(line), "chest %s %03X", verb, id);
    mm64r::log_line(line);
    log_state(rdram, "chest");
}

} // namespace

namespace mm64r {

bool warp_blocks_save() {
    return g_warped;
}

const char* warp_banner() {
    return g_warped ? "DEBUG WARP: do not save" : "";
}

const char* warp_menu_line() {
    return g_menu_line.c_str();
}

} // namespace mm64r

extern "C" void remaster_warp_pad(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    on_pad(rdram);
}

extern "C" void remaster_warp_frame(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    on_frame(rdram);
}

extern "C" void remaster_warp_flag_test(uint8_t* rdram, recomp_context* ctx) {
    on_flag(rdram, static_cast<int>(ctx->r4), "TEST");
}

extern "C" void remaster_warp_flag_set(uint8_t* rdram, recomp_context* ctx) {
    on_flag(rdram, static_cast<int>(ctx->r4), "SET");
}
