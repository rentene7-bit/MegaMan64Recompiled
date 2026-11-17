#include "research_script.hpp"

#include <cctype>
#include <charconv>
#include <sstream>

namespace mm64r {
namespace {

std::string lower_copy(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool parse_int(std::string_view text, int& out) {
    if (text.empty()) {
        return false;
    }
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto parsed = std::from_chars(begin, end, out, base);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool parse_u32(std::string_view text, uint32_t& out) {
    if (text.empty()) {
        return false;
    }
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto parsed = std::from_chars(begin, end, out, base);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::vector<std::string> split(std::string_view line) {
    std::vector<std::string> words;
    std::string current;
    for (char ch : line) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

bool compare_op(std::string_view text) {
    return text == "eq" || text == "ne" || text == "lt" || text == "gt" || text == "le" || text == "ge"
        || text == "changed" || text == "unchanged";
}

} // namespace

bool button_bit(std::string_view name, uint16_t& bit) {
    const std::string key = lower_copy(name);
    if (key == "a") bit = 0x8000;
    else if (key == "b") bit = 0x4000;
    else if (key == "z") bit = 0x2000;
    else if (key == "start") bit = 0x1000;
    else if (key == "dup") bit = 0x0800;
    else if (key == "ddown") bit = 0x0400;
    else if (key == "dleft") bit = 0x0200;
    else if (key == "dright") bit = 0x0100;
    else if (key == "l") bit = 0x0020;
    else if (key == "r") bit = 0x0010;
    else if (key == "cup") bit = 0x0008;
    else if (key == "cdown") bit = 0x0004;
    else if (key == "cleft") bit = 0x0002;
    else if (key == "cright") bit = 0x0001;
    else return false;
    return true;
}

bool Script::memory_matches(const Op& op, uint32_t value) const {
    if (op.compare == "changed") {
        return wait_seen_ && value != wait_last_;
    }
    if (op.compare == "unchanged") {
        return wait_seen_ && wait_frames_ > 0 && value == wait_last_;
    }
    const uint32_t expect = static_cast<uint32_t>(op.number);
    if (op.compare == "eq") return value == expect;
    if (op.compare == "ne") return value != expect;
    if (op.compare == "lt") return value < expect;
    if (op.compare == "gt") return value > expect;
    if (op.compare == "le") return value <= expect;
    if (op.compare == "ge") return value >= expect;
    return false;
}

bool Script::run_instant(const Op& op, std::vector<Action>& out, bool& yielded) {
    Action action;
    switch (op.kind) {
    case Op::Kind::Log:
        action.kind = Action::Kind::Log;
        action.text = op.text;
        out.push_back(action);
        return true;
    case Op::Kind::Timeout:
        timeout_frames_ = op.number > 0 ? op.number : 1;
        return true;
    case Op::Kind::Wait:
        waiting_frames_ = op.number;
        yielded = true;
        return true;
    case Op::Kind::Hold:
        buttons_ = static_cast<uint16_t>(buttons_ | op.button);
        return true;
    case Op::Kind::Release:
        buttons_ = static_cast<uint16_t>(buttons_ & ~op.button);
        return true;
    case Op::Kind::ReleaseAll:
        buttons_ = 0;
        stick_x_ = 0;
        stick_y_ = 0;
        return true;
    case Op::Kind::Press:
        buttons_ = static_cast<uint16_t>(buttons_ | op.button);
        press_bit_ = op.button;
        press_frames_ = op.number > 0 ? op.number : 8;
        waiting_frames_ = press_frames_;
        yielded = true;
        return true;
    case Op::Kind::Stick:
        stick_x_ = op.number;
        stick_y_ = op.number2;
        return true;
    case Op::Kind::Snapshot:
        action.kind = Action::Kind::Snapshot;
        action.text = op.text;
        out.push_back(action);
        return true;
    case Op::Kind::Diff:
        action.kind = Action::Kind::Diff;
        action.text = op.text;
        action.text2 = op.text2;
        out.push_back(action);
        return true;
    case Op::Kind::DiffRange:
        action.kind = Action::Kind::DiffRange;
        action.text = op.text;
        action.text2 = op.text2;
        action.address = op.address;
        action.length = op.length;
        out.push_back(action);
        return true;
    case Op::Kind::Watch:
        action.kind = Action::Kind::Watch;
        action.text = op.text;
        action.address = op.address;
        action.width = op.width;
        out.push_back(action);
        return true;
    case Op::Kind::Unwatch:
        action.kind = Action::Kind::Unwatch;
        action.text = op.text;
        out.push_back(action);
        return true;
    case Op::Kind::CallsStart:
        action.kind = Action::Kind::CallsStart;
        out.push_back(action);
        return true;
    case Op::Kind::CallsStop:
        action.kind = Action::Kind::CallsStop;
        action.text = op.text;
        out.push_back(action);
        return true;
    case Op::Kind::CallsDiff:
        action.kind = Action::Kind::CallsDiff;
        action.text = op.text;
        action.text2 = op.text2;
        out.push_back(action);
        return true;
    case Op::Kind::Screenshot:
        action.kind = Action::Kind::Screenshot;
        action.text = op.text;
        out.push_back(action);
        return true;
    case Op::Kind::SetFb:
        framebuffer_ = op.address;
        action.kind = Action::Kind::SetFb;
        action.address = op.address;
        out.push_back(action);
        return true;
    case Op::Kind::Echo:
        action.kind = Action::Kind::Echo;
        action.text = op.text;
        action.address = op.address;
        action.width = op.width;
        out.push_back(action);
        return true;
    case Op::Kind::Autostart:
        autostart_ = true;
        action.kind = Action::Kind::Autostart;
        out.push_back(action);
        return true;
    case Op::Kind::WaitMem:
    case Op::Kind::RequireMem:
        return true;
    }
    return true;
}

InputOverride Script::input() const {
    InputOverride result;
    result.active = loaded_ && !finished_;
    result.buttons = buttons_;
    result.x = static_cast<float>(stick_x_) / 127.0f;
    result.y = static_cast<float>(stick_y_) / 127.0f;
    if (result.x > 1.0f) result.x = 1.0f;
    if (result.x < -1.0f) result.x = -1.0f;
    if (result.y > 1.0f) result.y = 1.0f;
    if (result.y < -1.0f) result.y = -1.0f;
    return result;
}

bool Script::load(std::string_view text, std::string& error) {
    code_.clear();
    pc_ = 0;
    loaded_ = false;
    finished_ = false;
    failed_ = false;
    skipped_ = false;
    autostart_ = false;
    timeout_frames_ = 600;
    waiting_frames_ = 0;
    press_frames_ = 0;
    buttons_ = 0;
    stick_x_ = 0;
    stick_y_ = 0;
    wait_seen_ = false;
    int line_no = 0;
    std::string line;
    std::istringstream stream{std::string(text)};
    while (std::getline(stream, line)) {
        line_no++;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        const std::vector<std::string> words = split(line);
        if (words.empty()) {
            continue;
        }
        const std::string verb = lower_copy(words[0]);
        Op op;
        auto need = [&](size_t count) {
            if (words.size() < count) {
                error = "line " + std::to_string(line_no) + ": " + verb + " needs more arguments";
                return false;
            }
            return true;
        };
        if (verb == "log") {
            if (!need(2)) return false;
            op.kind = Op::Kind::Log;
            for (size_t i = 1; i < words.size(); i++) {
                if (!op.text.empty()) op.text.push_back(' ');
                op.text += words[i];
            }
        } else if (verb == "timeout") {
            if (!need(2) || !parse_int(words[1], op.number)) {
                error = "line " + std::to_string(line_no) + ": timeout <frames>";
                return false;
            }
            op.kind = Op::Kind::Timeout;
        } else if (verb == "wait") {
            if (!need(2) || !parse_int(words[1], op.number)) {
                error = "line " + std::to_string(line_no) + ": wait <frames>";
                return false;
            }
            op.kind = Op::Kind::Wait;
        } else if (verb == "hold" || verb == "release" || verb == "press") {
            if (!need(2) || !button_bit(words[1], op.button)) {
                error = "line " + std::to_string(line_no) + ": unknown button";
                return false;
            }
            op.kind = verb == "hold" ? Op::Kind::Hold : verb == "release" ? Op::Kind::Release : Op::Kind::Press;
            if (verb == "press" && words.size() >= 3 && !parse_int(words[2], op.number)) {
                error = "line " + std::to_string(line_no) + ": press <button> [frames]";
                return false;
            }
        } else if (verb == "release_all") {
            op.kind = Op::Kind::ReleaseAll;
        } else if (verb == "stick") {
            if (!need(3) || !parse_int(words[1], op.number) || !parse_int(words[2], op.number2)) {
                error = "line " + std::to_string(line_no) + ": stick <x> <y>";
                return false;
            }
            op.kind = Op::Kind::Stick;
        } else if (verb == "wait_u8" || verb == "wait_u16" || verb == "wait_u32"
            || verb == "require_u8" || verb == "require_u16" || verb == "require_u32") {
            if (!need(4)) {
                error = "line " + std::to_string(line_no) + ": " + verb + " <addr> <op> <value>";
                return false;
            }
            op.kind = verb.rfind("require", 0) == 0 ? Op::Kind::RequireMem : Op::Kind::WaitMem;
            op.width = verb.find("u32") != std::string::npos ? 4 : verb.find("u16") != std::string::npos ? 2 : 1;
            op.compare = lower_copy(words[2]);
            if (!parse_u32(words[1], op.address) || !compare_op(op.compare) || !parse_int(words[3], op.number)) {
                error = "line " + std::to_string(line_no) + ": bad memory wait";
                return false;
            }
            op.text = verb + " " + words[1];
        } else if (verb == "snapshot" || verb == "screenshot" || verb == "calls_stop" || verb == "unwatch") {
            if (!need(2)) return false;
            op.text = words[1];
            if (verb == "snapshot") op.kind = Op::Kind::Snapshot;
            else if (verb == "screenshot") op.kind = Op::Kind::Screenshot;
            else if (verb == "calls_stop") op.kind = Op::Kind::CallsStop;
            else op.kind = Op::Kind::Unwatch;
        } else if (verb == "diff" || verb == "calls_diff") {
            if (!need(3)) return false;
            op.kind = verb == "diff" ? Op::Kind::Diff : Op::Kind::CallsDiff;
            op.text = words[1];
            op.text2 = words[2];
        } else if (verb == "diff_range") {
            if (!need(5) || !parse_u32(words[3], op.address) || !parse_u32(words[4], op.length)) {
                error = "line " + std::to_string(line_no) + ": diff_range <a> <b> <start> <length>";
                return false;
            }
            op.kind = Op::Kind::DiffRange;
            op.text = words[1];
            op.text2 = words[2];
        } else if (verb == "watch" || verb == "echo") {
            if (!need(4)) {
                error = "line " + std::to_string(line_no) + ": " + verb + " <u8|u16|u32> <addr> <label>";
                return false;
            }
            const std::string width = lower_copy(words[1]);
            op.width = width == "u32" ? 4 : width == "u16" ? 2 : width == "u8" ? 1 : 0;
            if (op.width == 0 || !parse_u32(words[2], op.address)) {
                error = "line " + std::to_string(line_no) + ": bad width or address";
                return false;
            }
            op.kind = verb == "watch" ? Op::Kind::Watch : Op::Kind::Echo;
            op.text = words[3];
        } else if (verb == "calls_start") {
            op.kind = Op::Kind::CallsStart;
        } else if (verb == "set_fb") {
            if (!need(2) || !parse_u32(words[1], op.address)) {
                error = "line " + std::to_string(line_no) + ": set_fb <addr>";
                return false;
            }
            op.kind = Op::Kind::SetFb;
        } else if (verb == "autostart") {
            op.kind = Op::Kind::Autostart;
        } else {
            error = "line " + std::to_string(line_no) + ": unknown command " + verb;
            return false;
        }
        code_.push_back(op);
    }
    loaded_ = true;
    error.clear();
    return true;
}

} // namespace mm64r
