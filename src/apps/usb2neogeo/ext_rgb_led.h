// ext_rgb_led.h - External RGB LED Driver (Common Cathode)
//
// Drives an external RGB LED via PWM on three GPIO pins.
// The LED mirrors the internal WS2812 status colors during custom mapping
// recording, providing a panel-mountable visual indicator.
//
// Hardware connection:
//   GP0 → R1 (150Ω) → LED Red Anode
//   GP1 → R2 (33Ω)  → LED Green Anode
//   GP5 → R3 (22Ω)  → LED Blue Anode
//   GND              → LED Common Cathode
//
// The PWM frequency is set to ~1kHz for flicker-free operation.
// Color values use the same 0-255 range as the WS2812 LED.

#ifndef EXT_RGB_LED_H
#define EXT_RGB_LED_H

#include <stdint.h>

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// These pins must be free (not used by Neo Geo GPIO output).
// On the RP2040-Zero build, GP0, GP1, GP5 are confirmed free.

#define EXT_LED_PIN_RED   1   // GP0 → PWM0A
#define EXT_LED_PIN_GREEN 0   // GP1 → PWM0B
#define EXT_LED_PIN_BLUE  2   // GP5 → PWM2B

// ============================================================================
// API
// ============================================================================

// Initialize the external RGB LED (configure PWM on the three pins)
void ext_rgb_led_init(void);

// Set the external LED color (0-255 per channel, same scale as WS2812)
void ext_rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b);

// Turn off the external LED
void ext_rgb_led_off(void);

#endif // EXT_RGB_LED_H
