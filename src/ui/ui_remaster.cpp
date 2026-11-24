#include "remaster_ui.h"

#include "addons.hpp"
#include "recomp_ui.h"
#include "zelda_support.h"
#ifdef MM64R_RESEARCH
#include "research.hpp"
#endif

#include "RmlUi/Core.h"

#include <chrono>
#include <string>

namespace {

recompui::ContextId title_context = recompui::ContextId::null();
recompui::ContextId hud_context = recompui::ContextId::null();
bool hud_ready = false;
bool hud_shown = false;
bool title_ready = false;
bool title_shown = false;
std::chrono::steady_clock::time_point title_shown_at{};

void hide_title() {
    if (!title_shown || title_context == recompui::ContextId::null()) {
        title_shown = false;
        return;
    }
    if (recompui::is_context_shown(title_context)) {
        recompui::hide_context(title_context);
    }
    title_shown = false;
}

} // namespace

void remaster_ui::init() {
    if (title_ready) {
        return;
    }
    title_context = recompui::create_context(zelda64::get_asset_path("remaster_title.rml"));
    title_context.set_captures_input(false);
    title_context.set_captures_mouse(false);
    if (Rml::ElementDocument* document = title_context.get_document()) {
        document->Hide();
    }
    title_ready = true;
}

void remaster_ui::show_title_overlay() {
    init();
    if (!title_ready || title_shown) {
        return;
    }
    title_context.set_captures_input(false);
    title_context.set_captures_mouse(false);
    recompui::show_context(title_context, "");
    title_shown = true;
    title_shown_at = std::chrono::steady_clock::now();
}

void remaster_ui::on_game_buttons(uint16_t buttons) {
    if (!title_shown) {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - title_shown_at;
    if (elapsed < std::chrono::seconds(3)) {
        return;
    }
    /* A or Start leaves the title. The word stays up through the opening. */
    if ((buttons & (0x8000u | 0x1000u)) != 0) {
        hide_title();
    }
}

void update_addon_hud(bool game_started) {
    if (!hud_ready) {
        hud_context = recompui::create_context(zelda64::get_asset_path("addon_hud.rml"));
        hud_context.set_captures_input(false);
        hud_context.set_captures_mouse(false);
        if (Rml::ElementDocument* document = hud_context.get_document()) {
            document->Hide();
        }
        hud_ready = true;
    }
    std::string line = game_started ? remaster::addon_hud_line() : std::string();
#ifdef MM64R_RESEARCH
    if (game_started) {
        const char* menu = mm64r::warp_menu_line();
        const char* banner = mm64r::warp_banner();
        if (menu != nullptr && menu[0] != '\0') {
            line = std::string(menu) + (line.empty() ? "" : "  ") + line;
        }
        if (banner != nullptr && banner[0] != '\0') {
            if (!line.empty()) {
                line += "  ";
            }
            line += banner;
        }
    }
#endif
    Rml::ElementDocument* document = hud_context.get_document();
    if (document == nullptr) {
        return;
    }
    if (line.empty()) {
        if (hud_shown) {
            recompui::hide_context(hud_context);
            hud_shown = false;
        }
        return;
    }
    if (Rml::Element* row = document->GetElementById("addon-hud")) {
        row->SetInnerRML(line);
    }
    if (!hud_shown) {
        hud_context.set_captures_input(false);
        hud_context.set_captures_mouse(false);
        recompui::show_context(hud_context, "");
        hud_shown = true;
    }
}

void remaster_ui::on_frame(bool game_started) {
    update_addon_hud(game_started);
    if (!game_started) {
        hide_title();
        return;
    }
    if (!title_shown) {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - title_shown_at;
    if (elapsed > std::chrono::seconds(90)) {
        hide_title();
    }
}
