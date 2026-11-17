#ifndef REMASTER_UI_H
#define REMASTER_UI_H

#include <cstdint>

namespace remaster_ui {
    void init();
    void show_title_overlay();
    void on_game_buttons(uint16_t buttons);
    void on_frame(bool game_started);
}

#endif
