// kbmouse.c - Gamepad to Keyboard/Mouse conversion
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Converts gamepad input to keyboard and mouse HID reports.

#include "kbmouse.h"
#include "core/buttons.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// STATE
// ============================================================================

// Analog configuration (can be modified at runtime)
static kbmouse_analog_config_t analog_config = {
    .deadzone = KBMOUSE_DEFAULT_DEADZONE,
    .sensitivity = KBMOUSE_DEFAULT_SENSITIVITY,
    .scroll_deadzone = KBMOUSE_DEFAULT_SCROLL_DEADZONE,
    .scroll_speed = KBMOUSE_DEFAULT_SCROLL_SPEED,
};

// Keyboard LED state (set by host via output reports)
static uint8_t keyboard_led_state = 0;

// ============================================================================
// DEFAULT BUTTON MAPPING
// ============================================================================

// Default mapping table: gamepad button -> keyboard/mouse action
// Gaming-optimized: triggers for fire/aim, face buttons for actions
static const kbmouse_button_map_t default_button_map[] = {
    // Face buttons (common game actions)
    { JP_BUTTON_B1, KBMOUSE_ACTION_KEY, HID_KEY_SPACE },              // A/Cross -> Space (jump)
    { JP_BUTTON_B2, KBMOUSE_ACTION_KEY, HID_KEY_E },                 // B/Circle -> E (interact)
    { JP_BUTTON_B3, KBMOUSE_ACTION_KEY, HID_KEY_R },                 // X/Square -> R (reload)
    { JP_BUTTON_B4, KBMOUSE_ACTION_KEY, HID_KEY_Q },                 // Y/Triangle -> Q (ability/swap)

    // Shoulder buttons (modifiers)
    { JP_BUTTON_L1, KBMOUSE_ACTION_MODIFIER, KBMOUSE_MOD_LSHIFT },   // L1 -> Shift (sprint)
    { JP_BUTTON_R1, KBMOUSE_ACTION_MODIFIER, KBMOUSE_MOD_LCTRL },    // R1 -> Ctrl (crouch)

    // Triggers (primary combat)
    { JP_BUTTON_L2, KBMOUSE_ACTION_MOUSE_BTN, KBMOUSE_BTN_RIGHT },   // L2 -> Right click (ADS/aim)
    { JP_BUTTON_R2, KBMOUSE_ACTION_MOUSE_BTN, KBMOUSE_BTN_LEFT },    // R2 -> Left click (fire)

    // Center cluster
    { JP_BUTTON_S1, KBMOUSE_ACTION_KEY, HID_KEY_TAB },               // Select -> Tab (map/scoreboard)
    { JP_BUTTON_S2, KBMOUSE_ACTION_KEY, HID_KEY_ESCAPE },            // Start -> Escape (menu)

    // Stick clicks
    { JP_BUTTON_L3, KBMOUSE_ACTION_KEY, HID_KEY_V },                 // L3 -> V (melee)
    { JP_BUTTON_R3, KBMOUSE_ACTION_KEY, HID_KEY_F },                 // R3 -> F (interact/melee)

    // D-pad (weapon slots)
    { JP_BUTTON_DU, KBMOUSE_ACTION_KEY, HID_KEY_1 },                 // D-Up -> 1 (primary)
    { JP_BUTTON_DD, KBMOUSE_ACTION_KEY, HID_KEY_3 },                 // D-Down -> 3 (equipment)
    { JP_BUTTON_DL, KBMOUSE_ACTION_KEY, HID_KEY_4 },                 // D-Left -> 4 (grenade/util)
    { JP_BUTTON_DR, KBMOUSE_ACTION_KEY, HID_KEY_2 },                 // D-Right -> 2 (secondary)

    // Auxiliary
    { JP_BUTTON_A1, KBMOUSE_ACTION_KEY, HID_KEY_M },                 // Home/Guide -> M (map)
};

#define DEFAULT_MAP_COUNT (sizeof(default_button_map) / sizeof(default_button_map[0]))

// ============================================================================
// ANALOG PROCESSING
// ============================================================================

// Max mouse pixels per USB poll at default sensitivity (5).
// At ~125Hz polling: 15 * 125 = 1875 px/sec max (crosses 1080p in ~1s).
#define MOUSE_MAX_SPEED 15.0f

// Sub-pixel accumulators for smooth low-speed movement
static float mouse_accum_x = 0.0f;
static float mouse_accum_y = 0.0f;
static float scroll_accum = 0.0f;

// Previous Rz value for delta-based scroll
static uint8_t prev_rz = 128;

// Apply deadzone, cubic curve, and sub-pixel accumulation.
// Cubic (x³) gives a large precision zone: 50% deflection = 12.5% of max speed,
// while full deflection still reaches max. Sub-pixel accumulation ensures smooth
// low-speed movement by carrying fractional pixels between polls.
static int8_t process_analog_to_mouse(uint8_t analog, uint8_t deadzone,
                                       uint8_t sensitivity, float* accumulator)
{
    // Center analog value to signed
    int16_t centered = (int16_t)analog - 128;

    // Apply deadzone — reset accumulator to prevent drift
    if (abs(centered) < deadzone) {
        *accumulator = 0.0f;
        return 0;
    }

    // Calculate sign and magnitude
    int16_t sign = (centered > 0) ? 1 : -1;
    int16_t magnitude = abs(centered) - deadzone;

    // Normalize to 0.0-1.0 range (after deadzone)
    float normalized = (float)magnitude / (127 - deadzone);

    // Cubic curve for large precision zone at low deflection
    // 25% deflection → 1.6% speed, 50% → 12.5%, 75% → 42%, 100% → 100%
    float curved = normalized * normalized * normalized;

    // Scale by sensitivity (1-10 maps to 0.2-2.0)
    float sens_factor = (float)sensitivity / 5.0f;

    // Accumulate sub-pixel movement
    *accumulator += curved * MOUSE_MAX_SPEED * sens_factor * sign;

    // Extract integer pixels, keep fractional remainder
    int8_t delta = (int8_t)*accumulator;
    *accumulator -= (float)delta;

    return delta;
}


// ============================================================================
// CONVERSION API
// ============================================================================

void kbmouse_init(void)
{
    // Reset to default configuration
    analog_config.deadzone = KBMOUSE_DEFAULT_DEADZONE;
    analog_config.sensitivity = KBMOUSE_DEFAULT_SENSITIVITY;
    analog_config.scroll_deadzone = KBMOUSE_DEFAULT_SCROLL_DEADZONE;
    analog_config.scroll_speed = KBMOUSE_DEFAULT_SCROLL_SPEED;
    mouse_accum_x = 0.0f;
    mouse_accum_y = 0.0f;
    scroll_accum = 0.0f;
    prev_rz = 128;
    keyboard_led_state = 0;
}

void kbmouse_convert(uint32_t buttons,
                     const profile_output_t* profile_out,
                     kbmouse_keyboard_report_t* kb_report,
                     kbmouse_mouse_report_t* mouse_report)
{
    // Clear reports
    memset(kb_report, 0, sizeof(kbmouse_keyboard_report_t));
    memset(mouse_report, 0, sizeof(kbmouse_mouse_report_t));

    uint8_t keycode_index = 0;

    // Process button mappings
    for (size_t i = 0; i < DEFAULT_MAP_COUNT; i++) {
        const kbmouse_button_map_t* map = &default_button_map[i];

        // Check if gamepad button is pressed
        if (buttons & map->gamepad_button) {
            switch (map->type) {
                case KBMOUSE_ACTION_KEY:
                    // Add keycode if we have room (6 key rollover)
                    if (keycode_index < 6) {
                        kb_report->keycode[keycode_index++] = map->value;
                    }
                    break;

                case KBMOUSE_ACTION_MODIFIER:
                    // Add modifier
                    kb_report->modifier |= map->value;
                    break;

                case KBMOUSE_ACTION_MOUSE_BTN:
                    // Add mouse button
                    mouse_report->buttons |= map->value;
                    break;

                default:
                    break;
            }
        }
    }

    // Process analog sticks

    // Rz axis present (non-zero) means device has a dedicated extra axis
    // (e.g. twist controller). Use it for delta-based scroll, and skip
    // right stick → mouse so the axes don't conflict.
    // Normal controllers (rz_analog == 0) use right stick for mouse.
    if (profile_out->rz_analog != 0) {
        // Delta-based scroll: twist *movement* generates scroll ticks.
        // Twisting scrolls proportionally; releasing springs back and
        // scrolls in reverse as the axis returns to center.
        int16_t rz_delta = (int16_t)profile_out->rz_analog - (int16_t)prev_rz;
        prev_rz = profile_out->rz_analog;
        if (rz_delta != 0) {
            // scroll_speed 1-10 maps to 0.1-1.0 scaling
            float scale = (float)analog_config.scroll_speed / 10.0f;
            scroll_accum += (float)rz_delta * scale;
            int8_t scroll_ticks = (int8_t)scroll_accum;
            scroll_accum -= (float)scroll_ticks;
            mouse_report->wheel = -scroll_ticks;
        }
    } else {
        // Right stick -> Mouse movement (standard controllers)
        mouse_report->x = process_analog_to_mouse(
            profile_out->right_x,
            analog_config.deadzone,
            analog_config.sensitivity,
            &mouse_accum_x
        );
        mouse_report->y = process_analog_to_mouse(
            profile_out->right_y,
            analog_config.deadzone,
            analog_config.sensitivity,
            &mouse_accum_y
        );
    }

    // Left stick -> WASD keys (movement)
    // Use a larger deadzone for digital output to avoid drift
    const uint8_t wasd_deadzone = 40;

    // W - stick up (Y < center - deadzone)
    if (profile_out->left_y < (128 - wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_W;
    }
    // S - stick down (Y > center + deadzone)
    if (profile_out->left_y > (128 + wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_S;
    }
    // A - stick left (X < center - deadzone)
    if (profile_out->left_x < (128 - wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_A;
    }
    // D - stick right (X > center + deadzone)
    if (profile_out->left_x > (128 + wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_D;
    }
}

const kbmouse_analog_config_t* kbmouse_get_config(void)
{
    return &analog_config;
}

void kbmouse_set_config(const kbmouse_analog_config_t* config)
{
    if (config) {
        analog_config = *config;
    }
}

uint8_t kbmouse_get_led_state(void)
{
    return keyboard_led_state;
}

void kbmouse_set_led_state(uint8_t leds)
{
    keyboard_led_state = leds;
}
