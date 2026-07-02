// profiles.h - SNES2USB App Profiles
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Profile definitions for SNES2USB adapter.

#ifndef SNES2USB_PROFILES_H
#define SNES2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// DEFAULT PROFILE
// ============================================================================

// S1+S2 combos (Home, d-pad mode) are handled in snes_host.c so all
// SNES-input apps get them automatically.

static const profile_t snes2usb_profiles[] = {
    {
        .name = "default",
        .description = "Standard passthrough",
        .button_map = NULL,
        .button_map_count = 0,
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

static const profile_set_t snes2usb_profile_set = {
    .profiles = snes2usb_profiles,
    .profile_count = sizeof(snes2usb_profiles) / sizeof(snes2usb_profiles[0]),
    .default_index = 0,
};

#endif // SNES2USB_PROFILES_H
