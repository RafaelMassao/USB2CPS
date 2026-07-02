// tud_gc_adapter.h - TinyUSB GameCube Adapter class driver
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Custom USB device class driver implementing GameCube Adapter protocol.
// Emulates Wii U/Switch GameCube Adapter (VID 057E, PID 0337).

#ifndef TUD_GC_ADAPTER_H
#define TUD_GC_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "descriptors/gc_adapter_descriptors.h"

// ============================================================================
// GC ADAPTER CONFIGURATION
// ============================================================================

#ifndef CFG_TUD_GC_ADAPTER
#define CFG_TUD_GC_ADAPTER 0
#endif

#ifndef CFG_TUD_GC_ADAPTER_EP_BUFSIZE
#define CFG_TUD_GC_ADAPTER_EP_BUFSIZE 37
#endif

// ============================================================================
// GC ADAPTER API
// ============================================================================

// Check if GC adapter device is ready to send a report
bool tud_gc_adapter_ready(void);

// Send controller input report (37 bytes for all 4 ports)
// Returns true if transfer was queued successfully
bool tud_gc_adapter_send_report(const gc_adapter_in_report_t* report);

// Get rumble output command (5 bytes)
// Call this to retrieve the latest rumble values from host
// Returns true if rumble data is available
bool tud_gc_adapter_get_rumble(gc_adapter_out_report_t* rumble);

// ============================================================================
// CLASS DRIVER (internal)
// ============================================================================

// Get the GC adapter class driver for registration
const usbd_class_driver_t* tud_gc_adapter_class_driver(void);

#endif // TUD_GC_ADAPTER_H
