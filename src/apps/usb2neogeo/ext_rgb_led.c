// ext_rgb_led.c - External RGB LED Driver (Common Cathode)
//
// Uses RP2040 hardware PWM to drive an external RGB LED.
// Each color channel is driven by a separate GPIO pin with PWM.
//
// The PWM is configured with a wrap value of 255, so the duty cycle
// maps directly to the 0-255 color value (same scale as WS2812).
//
// For common-cathode LEDs:
//   HIGH = LED ON (current flows from GPIO through resistor and LED to GND)
//   LOW  = LED OFF
//
// PWM duty cycle directly controls brightness:
//   0   = fully off
//   255 = fully on

#include "ext_rgb_led.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <stdio.h>

// PWM slice and channel for each color
static uint slice_red, slice_green, slice_blue;
static uint chan_red, chan_green, chan_blue;
static bool ext_led_initialized = false;

void ext_rgb_led_init(void)
{
    // Configure each pin for PWM function
    gpio_set_function(EXT_LED_PIN_RED,   GPIO_FUNC_PWM);
    gpio_set_function(EXT_LED_PIN_GREEN, GPIO_FUNC_PWM);
    gpio_set_function(EXT_LED_PIN_BLUE,  GPIO_FUNC_PWM);

    // Get PWM slice and channel for each pin
    slice_red   = pwm_gpio_to_slice_num(EXT_LED_PIN_RED);
    chan_red     = pwm_gpio_to_channel(EXT_LED_PIN_RED);

    slice_green = pwm_gpio_to_slice_num(EXT_LED_PIN_GREEN);
    chan_green   = pwm_gpio_to_channel(EXT_LED_PIN_GREEN);

    slice_blue  = pwm_gpio_to_slice_num(EXT_LED_PIN_BLUE);
    chan_blue    = pwm_gpio_to_channel(EXT_LED_PIN_BLUE);

    // Configure PWM: wrap at 255 for direct 8-bit color mapping
    // At 125MHz system clock with divider 488.28:
    //   freq = 125MHz / (488.28 * 256) ≈ 1kHz
    // But we'll use a simpler approach: divider = 490, wrap = 255
    // This gives ~1kHz which is flicker-free for LEDs.

    // Configure Red PWM slice
    pwm_config cfg_red = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg_red, 490.0f);
    pwm_config_set_wrap(&cfg_red, 255);
    pwm_init(slice_red, &cfg_red, true);
    pwm_set_chan_level(slice_red, chan_red, 0);

    // Configure Green PWM slice
    // Note: if Red and Green share the same slice (GP0=PWM0A, GP1=PWM0B),
    // we only need to init the slice once. But calling pwm_init twice on the
    // same slice with the same config is harmless.
    pwm_config cfg_green = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg_green, 490.0f);
    pwm_config_set_wrap(&cfg_green, 255);
    pwm_init(slice_green, &cfg_green, true);
    pwm_set_chan_level(slice_green, chan_green, 0);

    // Configure Blue PWM slice
    pwm_config cfg_blue = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg_blue, 490.0f);
    pwm_config_set_wrap(&cfg_blue, 255);
    pwm_init(slice_blue, &cfg_blue, true);
    pwm_set_chan_level(slice_blue, chan_blue, 0);

    ext_led_initialized = true;

    printf("[ext_rgb_led] Initialized: R=GP%d, G=GP%d, B=GP%d\n",
           EXT_LED_PIN_RED, EXT_LED_PIN_GREEN, EXT_LED_PIN_BLUE);
}

void ext_rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!ext_led_initialized) return;

    // Set PWM duty cycle for each channel
    // For common-cathode: higher duty = brighter
    pwm_set_chan_level(slice_red,   chan_red,   r);
    pwm_set_chan_level(slice_green, chan_green,  g);
    pwm_set_chan_level(slice_blue,  chan_blue,   b);
}

void ext_rgb_led_off(void)
{
    if (!ext_led_initialized) return;

    pwm_set_chan_level(slice_red,   chan_red,   0);
    pwm_set_chan_level(slice_green, chan_green,  0);
    pwm_set_chan_level(slice_blue,  chan_blue,   0);
}
