#ifndef MM64R_ADDON_TOOLS_HPP
#define MM64R_ADDON_TOOLS_HPP

/* Stable calls for the add-on 24-46 branch.
 *
 * Story bits for add-ons 1-23 are 0xC00-0xCFF (guest 0x80205728, MSB-first
 * from 0x802055A8). They are not 0x900-0x9FF. Add-ons 24-46 use 0xA00-0xAFF.
 *
 * Map the other branch's stubs onto these:
 *   remaster::addons::ToolHooks     -> remaster::tools::ToolHooks
 *   tool_hooks().sonar              -> remaster::tools::sonar()
 *   tool_hooks().fusion             -> remaster::tools::fusion()
 *   note_deep_gate_floor / floor    -> remaster::tools::note_deep_gate_floor
 *                                      remaster::tools::deep_gate_floor
 */

namespace remaster::tools {

constexpr int kFlagLo = 0xC00;
constexpr int kFlagHi = 0xCFF;

struct ToolHooks {
    bool sonar = false;
    bool grapple = false;
    bool coolant = false;
    bool fusion = false;
};

ToolHooks tool_hooks();
bool sonar();
bool fusion();
int deep_gate_floor();
void note_deep_gate_floor(int floor);

} // namespace remaster::tools

#endif
