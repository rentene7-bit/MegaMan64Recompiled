#include "research_script.hpp"

#include <iostream>
#include <string>

namespace {

int g_failed = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL " << message << "\n";
        g_failed++;
    }
}

uint32_t g_mem = 0;

bool read_mem(int width, uint32_t address, uint32_t& value) {
    (void)width;
    (void)address;
    value = g_mem;
    return true;
}

} // namespace

int main() {
    mm64r::Script script;
    std::string error;
    const char* text =
        "log hello\n"
        "hold A\n"
        "wait 2\n"
        "release A\n"
        "press B 1\n"
        "stick 40 -20\n"
        "wait_u16 0x80204A1E gt 0\n"
        "echo u16 0x80204A1E hp\n"
        "require_u16 0x80204A1E eq 80\n";
    expect(script.load(text, error), error.empty() ? "script loads" : error);
    auto actions = script.on_frame(read_mem);
    expect(!actions.empty() && actions[0].text == "hello", "log runs on the first frame");
    expect(script.input().buttons == 0x8000, "A is held");
    script.on_frame(read_mem);
    expect(script.input().buttons == 0x8000, "A stays held during the wait");
    script.on_frame(read_mem);
    expect((script.input().buttons & 0x8000) == 0, "A is released when the wait ends");
    expect((script.input().buttons & 0x4000) != 0, "press holds B on that frame");
    script.on_frame(read_mem);
    expect(script.input().x > 0.3f && script.input().y < 0.0f, "stick is applied");
    g_mem = 0;
    script.on_frame(read_mem);
    expect(script.active(), "the memory wait is still pending");
    g_mem = 80;
    actions = script.on_frame(read_mem);
    bool saw_hp = false;
    for (const auto& action : actions) {
        if (action.kind == mm64r::Action::Kind::Echo) {
            saw_hp = true;
        }
    }
    expect(saw_hp, "echo runs once memory matches");
    expect(script.finished(), "require passes and the script ends");

    mm64r::Script skipped;
    expect(skipped.load("require_u16 0x10 eq 1\n", error), "require script loads");
    g_mem = 0;
    actions = skipped.on_frame(read_mem);
    expect(skipped.skipped(), "a failed require skips the script");
    expect(!actions.empty() && actions[0].kind == mm64r::Action::Kind::Skip, "skip is reported");

    if (g_failed != 0) {
        std::cerr << g_failed << " failed\n";
        return 1;
    }
    std::cout << "research script tests passed\n";
    return 0;
}
