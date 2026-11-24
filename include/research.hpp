#ifndef MM64_RESEARCH_HPP
#define MM64_RESEARCH_HPP

#include <cstdint>
#include <string>

struct SDL_Window;

namespace mm64r {

void note_rdram(uint8_t* rdram);
void note_window(SDL_Window* window);
void on_vi();
bool script_active();
void apply_input(uint16_t* buttons, float* x, float* y);
bool wants_autostart();
void log_line(const std::string& line);
bool warp_blocks_save();
const char* warp_banner();
const char* warp_menu_line();

} // namespace mm64r

#endif
