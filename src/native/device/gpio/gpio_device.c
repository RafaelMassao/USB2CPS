// gpio_device.c

#include "gpio_device.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"
#include "hardware/structs/sio.h"

#if CFG_TUSB_DEBUG >= 1
#include "hardware/uart.h"
#endif

#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/profiles/profile_indicator.h"
#include "core/services/codes/codes.h"

// ============================================================================
// CUSTOM MAPPING SUPPORT
// ============================================================================
// External functions from app.c for custom profile support

// Returns the custom profile if active, or NULL if not
extern const profile_t* app_get_custom_profile(void);

// Check if recording mode is active (to suppress normal combo detection)
extern bool app_is_recording(void);
// Optional app hook: allow runtime disabling of left-analog -> D-pad conversion.
// Default behavior keeps analog-to-D-pad enabled for all existing apps.
__attribute__((weak)) bool app_gpio_use_left_analog_as_dpad(void) {
  return true;
}

// ============================================================================
// INTERNAL STATE
// ============================================================================

static gpio_device_port_t gpio_ports[GPIO_MAX_PLAYERS];
static bool initialized = false;

// ============================================================================
// PROFILE SYSTEM (Delegates to core profile service)
// ============================================================================

static uint8_t gpio_get_player_count_for_profile(void) {
    return router_get_player_count(OUTPUT_TARGET_GPIO);
}

static uint8_t gpio_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_GPIO);
}

static uint8_t gpio_get_active_profile(void) {
    return profile_get_active_index(OUTPUT_TARGET_GPIO);
}

static void gpio_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_GPIO, index);
}

static const char* gpio_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_GPIO, index);
}

// ============================================================================
// Internal GPIO Functions
// ============================================================================

// Initialize GPIO pins
static void gpioport_gpio_init(bool active_high)
{
    uint32_t gpio_mask = 0;

    for (int i = 0; i < GPIO_MAX_PLAYERS; i++) {
      gpio_device_port_t* port = &gpio_ports[i];
      gpio_mask |= port->gpio_mask;
    }

    gpio_init_mask(gpio_mask);
    gpio_clr_mask(gpio_mask);
    for (int i = 0; i < 30; i++) {
        if (gpio_mask & (1u << i)) {
            gpio_disable_pulls(i);
        }
    }
    
    if (active_high) {
      gpio_set_dir_out_masked(gpio_mask);
    } else {
      gpio_set_dir_in_masked(gpio_mask);
    }
    
}

// ============================================================================
// GPIO PORT Functions
// ============================================================================
void gpioport_init(gpio_device_port_t* port, gpio_device_config_t* config, bool active_high)
{
    port->active_high = active_high;
    port->gpio_mask = 0;

    // Pin Mask
    port->mask_du = GPIO_MASK(config->pin_du);
    port->mask_dd = GPIO_MASK(config->pin_dd);
    port->mask_dr = GPIO_MASK(config->pin_dr);
    port->mask_dl = GPIO_MASK(config->pin_dl);
    port->mask_b1 = GPIO_MASK(config->pin_b1);
    port->mask_b2 = GPIO_MASK(config->pin_b2);
    port->mask_b3 = GPIO_MASK(config->pin_b3);
    port->mask_b4 = GPIO_MASK(config->pin_b4);
    port->mask_l1 = GPIO_MASK(config->pin_l1);
    port->mask_r1 = GPIO_MASK(config->pin_r1);
    port->mask_l2 = GPIO_MASK(config->pin_l2);
    port->mask_r2 = GPIO_MASK(config->pin_r2);
    port->mask_s1 = GPIO_MASK(config->pin_s1);
    port->mask_s2 = GPIO_MASK(config->pin_s2);
    port->mask_a1 = GPIO_MASK(config->pin_a1);
    port->mask_a2 = GPIO_MASK(config->pin_a2);
    port->mask_l3 = GPIO_MASK(config->pin_l3);
    port->mask_r3 = GPIO_MASK(config->pin_r3);
    port->mask_l4 = GPIO_MASK(config->pin_l4);
    port->mask_r4 = GPIO_MASK(config->pin_r4);

    port->gpio_mask = (port->mask_du | port->mask_dd | port->mask_dr | port->mask_dl |
                       port->mask_b1 | port->mask_b2 | port->mask_b3 | port->mask_b4 |
                       port->mask_l1 | port->mask_r1 | port->mask_l2 | port->mask_r2 |
                       port->mask_s1 | port->mask_s2 | port->mask_a1 | port->mask_a2 |
                       port->mask_l3 | port->mask_r3 | port->mask_l4 | port->mask_r4);

    port->last_read  = 0;
}

// ============================================================================
// PUSH-BASED OUTPUT VIA ROUTER TAP
// ============================================================================
// GPIO updates happen immediately when input arrives via router tap callback,
// eliminating the one-loop-iteration polling delay. The tap fires from within
// router_submit_input() on the same iteration input is received.

// Last raw button state from tap — used by task loop for combo detection
static volatile uint32_t tap_last_buttons = 0;
static volatile bool tap_has_update = false;

// Exported to app.c for custom mapping recording
volatile uint32_t app_tap_last_buttons = 0;
volatile uint8_t app_tap_last_l2 = 0;
volatile uint8_t app_tap_last_r2 = 0;
volatile bool app_tap_has_update = false;

// Cache last GPIO mask written per player to avoid redundant register writes.
static uint32_t gpio_last_buttons[GPIO_MAX_PLAYERS] = {0};
static bool gpio_has_last_buttons[GPIO_MAX_PLAYERS] = {0};

// Cache last tap input signature per player to skip remap work on idle reports.
typedef struct {
  uint32_t buttons;
  uint8_t lx;
  uint8_t ly;
  uint8_t rx;
  uint8_t ry;
  uint8_t l2;
  uint8_t r2;
  uint8_t rz;
  const profile_t* profile;
  bool use_left_analog_as_dpad;
} gpio_tap_signature_t;

static gpio_tap_signature_t tap_last_signature[GPIO_MAX_PLAYERS];
static bool tap_has_signature[GPIO_MAX_PLAYERS] = {0};

// Tap callback — fires immediately from router_submit_input().
// Must be fast: just apply profile + update GPIO. No printf or blocking.
static void __not_in_flash_func(gpio_tap_callback)(output_target_t output,
                                                      uint8_t player_index,
                                                      const input_event_t* event)
{
  (void)output;
  
  if (player_index >= GPIO_MAX_PLAYERS) return;

  // Store raw buttons for combo detection in task loop
  tap_last_buttons = event->buttons;
  tap_has_update = true;

  // Also export to app.c for custom mapping recording
  app_tap_last_buttons = event->buttons;
  app_tap_last_l2 = event->analog[ANALOG_L2];
  app_tap_last_r2 = event->analog[ANALOG_R2];
  app_tap_has_update = true;

  // Only update GPIO if we have connected players
  if (playersCount == 0) return;

  // If recording mode is active, suppress GPIO output for action buttons
  // (only pass through D-pad so user can still see directional feedback)
  if (app_is_recording()) {
    // During recording, don't update GPIO with action buttons
    // This prevents accidental game inputs while mapping
    return;
  }

  // Determine which profile to use:
  // 1. If custom profile is active, use it
  // 2. Otherwise, use the standard active profile
  const profile_t* custom = app_get_custom_profile();
  const profile_t* profile;
  bool using_custom = (custom != NULL);
  if (using_custom) {
    profile = custom;
  } else {
    profile = profile_get_active(OUTPUT_TARGET_GPIO);
  }

  bool use_left_analog_as_dpad = app_gpio_use_left_analog_as_dpad();
  const uint8_t* analog = event->analog;

  // Fast path: ignore fixed-interval idle reports when neither input state,
  // profile selection, nor analog->dpad setting changed.
  if (tap_has_signature[player_index]) {
    const gpio_tap_signature_t* last = &tap_last_signature[player_index];
    if (last->buttons == event->buttons &&
        last->lx == analog[ANALOG_LX] &&
        last->ly == analog[ANALOG_LY] &&
        last->rx == analog[ANALOG_RX] &&
        last->ry == analog[ANALOG_RY] &&
        last->l2 == analog[ANALOG_L2] &&
        last->r2 == analog[ANALOG_R2] &&
        last->rz == analog[ANALOG_RZ] &&
        last->profile == profile &&
        last->use_left_analog_as_dpad == use_left_analog_as_dpad) {
      return;
    }
  }

  tap_last_signature[player_index] = (gpio_tap_signature_t){
    .buttons = event->buttons,
    .lx = analog[ANALOG_LX],
    .ly = analog[ANALOG_LY],
    .rx = analog[ANALOG_RX],
    .ry = analog[ANALOG_RY],
    .l2 = analog[ANALOG_L2],
    .r2 = analog[ANALOG_R2],
    .rz = analog[ANALOG_RZ],
    .profile = profile,
    .use_left_analog_as_dpad = use_left_analog_as_dpad,
  };
  tap_has_signature[player_index] = true;

  // ---- L2/R2 CROSS-SYNTHESIS ----
  // Different controller types handle L2/R2 differently:
  //   - XInput (Xbox): triggers are PURELY ANALOG (0-255), digital bits never set
  //   - HID generic:   triggers are PURELY DIGITAL (button 7/8), analog always 0
  //   - DualShock 4:   triggers are BOTH digital AND analog
  //
  // The profile system uses a threshold on the ANALOG value to set the digital bit.
  // This breaks HID controllers because: threshold clears digital bit, then checks
  // analog (which is 0) → bit never restored.
  //
  // Solution: bidirectional synthesis BEFORE profile_apply:
  //   1. If digital bit is set but analog is 0 → set analog to 255 (HID fix)
  //   2. If analog >= 64 but digital bit is not set → set digital bit (XInput fix)
  // This ensures profile_apply's threshold logic works for ALL controller types.

  uint32_t buttons_for_apply = event->buttons;
  uint8_t l2_analog = analog[ANALOG_L2];
  uint8_t r2_analog = analog[ANALOG_R2];

  // L2: digital→analog synthesis (HID controllers)
  if ((buttons_for_apply & JP_BUTTON_L2) && l2_analog == 0) {
    l2_analog = 255;  // Digital is pressed, inject full analog value
  }
  // L2: analog→digital synthesis (XInput controllers)
  if (l2_analog >= 64) {
    buttons_for_apply |= JP_BUTTON_L2;
  }

  // R2: digital→analog synthesis (HID controllers)
  if ((buttons_for_apply & JP_BUTTON_R2) && r2_analog == 0) {
    r2_analog = 255;  // Digital is pressed, inject full analog value
  }
  // R2: analog→digital synthesis (XInput controllers)
  if (r2_analog >= 64) {
    buttons_for_apply |= JP_BUTTON_R2;
  }

  // Apply profile remapping (now with consistent digital+analog for all controllers)
  profile_output_t mapped;
  profile_apply(profile, buttons_for_apply,
                analog[ANALOG_LX], analog[ANALOG_LY],
                analog[ANALOG_RX], analog[ANALOG_RY],
                l2_analog, r2_analog,
                analog[ANALOG_RZ],
                &mapped);

  const gpio_device_port_t* port = &gpio_ports[player_index];
  uint32_t gpio_buttons = 0;

  // Mapping the buttons (active-low: 0 = pressed)
  gpio_buttons |= (mapped.buttons & JP_BUTTON_S2) ? port->mask_s2 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_S1) ? port->mask_s1 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_DD) ? port->mask_dd : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_DL) ? port->mask_dl : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_DU) ? port->mask_du : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_DR) ? port->mask_dr : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_B1) ? port->mask_b1 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_B2) ? port->mask_b2 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_B3) ? port->mask_b3 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_B4) ? port->mask_b4 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_L1) ? port->mask_l1 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_R1) ? port->mask_r1 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_L2) ? port->mask_l2 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_R2) ? port->mask_r2 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_L3) ? port->mask_l3 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_R3) ? port->mask_r3 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_L4) ? port->mask_l4 : 0;
  gpio_buttons |= (mapped.buttons & JP_BUTTON_R4) ? port->mask_r4 : 0;
  // D-pad from left analog stick (threshold at 64/192 from center 128)
  // HID convention: 0=up, 128=center, 255=down
  if (use_left_analog_as_dpad) {
    gpio_buttons |= (mapped.left_x < 64)  ? port->mask_dl : 0;  // Dpad Left
    gpio_buttons |= (mapped.left_x > 192) ? port->mask_dr : 0;  // Dpad Right
    gpio_buttons |= (mapped.left_y < 64)  ? port->mask_du : 0;  // Dpad Up
    gpio_buttons |= (mapped.left_y > 192) ? port->mask_dd : 0;  // Dpad Down
  }

  // Most USB controllers report at fixed intervals even when idle.
  // Skip GPIO register writes when output state is unchanged.
  if (gpio_has_last_buttons[player_index] && gpio_last_buttons[player_index] == gpio_buttons) {
    return;
  }

  gpio_last_buttons[player_index] = gpio_buttons;
  gpio_has_last_buttons[player_index] = true;


  if (port->active_high) {
    gpio_put_masked(port->gpio_mask, gpio_buttons);
  } else {
    sio_hw->gpio_oe_set = gpio_buttons;
    sio_hw->gpio_oe_clr = port->gpio_mask & (~gpio_buttons);
  }
}

// init for GPIO communication
void gpio_device_init()
{ 
  profile_indicator_disable_rumble();
  profile_set_player_count_callback(gpio_get_player_count_for_profile);

  // Register exclusive tap for push-based GPIO updates — fires immediately from
  // router_submit_input() instead of waiting for next task loop iteration.
  // Exclusive: router skips storing to router_outputs[] since we never poll.
  router_set_tap_exclusive(OUTPUT_TARGET_GPIO, gpio_tap_callback);

  #if CFG_TUSB_DEBUG >= 1
  // Initialize chosen UART
  uart_init(UART_ID, BAUD_RATE);

  // Set the GPIO function for the UART pins
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  #endif
}

// 
void gpio_device_init_pins(gpio_device_config_t* config, bool active_high){
  for (int i = 0; i < GPIO_MAX_PLAYERS; i++) {
    gpio_device_port_t* port = &gpio_ports[i];
    gpio_device_config_t* port_config = &config[i];
    gpioport_init(port, port_config, active_high);
    gpio_has_last_buttons[i] = false;
    gpio_last_buttons[i] = 0;
    tap_has_signature[i] = false;
  }
  gpioport_gpio_init(active_high);
  initialized = true;
}

// Task loop — handles non-latency-critical work (combo detection, cheat codes).
// GPIO updates are now handled by the tap callback above.
void gpio_device_task()
{
  static uint32_t last_buttons = 0;
  bool had_update = false;

  // Pick up raw button state from tap callback
  if (tap_has_update) {
    last_buttons = tap_last_buttons;
    tap_has_update = false;
    had_update = true;
  }

  // Don't process profile switch combo while recording custom mapping
  // This prevents the Coin hold from also triggering profile cycling
  if (!app_is_recording()) {
    // Always check profile switching combo with last known state
    // This ensures combo detection works even when controller doesn't send updates while buttons held
    if (playersCount > 0) {
      profile_check_switch_combo(last_buttons);
    }
  }

  // Run cheat code detection when we had new input
  if (had_update && playersCount > 0) {
    codes_process_raw(last_buttons);
  }
}

//

//-----------------------------------------------------------------------------
// Core1 Entry Point
//-----------------------------------------------------------------------------

void __not_in_flash_func(core1_task)(void) {
  while (1) {
    sleep_ms(100);
  }
}

// Input flow: USB drivers → router_submit_input() → tap callback → GPIO (immediate)
//             Task loop handles combo detection and cheat codes

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface gpio_output_interface = {
    .name = "GPIO",
    .target = OUTPUT_TARGET_GPIO,
    .init = gpio_device_init,
    .core1_task = NULL,
    .task = gpio_device_task,  // GPIO needs periodic scan detection task
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = gpio_get_profile_count,
    .get_active_profile = gpio_get_active_profile,
    .set_active_profile = gpio_set_active_profile,
    .get_profile_name = gpio_get_profile_name,
    .get_trigger_threshold = NULL,
};
