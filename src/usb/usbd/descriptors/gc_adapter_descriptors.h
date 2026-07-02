// gc_adapter_descriptors.h - GameCube Adapter USB descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Nintendo GameCube Controller Adapter compatible descriptors.
// Emulates Wii U/Switch GameCube Adapter (VID 057E, PID 0337).
// Supports up to 4 controllers via single USB interface.
//
// The real adapter uses HID class with a custom report descriptor.
// Report IDs: 0x11=rumble(5B), 0x13=init(1B), 0x21=input(37B)

#ifndef GC_ADAPTER_DESCRIPTORS_H
#define GC_ADAPTER_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// GC ADAPTER USB IDENTIFIERS
// ============================================================================

// Nintendo GameCube Adapter for Wii U/Switch
#define GC_ADAPTER_VID          0x057E  // Nintendo
#define GC_ADAPTER_PID          0x0337  // GameCube Adapter
#define GC_ADAPTER_BCD_DEVICE   0x0100  // v1.0

// ============================================================================
// GC ADAPTER PROTOCOL CONSTANTS
// ============================================================================

// Report IDs
#define GC_ADAPTER_REPORT_ID_RUMBLE  0x11  // Rumble output command (5 bytes)
#define GC_ADAPTER_REPORT_ID_INIT    0x13  // Init command from host (1 byte)
#define GC_ADAPTER_REPORT_ID_INPUT   0x21  // Controller input report (37 bytes)

// Port status byte (bitmask)
// Real adapter has 2 USB cables: black (data) + gray (rumble power)
// - Bit 2 (0x04): Rumble available (gray USB connected on real hardware)
// - Bit 4 (0x10): Controller connected to this port
#define GC_ADAPTER_STATUS_RUMBLE        0x04  // Rumble available
#define GC_ADAPTER_STATUS_CONTROLLER    0x10  // Controller connected

// Combined status values
#define GC_ADAPTER_STATUS_NONE          0x00  // No controller, no rumble
#define GC_ADAPTER_STATUS_CONNECTED     (GC_ADAPTER_STATUS_CONTROLLER | GC_ADAPTER_STATUS_RUMBLE)  // 0x14

// Legacy defines (kept for compatibility but not used in new format)
#define GC_ADAPTER_PORT_NONE         0x00
#define GC_ADAPTER_PORT_WIRED        0x10
#define GC_ADAPTER_TYPE_NONE         0x00
#define GC_ADAPTER_TYPE_NORMAL       0x01

// Report sizes (excluding report ID)
#define GC_ADAPTER_INPUT_SIZE        37    // 1 byte report_id + 36 bytes data
#define GC_ADAPTER_RUMBLE_SIZE       5     // 1 byte report_id + 4 bytes rumble state
#define GC_ADAPTER_INIT_SIZE         1     // 1 byte report_id only

// ============================================================================
// GC ADAPTER REPORT STRUCTURES
// ============================================================================

// Per-port input report (9 bytes)
typedef struct __attribute__((packed)) {
    // Byte 0: Status bitmask (see GC_ADAPTER_STATUS_* defines)
    // - Bit 2 (0x04): Gray USB connected
    // - Bit 4 (0x10): Controller connected to this port
    uint8_t status;

    // Byte 1: Buttons (A, B, X, Y, D-pad)
    struct {
        uint8_t a : 1;
        uint8_t b : 1;
        uint8_t x : 1;
        uint8_t y : 1;
        uint8_t dpad_left : 1;
        uint8_t dpad_right : 1;
        uint8_t dpad_down : 1;
        uint8_t dpad_up : 1;
    };

    // Byte 2: Buttons (Start, Z, R, L)
    // Bits 4-7 are unused in original GC protocol
    struct {
        uint8_t start : 1;
        uint8_t z : 1;
        uint8_t r : 1;
        uint8_t l : 1;
        uint8_t : 4;          // Reserved (bits 4-7)
    };

    // Bytes 3-8: Analog axes
    uint8_t stick_x;     // Main stick X (0-255, 128=center)
    uint8_t stick_y;     // Main stick Y (0-255, 128=center)
    uint8_t cstick_x;    // C-stick X (0-255, 128=center)
    uint8_t cstick_y;    // C-stick Y (0-255, 128=center)
    uint8_t trigger_l;   // L trigger analog (0-255)
    uint8_t trigger_r;   // R trigger analog (0-255)
} gc_adapter_port_t;

_Static_assert(sizeof(gc_adapter_port_t) == 9, "gc_adapter_port_t must be 9 bytes");

// Full adapter input report (37 bytes)
typedef struct __attribute__((packed)) {
    uint8_t report_id;           // Always 0x21
    gc_adapter_port_t port[4];   // 4 controller ports
} gc_adapter_in_report_t;

_Static_assert(sizeof(gc_adapter_in_report_t) == 37, "gc_adapter_in_report_t must be 37 bytes");

// Rumble output command (5 bytes)
typedef struct __attribute__((packed)) {
    uint8_t report_id;   // Always 0x11
    uint8_t rumble[4];   // Per-port rumble state (0=off, 1=on)
} gc_adapter_out_report_t;

_Static_assert(sizeof(gc_adapter_out_report_t) == 5, "gc_adapter_out_report_t must be 5 bytes");

// ============================================================================
// GC ADAPTER HID REPORT DESCRIPTOR
// ============================================================================

// HID Report Descriptor for GC Adapter
// Describes full 4-port input report (36 bytes) for Web HID compatibility
// Each port: 1 status + 2 buttons + 6 analog = 9 bytes × 4 ports = 36 bytes
static const uint8_t gc_adapter_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // Report ID 0x11: Rumble Output (5 bytes: report_id + 4 port rumble states)
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x11,        //     Report ID (17)
    0x19, 0x00,        //     Usage Minimum (Undefined)
    0x2A, 0xFF, 0x00,  //     Usage Maximum (0xFF)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x05,        //     Report Count (5)
    0x91, 0x00,        //     Output (Data,Array,Abs)
    0xC0,              //   End Collection

    // Report ID 0x21: Input Report (36 bytes: 4 ports × 9 bytes each)
    // Per-port format: [status] [buttons_lo] [buttons_hi] [lx] [ly] [rx] [ry] [lt] [rt]
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x21,        //     Report ID (33)
    0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        //     Usage (Vendor Usage 1)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x24,        //     Report Count (36) - 4 ports × 9 bytes
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0xC0,              //   End Collection

    // Report ID 0x13: Init Command (1 byte: just report_id)
    0xA1, 0x03,        //   Collection (Report)
    0x85, 0x13,        //     Report ID (19)
    0x19, 0x00,        //     Usage Minimum (Undefined)
    0x2A, 0xFF, 0x00,  //     Usage Maximum (0xFF)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x00,        //     Output (Data,Array,Abs)
    0xC0,              //   End Collection

    0xC0,              // End Collection
};

// ============================================================================
// GC ADAPTER USB DESCRIPTORS
// ============================================================================

// Device descriptor - HID class
static const tusb_desc_device_t gc_adapter_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0x00,    // Use class from interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = GC_ADAPTER_VID,
    .idProduct          = GC_ADAPTER_PID,
    .bcdDevice          = GC_ADAPTER_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,
    .bNumConfigurations = 0x01
};

// Configuration descriptor length
// Config (9) + Interface (9) + HID (9) + EP IN (7) + EP OUT (7) = 41
#define GC_ADAPTER_CONFIG_TOTAL_LEN  41

// Configuration descriptor - HID with IN and OUT endpoints
// Match HOJA's endpoint addresses and sizes: EP IN 0x82 (37 bytes), EP OUT 0x01 (6 bytes)
static const uint8_t gc_adapter_config_descriptor[] = {
    // Configuration descriptor (9 bytes)
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, GC_ADAPTER_CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED, 500),  // 500mA

    // Interface descriptor (9 bytes)
    9, TUSB_DESC_INTERFACE, 0x00, 0x00, 0x02, TUSB_CLASS_HID, 0x00, 0x00, 0x00,

    // HID descriptor (9 bytes)
    9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0110), 0, 1, HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(sizeof(gc_adapter_report_descriptor)),

    // Endpoint IN descriptor (7 bytes) - 37 bytes for input report
    7, TUSB_DESC_ENDPOINT, 0x82, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(37), 1,

    // Endpoint OUT descriptor (7 bytes) - 6 bytes for rumble (matches HOJA)
    7, TUSB_DESC_ENDPOINT, 0x01, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(6), 1,
};

// String descriptors
#define GC_ADAPTER_MANUFACTURER  "Nintendo Co., Ltd."
#define GC_ADAPTER_PRODUCT       "WUP-028"  // Wii U GameCube Adapter product code

#endif // GC_ADAPTER_DESCRIPTORS_H
