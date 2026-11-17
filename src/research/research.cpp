#include "research.hpp"
#include "research_script.hpp"

#include "recomp.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#include <SDL.h>
#include <SDL_syswm.h>
#ifdef __linux__
#undef None
#undef Status
#undef LockMask
#undef ControlMask
#undef Success
#undef Always
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace mm64r {

std::mutex g_calls_mu;
std::unordered_set<std::string> g_calls;
std::atomic<bool> g_recording{false};

namespace {

constexpr size_t kRdramBytes = 8 * 1024 * 1024;

uint8_t* g_rdram = nullptr;
SDL_Window* g_window = nullptr;
Script g_script;
std::mutex g_log_mu;
std::filesystem::path g_out = "research/out";
bool g_loaded = false;
int g_frame = 0;

struct Watch {
    std::string label;
    uint32_t address = 0;
    int width = 1;
    uint32_t last = 0;
    bool seen = false;
};

std::vector<Watch> g_watches;

bool guest_ok(uint32_t address, int width) {
    if (address < 0x80000000u) {
        return false;
    }
    const uint64_t end = static_cast<uint64_t>(address) + static_cast<uint64_t>(width);
    return end <= 0x80800000ull;
}

bool read_mem(int width, uint32_t address, uint32_t& value) {
    if (g_rdram == nullptr || !guest_ok(address, width)) {
        return false;
    }
    uint8_t* rdram = g_rdram;
    /* Guest addresses are signed MIPS words. A uint32_t 0x80xxxxxx sign-extends;
     * passing it straight in makes the MEM macros land about 4 GB past RDRAM. */
    const int64_t sa = static_cast<int64_t>(static_cast<int32_t>(address));
    if (width == 1) {
        value = MEM_BU(0, sa);
    } else if (width == 2) {
        value = MEM_HU(0, sa);
    } else {
        value = static_cast<uint32_t>(MEM_W(0, sa));
    }
    return true;
}

bool write_mem(int width, uint32_t address, uint32_t value) {
    if (g_rdram == nullptr || !guest_ok(address, width)) {
        return false;
    }
    uint8_t* rdram = g_rdram;
    const int64_t sa = static_cast<int64_t>(static_cast<int32_t>(address));
    if (width == 1) {
        MEM_BU(0, sa) = static_cast<uint8_t>(value);
    } else if (width == 2) {
        MEM_HU(0, sa) = static_cast<uint16_t>(value);
    } else {
        MEM_W(0, sa) = static_cast<int32_t>(value);
    }
    return true;
}

void log_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_log_mu);
    std::error_code ec;
    std::filesystem::create_directories(g_out, ec);
    std::ofstream out(g_out / "session.log", std::ios::app);
    out << g_frame << " " << line << "\n";
}

std::filesystem::path snap_path(const std::string& name) {
    return g_out / ("snap_" + name + ".bin");
}

bool read_snap(const std::string& name, std::vector<uint8_t>& bytes) {
    std::ifstream in(snap_path(name), std::ios::binary);
    if (!in) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return bytes.size() == kRdramBytes;
}

void write_diff(const std::string& name_a, const std::string& name_b, uint32_t start, uint32_t length) {
    std::vector<uint8_t> a;
    std::vector<uint8_t> b;
    if (!read_snap(name_a, a) || !read_snap(name_b, b)) {
        log_line("diff missing snapshot " + name_a + " or " + name_b);
        return;
    }
    if (start < 0x80000000u || length == 0) {
        start = 0x80000000u;
        length = static_cast<uint32_t>(kRdramBytes);
    }
    const uint32_t last = start + std::min(length, static_cast<uint32_t>(0x80800000u - start));
    char range_tag[24] = "";
    if (start != 0x80000000u || length != static_cast<uint32_t>(kRdramBytes)) {
        std::snprintf(range_tag, sizeof(range_tag), "_%08X", start);
    }
    std::ofstream out(g_out / ("diff_" + name_a + "_" + name_b + range_tag + ".txt"));
    int ranges = 0;
    uint32_t addr = start;
    while (addr < last && ranges < 500) {
        const size_t index = static_cast<size_t>((addr - 0x80000000u) ^ 3u);
        if (a[index] == b[index]) {
            addr++;
            continue;
        }
        const uint32_t range_start = addr;
        std::string bytes;
        while (addr < last && ranges < 500) {
            const size_t cursor = static_cast<size_t>((addr - 0x80000000u) ^ 3u);
            if (a[cursor] == b[cursor]) {
                break;
            }
            if (!bytes.empty()) bytes.push_back(' ');
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02X", b[cursor]);
            bytes += hex;
            addr++;
            if (bytes.size() > 96) {
                break;
            }
        }
        out << std::hex << "0x" << range_start << std::dec << " " << bytes << "\n";
        ranges++;
    }
    log_line("diff " + name_a + " " + name_b + " ranges " + std::to_string(ranges));
}

void write_ppm(const std::filesystem::path& path, const uint8_t* rgb, int width, int height) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb), static_cast<std::streamsize>(width * height * 3));
}

void screenshot_framebuffer(const std::string& name) {
    const uint32_t fb = g_script.framebuffer();
    if (fb == 0 || g_rdram == nullptr) {
        log_line("screenshot " + name + " has no framebuffer address");
        return;
    }
    constexpr int kWidth = 320;
    constexpr int kHeight = 240;
    std::vector<uint8_t> rgb(static_cast<size_t>(kWidth * kHeight * 3));
    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x++) {
            uint32_t pixel = 0;
            if (!read_mem(2, fb + static_cast<uint32_t>((y * kWidth + x) * 2), pixel)) {
                log_line("screenshot " + name + " left RDRAM");
                return;
            }
            const size_t at = static_cast<size_t>((y * kWidth + x) * 3);
            rgb[at] = static_cast<uint8_t>(((pixel >> 11) & 31) * 255 / 31);
            rgb[at + 1] = static_cast<uint8_t>(((pixel >> 6) & 31) * 255 / 31);
            rgb[at + 2] = static_cast<uint8_t>(((pixel >> 1) & 31) * 255 / 31);
        }
    }
    write_ppm(g_out / (name + "_fb.ppm"), rgb.data(), kWidth, kHeight);
    log_line("screenshot framebuffer " + name);
}

void screenshot_window(const std::string& name) {
    if (g_window == nullptr) {
        log_line("screenshot " + name + " has no window");
        return;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(g_window, &width, &height);
    if (width < 1 || height < 1 || width > 3840 || height > 2160) {
        log_line("screenshot " + name + " skipped an unusual window size");
        return;
    }
#if defined(__linux__)
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(g_window, &info) != SDL_TRUE) {
        log_line("screenshot " + name + " could not read the window");
        return;
    }
    XImage* image = XGetImage(info.info.x11.display, info.info.x11.window, 0, 0, static_cast<unsigned>(width), static_cast<unsigned>(height), AllPlanes, ZPixmap);
    if (image == nullptr) {
        log_line("screenshot " + name + " XGetImage failed");
        return;
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(width * height * 3));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const unsigned long pixel = XGetPixel(image, x, y);
            const size_t at = static_cast<size_t>((y * width + x) * 3);
            rgb[at] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
            rgb[at + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
            rgb[at + 2] = static_cast<uint8_t>(pixel & 0xFF);
        }
    }
    XDestroyImage(image);
    write_ppm(g_out / (name + "_window.ppm"), rgb.data(), width, height);
    log_line("screenshot window " + name);
#elif defined(_WIN32)
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(g_window, &info) != SDL_TRUE) {
        log_line("screenshot " + name + " could not read the window");
        return;
    }
    HDC window_dc = GetDC(info.info.win.window);
    HDC memory_dc = CreateCompatibleDC(window_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(window_dc, width, height);
    SelectObject(memory_dc, bitmap);
    BitBlt(memory_dc, 0, 0, width, height, window_dc, 0, 0, SRCCOPY);
    BITMAPINFO header{};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = width;
    header.bmiHeader.biHeight = -height;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 24;
    header.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> bgr(static_cast<size_t>(((width * 3 + 3) & ~3) * height));
    GetDIBits(memory_dc, bitmap, 0, static_cast<UINT>(height), bgr.data(), &header, DIB_RGB_COLORS);
    std::vector<uint8_t> rgb(static_cast<size_t>(width * height * 3));
    const int stride = (width * 3 + 3) & ~3;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const size_t from = static_cast<size_t>(y * stride + x * 3);
            const size_t to = static_cast<size_t>((y * width + x) * 3);
            rgb[to] = bgr[from + 2];
            rgb[to + 1] = bgr[from + 1];
            rgb[to + 2] = bgr[from];
        }
    }
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(info.info.win.window, window_dc);
    write_ppm(g_out / (name + "_window.ppm"), rgb.data(), width, height);
    log_line("screenshot window " + name);
#else
    log_line("screenshot " + name + " has no window capture on this OS");
#endif
}

void ensure_loaded() {
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    if (const char* out = std::getenv("MM64R_OUT")) {
        g_out = out;
    }
    std::error_code ec;
    std::filesystem::create_directories(g_out, ec);
    const char* path = std::getenv("MM64R_SCRIPT");
    if (path == nullptr || path[0] == '\0') {
        log_line("research harness on, no MM64R_SCRIPT");
        return;
    }
    std::ifstream in(path);
    if (!in) {
        log_line(std::string("could not open script ") + path);
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    if (!g_script.load(text, error)) {
        log_line("script error: " + error);
        return;
    }
    log_line(std::string("loaded ") + path);
    if (std::getenv("MM64R_AUTOSTART") != nullptr) {
        log_line("autostart from the environment");
    }
}

void handle(const Action& action) {
    switch (action.kind) {
    case Action::Kind::Log:
        log_line(action.text);
        break;
    case Action::Kind::Fail:
        log_line("FAIL " + action.text);
        break;
    case Action::Kind::Skip:
        log_line("SKIP " + action.text);
        break;
    case Action::Kind::Done:
        log_line("SCRIPT DONE");
        break;
    case Action::Kind::Echo: {
        uint32_t value = 0;
        if (!read_mem(action.width, action.address, value)) {
            log_line("echo " + action.text + " unavailable");
        } else {
            log_line("echo " + action.text + " " + std::to_string(value));
        }
        break;
    }
    case Action::Kind::Snapshot: {
        if (g_rdram == nullptr) {
            log_line("snapshot " + action.text + " before RDRAM was seen");
            break;
        }
        std::ofstream out(snap_path(action.text), std::ios::binary);
        out.write(reinterpret_cast<const char*>(g_rdram), static_cast<std::streamsize>(kRdramBytes));
        log_line("snapshot " + action.text);
        break;
    }
    case Action::Kind::Diff:
        write_diff(action.text, action.text2, 0, 0);
        break;
    case Action::Kind::DiffRange:
        write_diff(action.text, action.text2, action.address, action.length);
        break;
    case Action::Kind::Watch:
        g_watches.push_back(Watch{action.text, action.address, action.width, 0, false});
        log_line("watch " + action.text);
        break;
    case Action::Kind::Unwatch:
        g_watches.erase(std::remove_if(g_watches.begin(), g_watches.end(), [&](const Watch& watch) {
            return watch.label == action.text;
        }), g_watches.end());
        break;
    case Action::Kind::CallsStart:
        {
            std::lock_guard<std::mutex> lock(g_calls_mu);
            g_calls.clear();
        }
        g_recording.store(true);
        log_line("calls start");
        break;
    case Action::Kind::CallsStop: {
        g_recording.store(false);
        std::vector<std::string> names;
        {
            std::lock_guard<std::mutex> lock(g_calls_mu);
            names.assign(g_calls.begin(), g_calls.end());
        }
        std::sort(names.begin(), names.end());
        std::ofstream out(g_out / ("calls_" + action.text + ".txt"));
        for (const std::string& name : names) {
            out << name << "\n";
        }
        log_line("calls " + action.text + " " + std::to_string(names.size()));
        if (names.empty()) {
            log_line("no calls recorded; rebuild with trace_mode = true in us.rev1.toml and -DMM64R_RESEARCH=ON");
        }
        break;
    }
    case Action::Kind::CallsDiff: {
        std::ifstream left(g_out / ("calls_" + action.text + ".txt"));
        std::ifstream right(g_out / ("calls_" + action.text2 + ".txt"));
        std::unordered_set<std::string> a;
        std::unordered_set<std::string> b;
        std::string line;
        while (std::getline(left, line)) a.insert(line);
        while (std::getline(right, line)) b.insert(line);
        std::ofstream out(g_out / ("calls_diff_" + action.text + "_" + action.text2 + ".txt"));
        for (const std::string& name : b) {
            if (!a.contains(name)) out << "only_b " << name << "\n";
        }
        for (const std::string& name : a) {
            if (!b.contains(name)) out << "only_a " << name << "\n";
        }
        log_line("calls diff " + action.text + " " + action.text2);
        break;
    }
    case Action::Kind::Screenshot:
        screenshot_window(action.text);
        screenshot_framebuffer(action.text);
        break;
    case Action::Kind::SetFb:
        log_line("framebuffer set");
        break;
    case Action::Kind::Poke:
        if (write_mem(action.width, action.address, action.value)) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "poke u%d 0x%08X = %u", action.width * 8, action.address, action.value);
            log_line(buf);
        } else {
            log_line("poke unavailable");
        }
        break;
    case Action::Kind::Autostart:
        log_line("script requested autostart");
        break;
    }
}

void sample_watches() {
    for (Watch& watch : g_watches) {
        uint32_t value = 0;
        if (!read_mem(watch.width, watch.address, value)) {
            continue;
        }
        if (!watch.seen) {
            watch.seen = true;
            watch.last = value;
            log_line("watch " + watch.label + " initial " + std::to_string(value));
            continue;
        }
        if (value != watch.last) {
            log_line("watch " + watch.label + " " + std::to_string(watch.last) + " -> " + std::to_string(value));
            watch.last = value;
        }
    }
}

} // namespace

void note_rdram(uint8_t* rdram) {
    if (g_rdram == nullptr && rdram != nullptr) {
        g_rdram = rdram;
        log_line("rdram host captured");
    }
}

void note_window(SDL_Window* window) {
    g_window = window;
}

bool script_active() {
    return g_script.active();
}

void apply_input(uint16_t* buttons, float* x, float* y) {
    const InputOverride state = g_script.input();
    if (!state.active) {
        return;
    }
    *buttons = state.buttons;
    *x = state.x;
    *y = state.y;
}

bool wants_autostart() {
    return std::getenv("MM64R_AUTOSTART") != nullptr || g_script.autostart();
}

void on_vi() {
    ensure_loaded();
    g_frame++;
    if (!g_script.active()) {
        sample_watches();
        return;
    }
    const auto actions = g_script.on_frame(read_mem);
    for (const Action& action : actions) {
        handle(action);
    }
    sample_watches();
}

} // namespace mm64r

extern "C" void mm64r_trace_entry(const char* name) {
    if (!mm64r::g_recording.load(std::memory_order_relaxed) || name == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mm64r::g_calls_mu);
    mm64r::g_calls.emplace(name);
}
