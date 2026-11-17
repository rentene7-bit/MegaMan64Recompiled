#include <array>

#include "librecomp/helpers.hpp"
#include "recomp_input.h"
#include "remaster.hpp"
#include "remaster_ui.h"
#include "ultramodern/ultramodern.hpp"

// Arrays that hold the mappings for every input for keyboard and controller respectively.
using input_mapping = std::array<recomp::InputField, recomp::bindings_per_input>;
using input_mapping_array = std::array<input_mapping, static_cast<size_t>(recomp::GameInput::COUNT)>;
static input_mapping_array keyboard_input_mappings{};
static input_mapping_array controller_input_mappings{};

// Make the button value array, which maps a button index to its bit field.
#define DEFINE_INPUT(name, value, readable) uint16_t(value##u),
static const std::array n64_button_values = {
    DEFINE_N64_BUTTON_INPUTS()
};
#undef DEFINE_INPUT

// Make the input name array.
#define DEFINE_INPUT(name, value, readable) readable,
static const std::vector<std::string> input_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

// Make the input enum name array.
#define DEFINE_INPUT(name, value, readable) #name,
static const std::vector<std::string> input_enum_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

size_t recomp::get_num_inputs() {
    return (size_t)GameInput::COUNT;
}

const std::string& recomp::get_input_name(GameInput input) {
    return input_names.at(static_cast<size_t>(input));
}

const std::string& recomp::get_input_enum_name(GameInput input) {
    return input_enum_names.at(static_cast<size_t>(input));
}

recomp::GameInput recomp::get_input_from_enum_name(const std::string_view enum_name) {
    auto find_it = std::find(input_enum_names.begin(), input_enum_names.end(), enum_name);
    if (find_it == input_enum_names.end()) {
        return recomp::GameInput::COUNT;
    }

    return static_cast<recomp::GameInput>(find_it - input_enum_names.begin());
}

// Due to an RmlUi limitation this can't be const. Ideally it would return a const reference or even just a straight up copy.
recomp::InputField& recomp::get_input_binding(GameInput input, size_t binding_index, recomp::InputDevice device) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        return cur_input_mapping[binding_index];
    }
    else {
        static recomp::InputField dummy_field = {};
        return dummy_field;
    }
}

void recomp::set_input_binding(recomp::GameInput input, size_t binding_index, recomp::InputDevice device, recomp::InputField value) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        cur_input_mapping[binding_index] = value;
    }
}

bool recomp::get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    uint16_t cur_buttons = 0;
    float cur_x = 0.0f;
    float cur_y = 0.0f;
    
    if (controller_num != 0) {
        return false;
    }

    if (!recomp::game_input_disabled()) {
        for (size_t i = 0; i < n64_button_values.size(); i++) {
            size_t input_index = (size_t)GameInput::N64_BUTTON_START + i;
            cur_buttons |= recomp::get_input_digital(keyboard_input_mappings[input_index]) ? n64_button_values[i] : 0;
            cur_buttons |= recomp::get_input_digital(controller_input_mappings[input_index]) ? n64_button_values[i] : 0;
        }

        float joystick_deadzone = recomp::get_joystick_deadzone() / 100.0f;

        float joystick_x = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_NEG]);

        float joystick_y = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_NEG]);

        recomp::apply_joystick_deadzone(joystick_x, joystick_y, &joystick_x, &joystick_y);

        cur_x = recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_NEG]) + joystick_x;

        cur_y = recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_NEG]) + joystick_y;

        /* Right stick rotates the camera through the game's existing rotate buttons.
         * The threshold is the same on every controller, so a light touch does not spin the view. */
        float right_x = 0.0f;
        float right_y = 0.0f;
        recomp::get_right_analog(&right_x, &right_y);
        constexpr float kCameraThreshold = 0.45f;
        if (right_x <= -kCameraThreshold) {
            cur_buttons |= 0x2000;
        }
        if (right_x >= kCameraThreshold) {
            cur_buttons |= 0x0010;
        }
        (void)right_y;
        if (remaster::aim_uses_gyro()) {
            float gyro_x = 0.0f;
            float gyro_y = 0.0f;
            recomp::get_gyro_deltas(&gyro_x, &gyro_y);
            gyro_x = remaster::soften_aim(gyro_x);
            gyro_y = remaster::soften_aim(gyro_y);
            if (gyro_x <= -0.75f) {
                cur_buttons |= 0x2000;
            }
            if (gyro_x >= 0.75f) {
                cur_buttons |= 0x0010;
            }
            /* C-right is the game's look button. While it is held, gyro moves the stick
             * so look mode can aim. It does not replace the stick during normal movement. */
            if ((cur_buttons & 0x0001u) != 0) {
                cur_x = std::clamp(cur_x + gyro_x, -1.0f, 1.0f);
                cur_y = std::clamp(cur_y + gyro_y, -1.0f, 1.0f);
            }
        }
        if (remaster::one_handed()) {
            const uint16_t rotate_left = cur_buttons & 0x2000;
            const uint16_t rotate_right = cur_buttons & 0x0010;
            cur_buttons = static_cast<uint16_t>((cur_buttons & ~0x2010u) | (rotate_left ? 0x0010u : 0) | (rotate_right ? 0x2000u : 0));
        }
    }

    remaster_ui::on_game_buttons(cur_buttons);

    *buttons_out = cur_buttons;
    *x_out = std::clamp(cur_x, -1.0f, 1.0f);
    *y_out = std::clamp(cur_y, -1.0f, 1.0f);
    if (remaster::speedrun_active()) {
        remaster::record_ghost_sample(cur_buttons, static_cast<int8_t>(*x_out * 127.0f), static_cast<int8_t>(*y_out * 127.0f));
    }

    return true;
}
