// profiles.h - N642USB App Profiles
//
// Profile definitions for N64 to USB adapter.

#ifndef N642USB_PROFILES_H
#define N642USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// PROFILE 1: DEFAULT (DC-style face buttons)
// ============================================================================
// A=B1, C-Down=B2, B=B3, C-Left=B4, C-Up=L3, C-Right=R3
// C-buttons also map to right stick

// No remapping needed - use core defaults

// ============================================================================
// PROFILE 2: DUAL STICK (C-buttons as right stick only)
// ============================================================================
// A=B1, B=B2, C-buttons=right stick only (no button output)

static const button_map_entry_t n642usb_dualstick_map[] = {
    // N64 B (B3) -> USB B (B2)
    MAP_BUTTON(JP_BUTTON_B3, JP_BUTTON_B2),
    // Remove C-button mappings (they still control right stick via analog)
    MAP_BUTTON(JP_BUTTON_B2, 0),   // C-Down -> nothing
    MAP_BUTTON(JP_BUTTON_B4, 0),   // C-Left -> nothing
    MAP_BUTTON(JP_BUTTON_L3, 0),   // C-Up -> nothing
    MAP_BUTTON(JP_BUTTON_R3, 0),   // C-Right -> nothing
};

// ============================================================================
// PROFILE DEFINITIONS
// ============================================================================

static const profile_t n642usb_profiles[] = {
    // Profile 0: Default (DC-style)
    {
        .name = "default",
        .description = "DC-style: A/B/C-Down/C-Left as face buttons",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = NULL,
        .combo_map_count = 0,
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
    // Profile 1: Dual Stick
    {
        .name = "dualstick",
        .description = "Dual stick: A/B as face, C-pad as right stick",
        .button_map = n642usb_dualstick_map,
        .button_map_count = sizeof(n642usb_dualstick_map) / sizeof(n642usb_dualstick_map[0]),
        .combo_map = NULL,
        .combo_map_count = 0,
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_set_t n642usb_profile_set = {
    .profiles = n642usb_profiles,
    .profile_count = sizeof(n642usb_profiles) / sizeof(n642usb_profiles[0]),
    .default_index = 0,
};

#endif // N642USB_PROFILES_H
