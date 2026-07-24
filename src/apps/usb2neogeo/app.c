// app.c - USB2NEOGEO App Entry Point
// USB to NEOGEO+ adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.
//
// CUSTOM MAPPING FEATURE:
// Hold Coin (S1) for 2 seconds to enter recording mode.
// While still holding Coin, press buttons in sequence to assign them to
// Neo Geo outputs B1, B2, B3, B4, B5, B6 (in that order).
// Release Coin to finalize and save the custom mapping.
// Only action buttons are recordable: Cross, Circle, Square, Triangle, L1, R1, L2, R2.
// D-pad, Coin, and Start always pass through and cannot be recorded.
//
// NOTE: For XInput controllers (Xbox), L2/R2 are purely analog triggers.
// The driver does NOT set JP_BUTTON_L2/R2 digital bits. We synthesize them
// from the analog values using a threshold (CM_TRIGGER_THRESHOLD).
//
// To exit custom profile and return to fixed profiles, use the standard
// Coin + D-pad combo (hold Coin 2s, then press D-pad Up/Down).
//
// EXTERNAL RGB LED BEHAVIOR:
// The external RGB LED (common cathode, PWM on GP0/GP1/GP5) provides
// a panel-mountable visual indicator with the following states:
//   - GREEN constant:  System ready / normal operation
//   - RED:             Entered recording mode (Coin held 2s)
//   - Color per slot:  Each button press during recording shows a unique color
//                      (Yellow, Blue, Magenta, Cyan, Orange, Purple)
//   - GREEN returns:   After recording is saved, LED returns to green

#include "app.h"
#include "profiles.h"
#include "ext_rgb_led.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/leds/leds.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/gpio/gpio_device.h"
#include "usb/usbh/usbh.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// CUSTOM MAPPING RECORDING STATE
// ============================================================================

// Recording state machine
typedef enum {
    CM_STATE_IDLE,          // Normal operation
    CM_STATE_WAITING,       // Coin held, waiting for 2s threshold
    CM_STATE_RECORDING,     // Recording mode active (Coin still held)
} cm_state_t;

// Maximum number of Neo Geo action button slots to record
#define CM_MAX_SLOTS 6

// Allowed USB input buttons for recording (only action buttons)
// These are the 8 buttons the user can assign to B1-B6:
//   Cross (B1), Circle (B2), Square (B3), Triangle (B4), L1, R1, L2, R2
#define CM_ALLOWED_BUTTONS (JP_BUTTON_B1 | JP_BUTTON_B2 | JP_BUTTON_B3 | JP_BUTTON_B4 | \
                            JP_BUTTON_L1 | JP_BUTTON_R1 | JP_BUTTON_L2 | JP_BUTTON_R2)

// Hold time in milliseconds to enter recording mode
#define CM_HOLD_TIME_MS 2500

// Analog trigger threshold for detecting L2/R2 press (0-255)
// XInput controllers send L2/R2 as analog only (no digital bit).
// We consider the trigger "pressed" when its analog value >= this threshold.
#define CM_TRIGGER_THRESHOLD 64

// Minimum delay between recorded button slots.
// This filters mechanical bounce/mau contato that can look like two rapid presses
// of the same physical button during custom mapping.
#define CM_RECORD_MIN_INTERVAL_MS 200


// Neo Geo output targets for each slot (B1 through B6)
static const uint32_t cm_neogeo_outputs[CM_MAX_SLOTS] = {
    NEOGEO_BUTTON_B1,   // Slot 0 → P1/A
    NEOGEO_BUTTON_B2,   // Slot 1 → P2/B
    NEOGEO_BUTTON_B3,   // Slot 2 → P3/C
    NEOGEO_BUTTON_B4,   // Slot 3 → K1/D
    NEOGEO_BUTTON_B5,   // Slot 4 → K2/Select
    NEOGEO_BUTTON_B6,   // Slot 5 → K3
};

// Slot colors for visual feedback during recording.
// Avoids green (used for "ready") and red (used for "recording entered").
// Each slot gets a distinct, vibrant color for clear visual identification.
static const uint8_t cm_slot_colors[CM_MAX_SLOTS][3] = {
    {25, 64, 0},   // B1 - Yellow       (R+G) {64, 64, 0},
    {0, 0, 64},    // B2 - Blue          (B) 0, 0, 64},
    {48, 0, 64},   // B3 - Magenta       (R+B) {64, 0, 64}, 
    {0, 64, 64},   // B4 - Cyan          (G+B) {0, 64, 64}, 
    {25, 32, 0},   // B5 - Orange        (R + half G) {64, 32, 0},  
    {16, 0, 64},   // B6 - Purple/Violet (half R + B) 32, 0, 64},
};

// Slot color names for debug logging
static const char* cm_slot_color_names[CM_MAX_SLOTS] = {
    "Yellow", "Blue", "Magenta", "Cyan", "Orange", "Purple"
};

// Recording state
static cm_state_t cm_state = CM_STATE_IDLE;
static uint32_t cm_hold_start = 0;
static uint8_t cm_slot_index = 0;                      // Next slot to record
static uint32_t cm_recorded_inputs[CM_MAX_SLOTS] = {0}; // What input was recorded per slot
static uint32_t cm_last_buttons = 0;                    // Previous frame's button state
static uint32_t cm_last_record_time = 0;               // Last accepted slot timestamp
static bool cm_custom_active = false;                   // Is custom profile currently active?
static bool cm_analog_dpad_enabled = false;             // Runtime: left analog also drives D-pad
static bool cm_analog_toggle_latched = false;           // Edge latch for Start+Coin+B1 toggle
static uint32_t cm_menu_combo_start = 0;                // Start+Coin+strong buttons hold timer
static uint32_t cm_aux_combo_start = 0;                 // Start+Coin+weak buttons hold timer

#define CM_SERVICE_HOLD_TIME_MS 300
#define CM_MENU_COMBO_MASK (JP_BUTTON_S2 | JP_BUTTON_S1 | JP_BUTTON_R1 | JP_BUTTON_R2)
#define CM_AUX_COMBO_MASK (JP_BUTTON_S2 | JP_BUTTON_S1 | JP_BUTTON_B3 | JP_BUTTON_B1)

// Timer for returning BOTH LEDs to base color after temporary feedback effects.
static uint32_t led_feedback_timer = 0;

// Track the profile index to detect when user switches via Coin+D-pad combo
static uint8_t cm_last_profile_index = 0;

// Forward declarations
static void cm_apply_recording(void);
static void cm_enter_recording(void);
static void cm_exit_recording(void);

// ============================================================================
// HELPER: Set both internal WS2812 and external RGB LED to the same color
// ============================================================================

static void set_both_leds(uint8_t r, uint8_t g, uint8_t b)
{
    leds_set_color(r, g, b);           // Internal WS2812 on the RP2040-Zero
    ext_rgb_led_set_color(r, g, b);    // External panel-mount RGB LED
}

// Set only the external LED (without changing the internal WS2812)
static void set_ext_led(uint8_t r, uint8_t g, uint8_t b)
{
    ext_rgb_led_set_color(r, g, b);
}

// ============================================================================
// GPIO PIN CONFIGURATION
// ============================================================================

static gpio_device_config_t gpio_gpio_config[GPIO_MAX_PLAYERS] = {
    [0] = {
        .pin_du = P1_NEOGEO_DU_PIN,
        .pin_dd = P1_NEOGEO_DD_PIN,
        .pin_dl = P1_NEOGEO_DL_PIN,
        .pin_dr = P1_NEOGEO_DR_PIN,

        // Action Buttons
        .pin_b1 = P1_NEOGEO_B4_PIN,
        .pin_b2 = P1_NEOGEO_B5_PIN,
        .pin_b3 = P1_NEOGEO_B1_PIN,
        .pin_b4 = P1_NEOGEO_B2_PIN,
        .pin_l1 = GPIO_DISABLED,
        .pin_r1 = P1_NEOGEO_B3_PIN,
        .pin_l2 = GPIO_DISABLED,
        .pin_r2 = P1_NEOGEO_B6_PIN,

        // Meta Buttons

        .pin_s1 = P1_NEOGEO_S1_PIN,
        .pin_s2 = P1_NEOGEO_S2_PIN,
        .pin_a1 = P1_NEOGEO_MENU_PIN,
        .pin_a2 = P1_NEOGEO_AUX_PIN,
        // Extra Buttons
        .pin_l3 = GPIO_DISABLED,
        .pin_r3 = GPIO_DISABLED,
        .pin_l4 = GPIO_DISABLED,
        .pin_r4 = GPIO_DISABLED,
    },
    [1] = PORT_CONFIG_INIT
};

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_GPIO] = &neogeo_profile_set,
    },
    .shared_profiles = NULL,
};

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface gpio_output_interface;

static const OutputInterface* output_interfaces[] = {
    &gpio_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// CUSTOM MAPPING - RECORDING LOGIC
// ============================================================================

// Enter recording mode: reset slots, set LED to RED to indicate recording
static void cm_enter_recording(void)
{
    cm_state = CM_STATE_RECORDING;
    cm_slot_index = 0;
    memset(cm_recorded_inputs, 0, sizeof(cm_recorded_inputs));
    cm_last_buttons = 0;
    cm_last_record_time = 0;

    // Visual feedback: RED on both LEDs = "recording mode entered, waiting for buttons"
    set_both_leds(64, 0, 0);

    printf("[app:usb2neogeo] Custom mapping: RECORDING started (LED=RED, hold Coin, press buttons for B1-B6)\n");
}

// Exit recording mode: build the custom profile from recorded inputs
static void cm_exit_recording(void)
{
    cm_state = CM_STATE_IDLE;

    if (cm_slot_index > 0) {
        // We recorded at least one button — apply the custom mapping
        cm_apply_recording();
        cm_custom_active = true;

        // Visual feedback: brief white flash on both LEDs to confirm save,
        // then the ext LED will return to green in app_task() after the timer
        set_both_leds(32, 64, 50); //(64, 64, 64);

        printf("[app:usb2neogeo] Custom mapping: SAVED (%d buttons recorded, LED will return to GREEN)\n", cm_slot_index);
    } else {
        // No buttons recorded — deactivate custom profile, revert to normal
        cm_custom_active = false;

        // Visual feedback: brief red blink to indicate nothing saved,
        // then the ext LED will return to green in app_task() after the timer
        set_both_leds(32, 0, 0);

        printf("[app:usb2neogeo] Custom mapping: CANCELLED (no buttons recorded, LED will return to GREEN)\n");
    }

    // Trigger a profile indicator blink on the internal WS2812
    uint8_t active_idx = profile_get_active_index(OUTPUT_TARGET_GPIO);
    if (cm_custom_active) {
        leds_indicate_profile(NEOGEO_FIXED_PROFILE_COUNT);
    } else {
        leds_indicate_profile(active_idx);
    }
}

// Build the neogeo_custom_map[] from recorded inputs
static void cm_apply_recording(void)
{
    uint8_t map_idx = 0;
    uint32_t used_inputs = 0;

    // First: add recorded mappings (input → Neo Geo output)
    for (uint8_t i = 0; i < cm_slot_index && i < CM_MAX_SLOTS; i++) {
        if (cm_recorded_inputs[i] != 0) {
            neogeo_custom_map[map_idx].input = cm_recorded_inputs[i];
            neogeo_custom_map[map_idx].output = cm_neogeo_outputs[i];
            neogeo_custom_map[map_idx].analog = ANALOG_TARGET_NONE;
            neogeo_custom_map[map_idx].analog_value = 0;
            used_inputs |= cm_recorded_inputs[i];
            map_idx++;
        }
    }

    // Second: disable all allowed buttons that were NOT recorded
    // This prevents unrecorded action buttons from passing through
    static const uint32_t all_action_buttons[] = {
        JP_BUTTON_B1, JP_BUTTON_B2, JP_BUTTON_B3, JP_BUTTON_B4,
        JP_BUTTON_L1, JP_BUTTON_R1, JP_BUTTON_L2, JP_BUTTON_R2,
    };

    for (uint8_t i = 0; i < 8; i++) {
        if (!(used_inputs & all_action_buttons[i])) {
            // This button was not used in any recording slot — disable it
            if (map_idx < CUSTOM_MAP_MAX_ENTRIES) {
                neogeo_custom_map[map_idx].input = all_action_buttons[i];
                neogeo_custom_map[map_idx].output = 0;  // Disabled
                neogeo_custom_map[map_idx].analog = ANALOG_TARGET_NONE;
                neogeo_custom_map[map_idx].analog_value = 0;
                map_idx++;
            }
        }
    }

    // Update the custom profile's button_map_count
    neogeo_custom_map_count = map_idx;
    neogeo_profile_custom.button_map_count = map_idx;

    printf("[app:usb2neogeo] Custom map built: %d entries (%d recorded + %d disabled)\n",
           map_idx, cm_slot_index, map_idx - cm_slot_index);

    // Debug: print the mapping
    for (uint8_t i = 0; i < map_idx; i++) {
        if (neogeo_custom_map[i].output != 0) {
            printf("  [%d] input=0x%04lX → output=0x%04lX\n", i,
                   (unsigned long)neogeo_custom_map[i].input,
                   (unsigned long)neogeo_custom_map[i].output);
        } else {
            printf("  [%d] input=0x%04lX → DISABLED\n", i,
                   (unsigned long)neogeo_custom_map[i].input);
        }
    }
}

// Return a bit mask of inputs already recorded in the current session.
static uint32_t cm_recorded_mask(void)
{
    uint32_t mask = 0;
    for (uint8_t i = 0; i < cm_slot_index && i < CM_MAX_SLOTS; i++) {
        mask |= cm_recorded_inputs[i];
    }
    return mask;
}

// Process one frame of recording input
// Called from app_task() with the enriched button state (including synthesized L2/R2)
static void cm_process_recording(uint32_t buttons)
{
    bool coin_held = (buttons & JP_BUTTON_S1) != 0;
    bool dpad_any = (buttons & (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR)) != 0;

    switch (cm_state) {
        case CM_STATE_IDLE:
            // Only start waiting if Coin is held WITHOUT any D-pad direction.
            // If D-pad is also held, this is the profile-switch combo, not recording.
            if (coin_held && !dpad_any) {
                cm_state = CM_STATE_WAITING;
                cm_hold_start = platform_time_ms();
            }
            break;

        case CM_STATE_WAITING:
            if (!coin_held) {
                // Coin released before 2s — abort, go back to idle
                cm_state = CM_STATE_IDLE;
            } else if (dpad_any) {
                // D-pad pressed while waiting — this is profile-switch, not recording
                cm_state = CM_STATE_IDLE;
            } else {
                uint32_t elapsed = platform_time_ms() - cm_hold_start;
                if (elapsed >= CM_HOLD_TIME_MS) {
                    cm_enter_recording();
                }
            }
            break;

        case CM_STATE_RECORDING:
            if (!coin_held) {
                // Coin released — finalize recording
                cm_exit_recording();
            } else {
                // Detect newly pressed buttons (rising edge only)
                uint32_t newly_pressed = (buttons & ~cm_last_buttons) & CM_ALLOWED_BUTTONS;

                if (newly_pressed != 0 && cm_slot_index < CM_MAX_SLOTS) {
                    uint32_t now = platform_time_ms();
                    bool interval_ok = (cm_last_record_time == 0) ||
                                       ((now - cm_last_record_time) >= CM_RECORD_MIN_INTERVAL_MS);

                    if (interval_ok) {
                        // Find the first newly pressed allowed button that has not
                        // already been recorded in this mapping session.
                        for (int bit = 0; bit < 32; bit++) {
                            uint32_t mask = (1u << bit);
                            if ((newly_pressed & mask) &&
                                ((cm_recorded_mask() & mask) == 0)) {
                                cm_recorded_inputs[cm_slot_index] = mask;
                                cm_slot_index++;
                                cm_last_record_time = now;

                                printf("[app:usb2neogeo] Recorded slot %d: input=0x%04lX → NeoGeo B%d (LED=%s)\n",
                                       cm_slot_index, (unsigned long)mask, cm_slot_index,
                                       cm_slot_color_names[cm_slot_index - 1]);

                                // Visual feedback on BOTH LEDs: unique color per slot
                                uint8_t ci = cm_slot_index - 1;
                                if (ci < CM_MAX_SLOTS) {
                                    set_both_leds(cm_slot_colors[ci][0],
                                                  cm_slot_colors[ci][1],
                                                  cm_slot_colors[ci][2]);
                                }

                                break;  // Only record one button per frame
                            }

                        }
                    }
                }
            }
            break;
    }

    cm_last_buttons = buttons;
}

// ============================================================================
// CUSTOM PROFILE ACCESS (used by gpio_device.c via extern)
// ============================================================================

// Returns the custom profile if active, or NULL if not
const profile_t* app_get_custom_profile(void)
{
    if (cm_custom_active) {
        return &neogeo_profile_custom;
    }
    return NULL;
}

// Deactivate the custom profile (called when user switches to a fixed profile)
void app_deactivate_custom_profile(void)
{
    if (cm_custom_active) {
        cm_custom_active = false;
        // Return external LED to green (normal operation)
        set_ext_led(0, 8, 0);
        printf("[app:usb2neogeo] Custom mapping: DEACTIVATED (switched to fixed profile)\n");
    }
}

// Check if recording mode is active (to suppress normal output)
bool app_is_recording(void)
{
    return (cm_state == CM_STATE_RECORDING);
}

extern void gpio_device_set_aux_outputs(uint8_t player_index, bool a1_pressed, bool a2_pressed);

// Runtime hook consumed by gpio_device.c (weak default there).
// true  => keep current behavior (left analog + D-pad both drive direction)
// false => ignore left analog for direction output (D-pad only)
bool app_gpio_use_left_analog_as_dpad(void)
{
    return cm_analog_dpad_enabled;
}

// Toggle analog-direction input with Start + Coin + B1.
// Uses rising-edge latching so holding the combo doesn't retrigger every frame.

static bool cm_process_service_combo(uint32_t buttons, uint32_t combo_mask, uint32_t* hold_start)
{
    bool combo_pressed = (buttons & combo_mask) == combo_mask;
    if (!combo_pressed) {
        *hold_start = 0;
        return false;
    }

    uint32_t now = platform_time_ms();
    if (*hold_start == 0) {
        *hold_start = now;
        return false;
    }

    return (now - *hold_start) >= CM_SERVICE_HOLD_TIME_MS;
}

static void cm_process_service_outputs(uint32_t buttons)
{
    bool menu_pressed = cm_process_service_combo(buttons, CM_MENU_COMBO_MASK, &cm_menu_combo_start);
    bool aux_pressed = cm_process_service_combo(buttons, CM_AUX_COMBO_MASK, &cm_aux_combo_start);
    gpio_device_set_aux_outputs(0, menu_pressed, aux_pressed);
}

static void cm_process_analog_toggle(uint32_t buttons)
{
    uint32_t combo_mask = JP_BUTTON_S2 | JP_BUTTON_S1 | JP_BUTTON_B1;
    bool combo_pressed = (buttons & combo_mask) == combo_mask;

    if (combo_pressed && !cm_analog_toggle_latched) {
        cm_analog_toggle_latched = true;
        cm_analog_dpad_enabled = !cm_analog_dpad_enabled;

        if (cm_analog_dpad_enabled) {
            printf("[app:usb2neogeo] Direction input: LEFT ANALOG + D-PAD ENABLED (Start+Coin+B1)\n");
            set_both_leds(0, 64, 64); // Cyan = analog mode enabled feedback
        } else {
            printf("[app:usb2neogeo] Direction input: LEFT ANALOG DISABLED, D-PAD ONLY (Start+Coin+B1)\n");
            set_both_leds(32, 0, 64); // Magenta = analog disabled mode
        }

        led_feedback_timer = platform_time_ms();
    } else if (!combo_pressed) {
        cm_analog_toggle_latched = false;
    }
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:usb2neogeo] Initializing USB2NEOGEO v%s\n", APP_VERSION);

    // Initialize external RGB LED (PWM on GP0, GP1, GP5)
    ext_rgb_led_init();

    // Set external LED to GREEN immediately — system is ready
    set_ext_led(0, 8, 0);

    // Initialize GPIO PINS, with active_high = false
    gpio_device_init_pins(gpio_gpio_config, false);

    // Configure router for USB2NEOGEO
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_GPIO] = NEOGEO_OUTPUT_PORTS,  // 1 players
        },
        .merge_all_inputs = false,  // Simple 1:1 mapping (each USB device → NEOGEO adapter)
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → NEOGEO
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_GPIO, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    uint8_t profile_count = profile_get_count(OUTPUT_TARGET_GPIO);
    const char* active_name = profile_get_name(OUTPUT_TARGET_GPIO,
                                                profile_get_active_index(OUTPUT_TARGET_GPIO));

    // Store initial profile index for change detection
    cm_last_profile_index = profile_get_active_index(OUTPUT_TARGET_GPIO);

    printf("[app:usb2neogeo] Initialization complete\n");
    printf("[app:usb2neogeo]   Routing: %s\n", "SIMPLE (USB → NEOGEO+ adapter 1:1)");
    printf("[app:usb2neogeo]   Player slots: %d (SHIFT mode - players shift on disconnect)\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2neogeo]   Profiles: %d fixed + 1 custom (active: %s)\n", profile_count, active_name ? active_name : "none");
    printf("[app:usb2neogeo]   Custom mapping: Hold Coin 2s to record B1-B6\n");
    printf("[app:usb2neogeo]   Analog direction toggle: Start+Coin+B1 (default = D-PAD ONLY)\n");
    printf("[app:usb2neogeo]   Service outputs: MENU=GP%d via Start+Coin+R1+R2 1s, AUX=GP%d via Start+Coin+Square+Cross 1s\n", P1_NEOGEO_MENU_PIN, P1_NEOGEO_AUX_PIN);
    
    printf("[app:usb2neogeo]   External RGB LED: GP%d(R) GP%d(G) GP%d(B) — GREEN = ready\n",
           EXT_LED_PIN_RED, EXT_LED_PIN_GREEN, EXT_LED_PIN_BLUE);
}

// ============================================================================
// APP TASK (called in main loop)
// ============================================================================

// External: raw button state and analog triggers from gpio_device.c tap callback
extern volatile uint32_t app_tap_last_buttons;
extern volatile uint8_t app_tap_last_l2;
extern volatile uint8_t app_tap_last_r2;
extern volatile bool app_tap_has_update;

// Timer for returning BOTH LEDs to green after recording feedback
#define LED_FEEDBACK_DURATION_MS 1500  // Show save/cancel color for 1.5s before returning to green

void app_task(void)
{
    // Process custom mapping recording with latest button state
    // We use the raw buttons from the tap callback (set in gpio_device.c)
    static uint32_t last_known_buttons = 0;
    static uint8_t last_known_l2 = 0;
    static uint8_t last_known_r2 = 0;

    if (app_tap_has_update) {
        last_known_buttons = app_tap_last_buttons;
        last_known_l2 = app_tap_last_l2;
        last_known_r2 = app_tap_last_r2;
        app_tap_has_update = false;
    }

    // Detect if the user switched to a fixed profile via Coin+D-pad combo.
    // If the active profile index changed, deactivate the custom profile.
    uint8_t current_profile_index = profile_get_active_index(OUTPUT_TARGET_GPIO);
    if (current_profile_index != cm_last_profile_index) {
        cm_last_profile_index = current_profile_index;
        if (cm_custom_active) {
            app_deactivate_custom_profile();
        }
    }

    // Synthesize L2/R2 digital bits from analog trigger values.
    // XInput controllers (Xbox) send triggers as purely analog (0-255) and
    // do NOT set JP_BUTTON_L2/R2 in the digital button bitmap. We need to
    // inject these bits so the recording logic can detect L2/R2 presses.
    // This does NOT affect normal gameplay (profile_apply handles it separately).
    uint32_t enriched_buttons = last_known_buttons;
    if (last_known_l2 >= CM_TRIGGER_THRESHOLD) {
        enriched_buttons |= JP_BUTTON_L2;
    }
    if (last_known_r2 >= CM_TRIGGER_THRESHOLD) {
        enriched_buttons |= JP_BUTTON_R2;
    }

    // Track previous recording state to detect transitions
    cm_state_t prev_state = cm_state;

    // Always process recording state machine (even without new input,
    // to handle time-based transitions like the 2s hold detection)
    cm_process_recording(enriched_buttons);

    // Process runtime toggle for analog-as-direction mode.
    // Disabled while recording so button capture behavior stays deterministic.
    if (!app_is_recording()) {
        cm_process_analog_toggle(enriched_buttons);
        cm_process_service_outputs(enriched_buttons);
    } else {
        gpio_device_set_aux_outputs(0, false, false);     
    }

    // When recording just ended (transition from RECORDING to IDLE),
    // start the feedback timer. After the timer expires, BOTH LEDs
    // return to their normal GREEN state.
    if (prev_state == CM_STATE_RECORDING && cm_state == CM_STATE_IDLE) {
        led_feedback_timer = platform_time_ms();
    }

    // After feedback duration, return BOTH LEDs to GREEN (normal operation)
    if (led_feedback_timer > 0) {
        uint32_t elapsed = platform_time_ms() - led_feedback_timer;
        if (elapsed >= LED_FEEDBACK_DURATION_MS) {
            set_both_leds(0, 8, 0);  // Return BOTH LEDs to GREEN = ready
            led_feedback_timer = 0;
        }
    }
}
