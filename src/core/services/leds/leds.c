// leds.c - LED Subsystem
//
// Unified LED control for status indication.

#include "leds.h"
#include "neopixel/ws2812.h"
#include "core/services/players/manager.h"

static int connected_devices = 0;

void leds_init(void)
{
    neopixel_init();
}

void leds_set_connected_devices(int count)
{
    connected_devices = count;
}

void leds_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    neopixel_set_override_color(r, g, b);
}

void leds_task(void)
{
    int count = playersCount > connected_devices ? playersCount : connected_devices;
    neopixel_task(count);
}

void leds_indicate_profile(uint8_t profile_index)
{
    neopixel_indicate_profile(profile_index);
}

bool leds_is_indicating(void)
{
    return neopixel_is_indicating();
}
