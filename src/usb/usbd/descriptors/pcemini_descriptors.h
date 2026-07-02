// pcemini_descriptors.h - PC Engine Mini controller descriptors
// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
// SPDX-FileCopyrightText: Copyright (c) 2024 Robert Dale Smith
//
// PC Engine Mini (TurboGrafx-16 Mini) USB controller emulation.
// VID/PID: 0F0D:0138 (HORI CO.,LTD. / PCEngine PAD)
// Simple 4-button digital controller with D-pad (hat switch).

#ifndef PCEMINI_DESCRIPTORS_H
#define PCEMINI_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// USB IDENTIFIERS
// ============================================================================

#define PCEMINI_VID           0x0F0D  // HORI CO.,LTD.
#define PCEMINI_PID           0x0138  // PCEngine PAD
#define PCEMINI_BCD           0x0209  // v2.09
#define PCEMINI_MANUFACTURER  "HORI CO.,LTD."
#define PCEMINI_PRODUCT       "PCEngine PAD"

#define PCEMINI_ENDPOINT_SIZE 64

// ============================================================================
// BUTTON MASKS (16-bit report, bits 0-13 active)
// ============================================================================

#define PCEMINI_MASK_I        (1U <<  2)  // Button I
#define PCEMINI_MASK_II       (1U <<  1)  // Button II
#define PCEMINI_MASK_SELECT   (1U <<  8)  // Select
#define PCEMINI_MASK_RUN      (1U <<  9)  // Run

// ============================================================================
// HAT SWITCH VALUES
// ============================================================================

#define PCEMINI_HAT_UP        0x00
#define PCEMINI_HAT_UPRIGHT   0x01
#define PCEMINI_HAT_RIGHT     0x02
#define PCEMINI_HAT_DOWNRIGHT 0x03
#define PCEMINI_HAT_DOWN      0x04
#define PCEMINI_HAT_DOWNLEFT  0x05
#define PCEMINI_HAT_LEFT      0x06
#define PCEMINI_HAT_UPLEFT    0x07
#define PCEMINI_HAT_NOTHING   0x0F

// ============================================================================
// REPORT STRUCTURE (8 bytes - GP2040-CE compatible)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint16_t buttons;   // 14 buttons (bits 0-13), 2 padding bits
    uint8_t  hat;       // D-pad hat switch
    uint8_t  const0;    // Always 0x80 (LX center)
    uint8_t  const1;    // Always 0x80 (LY center)
    uint8_t  const2;    // Always 0x80 (RX center)
    uint8_t  const3;    // Always 0x80 (RY center)
    uint8_t  const4;    // Always 0x00 (padding)
} pcemini_in_report_t;

// Helper to initialize report to neutral state
static inline void pcemini_init_report(pcemini_in_report_t* report) {
    report->buttons = 0;
    report->hat     = PCEMINI_HAT_NOTHING;
    report->const0  = 0x80;
    report->const1  = 0x80;
    report->const2  = 0x80;
    report->const3  = 0x80;
    report->const4  = 0x00;
}

// ============================================================================
// DEVICE DESCRIPTOR
// ============================================================================

static const tusb_desc_device_t pcemini_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0x00,    // Use class info in Interface Descriptors
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = PCEMINI_VID,
    .idProduct          = PCEMINI_PID,
    .bcdDevice          = PCEMINI_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,    // No serial number
    .bNumConfigurations = 0x01
};

// ============================================================================
// HID REPORT DESCRIPTOR (94 bytes - from GP2040-CE capture)
// ============================================================================

static const uint8_t pcemini_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x35, 0x00,        //   Physical Minimum (0)
    0x45, 0x01,        //   Physical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0E,        //   Report Count (14)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x0E,        //   Usage Maximum (Button 14)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x01,        //   Input (Const) - 2 padding bits
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x25, 0x07,        //   Logical Maximum (7)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x65, 0x14,        //   Unit (Degrees)
    0x09, 0x39,        //   Usage (Hat switch)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)
    0x65, 0x00,        //   Unit (None)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Const) - 4 padding bits
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x46, 0xFF, 0x00,  //   Physical Maximum (255)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Const) - 1 byte padding
    0x0A, 0x4F, 0x48,  //   Usage (0x484F)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x08,        //   Report Count (8)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)
    0x0A, 0x4F, 0x48,  //   Usage (0x484F)
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0xC0,              // End Collection
};

// ============================================================================
// CONFIGURATION DESCRIPTOR (41 bytes - 2 endpoints IN + OUT)
// ============================================================================

static const uint8_t pcemini_config_descriptor[] = {
    // Configuration descriptor (9 bytes)
    0x09,                           // bLength
    TUSB_DESC_CONFIGURATION,        // bDescriptorType
    0x29, 0x00,                     // wTotalLength (41)
    0x01,                           // bNumInterfaces
    0x01,                           // bConfigurationValue
    0x00,                           // iConfiguration
    0x80,                           // bmAttributes (Bus Powered)
    0x32,                           // bMaxPower (100mA)

    // Interface descriptor (9 bytes)
    0x09,                           // bLength
    TUSB_DESC_INTERFACE,            // bDescriptorType
    0x00,                           // bInterfaceNumber
    0x00,                           // bAlternateSetting
    0x02,                           // bNumEndpoints
    TUSB_CLASS_HID,                 // bInterfaceClass
    0x00,                           // bInterfaceSubClass
    0x00,                           // bInterfaceProtocol
    0x00,                           // iInterface

    // HID descriptor (9 bytes)
    0x09,                           // bLength
    HID_DESC_TYPE_HID,              // bDescriptorType
    U16_TO_U8S_LE(0x0111),          // bcdHID (1.11)
    0x00,                           // bCountryCode
    0x01,                           // bNumDescriptors
    HID_DESC_TYPE_REPORT,           // bDescriptorType[0]
    U16_TO_U8S_LE(sizeof(pcemini_report_descriptor)), // wDescriptorLength[0]

    // Endpoint descriptor (OUT - 7 bytes)
    0x07,                           // bLength
    TUSB_DESC_ENDPOINT,             // bDescriptorType
    0x02,                           // bEndpointAddress (EP2 OUT)
    TUSB_XFER_INTERRUPT,            // bmAttributes
    U16_TO_U8S_LE(PCEMINI_ENDPOINT_SIZE), // wMaxPacketSize
    0x05,                           // bInterval (5ms)

    // Endpoint descriptor (IN - 7 bytes)
    0x07,                           // bLength
    TUSB_DESC_ENDPOINT,             // bDescriptorType
    0x81,                           // bEndpointAddress (EP1 IN)
    TUSB_XFER_INTERRUPT,            // bmAttributes
    U16_TO_U8S_LE(PCEMINI_ENDPOINT_SIZE), // wMaxPacketSize
    0x05,                           // bInterval (5ms)
};

#endif // PCEMINI_DESCRIPTORS_H
