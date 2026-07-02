// hid_mode.c - Generic HID gamepad USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/hid_descriptors.h"
#include "core/buttons.h"
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static joypad_hid_report_t hid_report;

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

// Convert Joypad buttons to HID gamepad buttons (18 buttons)
static uint32_t convert_buttons(uint32_t buttons)
{
    uint32_t hid_buttons = 0;

    if (buttons & JP_BUTTON_B1) hid_buttons |= USB_GAMEPAD_MASK_B1;
    if (buttons & JP_BUTTON_B2) hid_buttons |= USB_GAMEPAD_MASK_B2;
    if (buttons & JP_BUTTON_B3) hid_buttons |= USB_GAMEPAD_MASK_B3;
    if (buttons & JP_BUTTON_B4) hid_buttons |= USB_GAMEPAD_MASK_B4;
    if (buttons & JP_BUTTON_L1) hid_buttons |= USB_GAMEPAD_MASK_L1;
    if (buttons & JP_BUTTON_R1) hid_buttons |= USB_GAMEPAD_MASK_R1;
    if (buttons & JP_BUTTON_L2) hid_buttons |= USB_GAMEPAD_MASK_L2;
    if (buttons & JP_BUTTON_R2) hid_buttons |= USB_GAMEPAD_MASK_R2;
    if (buttons & JP_BUTTON_S1) hid_buttons |= USB_GAMEPAD_MASK_S1;
    if (buttons & JP_BUTTON_S2) hid_buttons |= USB_GAMEPAD_MASK_S2;
    if (buttons & JP_BUTTON_L3) hid_buttons |= USB_GAMEPAD_MASK_L3;
    if (buttons & JP_BUTTON_R3) hid_buttons |= USB_GAMEPAD_MASK_R3;
    if (buttons & JP_BUTTON_A1) hid_buttons |= USB_GAMEPAD_MASK_A1;
    if (buttons & JP_BUTTON_A2) hid_buttons |= USB_GAMEPAD_MASK_A2;
    if (buttons & JP_BUTTON_A3) hid_buttons |= USB_GAMEPAD_MASK_A3;
    if (buttons & JP_BUTTON_A4) hid_buttons |= USB_GAMEPAD_MASK_A4;
    if (buttons & JP_BUTTON_L4) hid_buttons |= USB_GAMEPAD_MASK_L4;
    if (buttons & JP_BUTTON_R4) hid_buttons |= USB_GAMEPAD_MASK_R4;

    return hid_buttons;
}

// Convert Joypad dpad to HID hat switch
static uint8_t convert_dpad_to_hat(uint32_t buttons)
{
    uint8_t up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    if (up && right) return HID_HAT_UP_RIGHT;
    if (up && left) return HID_HAT_UP_LEFT;
    if (down && right) return HID_HAT_DOWN_RIGHT;
    if (down && left) return HID_HAT_DOWN_LEFT;
    if (up) return HID_HAT_UP;
    if (down) return HID_HAT_DOWN;
    if (left) return HID_HAT_LEFT;
    if (right) return HID_HAT_RIGHT;

    return HID_HAT_CENTER;
}

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void hid_mode_init(void)
{
    memset(&hid_report, 0, sizeof(hid_report));
    // Set neutral analog values
    hid_report.lx = 128;
    hid_report.ly = 128;
    hid_report.rx = 128;
    hid_report.ry = 128;
    hid_report.hat = HID_HAT_CENTER;
}

static bool hid_mode_is_ready(void)
{
    return tud_hid_ready();
}

static bool hid_mode_send_report(uint8_t player_index,
                                  const input_event_t* event,
                                  const profile_output_t* profile_out,
                                  uint32_t buttons)
{
    (void)player_index;
    (void)event;

    // Convert buttons to HID format (18 buttons across 3 bytes)
    uint32_t hid_buttons = convert_buttons(buttons);
    hid_report.buttons_lo = hid_buttons & 0xFF;
    hid_report.buttons_mid = (hid_buttons >> 8) & 0xFF;
    hid_report.buttons_hi = (hid_buttons >> 16) & 0x03;
    hid_report.hat = convert_dpad_to_hat(buttons);

    // Analog sticks (HID convention: 0=up, 255=down)
    hid_report.lx = profile_out->left_x;
    hid_report.ly = profile_out->left_y;
    hid_report.rx = profile_out->right_x;
    hid_report.ry = profile_out->right_y;

    // Analog triggers
    hid_report.lt = profile_out->l2_analog;
    hid_report.rt = profile_out->r2_analog;

    // PS3 pressure axes (0x00 = released, 0xFF = fully pressed)
    hid_report.pressure_dpad_right = (buttons & JP_BUTTON_DR) ? 0xFF : 0x00;
    hid_report.pressure_dpad_left  = (buttons & JP_BUTTON_DL) ? 0xFF : 0x00;
    hid_report.pressure_dpad_up    = (buttons & JP_BUTTON_DU) ? 0xFF : 0x00;
    hid_report.pressure_dpad_down  = (buttons & JP_BUTTON_DD) ? 0xFF : 0x00;
    hid_report.pressure_triangle   = (hid_buttons & USB_GAMEPAD_MASK_B4) ? 0xFF : 0x00;
    hid_report.pressure_circle     = (hid_buttons & USB_GAMEPAD_MASK_B2) ? 0xFF : 0x00;
    hid_report.pressure_cross      = (hid_buttons & USB_GAMEPAD_MASK_B1) ? 0xFF : 0x00;
    hid_report.pressure_square     = (hid_buttons & USB_GAMEPAD_MASK_B3) ? 0xFF : 0x00;
    hid_report.pressure_l1         = (hid_buttons & USB_GAMEPAD_MASK_L1) ? 0xFF : 0x00;
    hid_report.pressure_r1         = (hid_buttons & USB_GAMEPAD_MASK_R1) ? 0xFF : 0x00;
    hid_report.pressure_l2         = profile_out->l2_analog;
    hid_report.pressure_r2         = profile_out->r2_analog;

    return tud_hid_report(0, &hid_report, sizeof(hid_report));
}

static const uint8_t* hid_mode_get_report_descriptor(void)
{
    return hid_report_descriptor;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

// Note: Device and config descriptors are still in usbd.c due to CDC coupling
// They will be passed via usbd_get_hid_device_descriptor() etc.

const usbd_mode_t hid_mode = {
    .name = "DInput",
    .mode = USB_OUTPUT_MODE_HID,

    // Descriptors - using extern functions from usbd.c for now
    .get_device_descriptor = NULL,  // Will use usbd.c's desc_device_hid
    .get_config_descriptor = NULL,  // Will use usbd.c's desc_configuration_hid
    .get_report_descriptor = hid_mode_get_report_descriptor,

    .init = hid_mode_init,
    .send_report = hid_mode_send_report,
    .is_ready = hid_mode_is_ready,

    // No feedback support for generic HID
    .handle_output = NULL,
    .get_rumble = NULL,
    .get_feedback = NULL,
    .get_report = NULL,
    .get_class_driver = NULL,
    .task = NULL,
};
