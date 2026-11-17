#ifndef MM64_RESEARCH_SCRIPT_HPP
#define MM64_RESEARCH_SCRIPT_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/* Text script for the research harness. The runner does not touch the game.
 * The integration layer supplies memory reads and receives the actions. */

namespace mm64r {

enum class Width { U8 = 1, U16 = 2, U32 = 4 };

struct Action {
    enum class Kind {
        Log,
        Echo,
        Snapshot,
        Diff,
        DiffRange,
        Watch,
        Unwatch,
        CallsStart,
        CallsStop,
        CallsDiff,
        Screenshot,
        SetFb,
        Autostart,
        Done,
        Skip,
        Fail
    } kind = Kind::Log;
    std::string text;
    std::string text2;
    uint32_t address = 0;
    uint32_t length = 0;
    int width = 1;
    uint32_t value = 0;
};

struct InputOverride {
    bool active = false;
    uint16_t buttons = 0;
    float x = 0.0f;
    float y = 0.0f;
};

class Script {
public:
    bool load(std::string_view text, std::string& error);
    bool active() const { return loaded_ && !finished_; }
    bool finished() const { return finished_; }
    bool skipped() const { return skipped_; }
    bool failed() const { return failed_; }
    bool autostart() const { return autostart_; }
    uint32_t framebuffer() const { return framebuffer_; }
    InputOverride input() const;

    /* Advance one video frame. read_mem returns false when the address is unavailable. */
    template <typename Read>
    std::vector<Action> on_frame(Read&& read_mem) {
        std::vector<Action> out;
        if (!loaded_ || finished_) {
            return out;
        }
        if (press_frames_ > 0) {
            press_frames_--;
            if (press_frames_ == 0) {
                buttons_ = static_cast<uint16_t>(buttons_ & ~press_bit_);
                press_bit_ = 0;
            }
        }
        step(out, read_mem);
        return out;
    }

private:
    struct Op {
        enum class Kind {
            Log, Timeout, Wait, Hold, Release, ReleaseAll, Press, Stick,
            WaitMem, RequireMem, Snapshot, Diff, DiffRange, Watch, Unwatch,
            CallsStart, CallsStop, CallsDiff, Screenshot, SetFb, Echo, Autostart
        } kind = Kind::Log;
        std::string text;
        std::string text2;
        std::string compare;
        uint32_t address = 0;
        uint32_t length = 0;
        int width = 1;
        int number = 0;
        int number2 = 0;
        uint16_t button = 0;
    };

    template <typename Read>
    void step(std::vector<Action>& out, Read&& read_mem) {
        while (pc_ < code_.size() && !finished_) {
            if (waiting_frames_ > 0) {
                waiting_frames_--;
                if (waiting_frames_ > 0) {
                    return;
                }
            }
            const Op& op = code_[pc_];
            if (op.kind == Op::Kind::WaitMem || op.kind == Op::Kind::RequireMem) {
                uint32_t value = 0;
                if (!read_mem(op.width, op.address, value)) {
                    out.push_back(Action{Action::Kind::Fail, "memory is not available yet"});
                    failed_ = true;
                    finished_ = true;
                    return;
                }
                if (!wait_seen_) {
                    wait_seen_ = true;
                    wait_last_ = value;
                    wait_frames_ = 0;
                }
                if (memory_matches(op, value)) {
                    if (op.kind == Op::Kind::RequireMem) {
                        pc_++;
                        wait_seen_ = false;
                        continue;
                    }
                    pc_++;
                    wait_seen_ = false;
                    continue;
                }
                if (op.kind == Op::Kind::RequireMem) {
                    out.push_back(Action{Action::Kind::Skip, "require failed at " + op.text});
                    skipped_ = true;
                    finished_ = true;
                    return;
                }
                wait_frames_++;
                wait_last_ = value;
                if (wait_frames_ >= timeout_frames_) {
                    out.push_back(Action{Action::Kind::Log, "timeout waiting for " + op.text});
                    pc_++;
                    wait_seen_ = false;
                    continue;
                }
                return;
            }

            bool yielded = false;
            if (!run_instant(op, out, yielded)) {
                return;
            }
            pc_++;
            if (yielded) {
                return;
            }
        }
        if (pc_ >= code_.size() && !finished_) {
            finished_ = true;
            buttons_ = 0;
            stick_x_ = 0;
            stick_y_ = 0;
            out.push_back(Action{Action::Kind::Done, {}});
        }
    }

    bool run_instant(const Op& op, std::vector<Action>& out, bool& yielded);
    bool memory_matches(const Op& op, uint32_t value) const;

    std::vector<Op> code_;
    size_t pc_ = 0;
    bool loaded_ = false;
    bool finished_ = false;
    bool failed_ = false;
    bool skipped_ = false;
    bool autostart_ = false;
    int timeout_frames_ = 600;
    int waiting_frames_ = 0;
    int press_frames_ = 0;
    uint16_t press_bit_ = 0;
    uint16_t buttons_ = 0;
    int stick_x_ = 0;
    int stick_y_ = 0;
    bool wait_seen_ = false;
    int wait_frames_ = 0;
    uint32_t wait_last_ = 0;
    uint32_t framebuffer_ = 0;
};

bool button_bit(std::string_view name, uint16_t& bit);

} // namespace mm64r

#endif
