// ble_beacon.h - BLE Non-Connectable Beacon for WiFi SSID Discovery
// SPDX-License-Identifier: Apache-2.0
//
// Broadcasts the WiFi SSID via BLE advertisement so iOS apps can discover
// nearby Joypad dongles without needing to scan WiFi networks (which iOS
// doesn't allow).
//
// Uses non-connectable advertising (ADV_NONCONN_IND) so the device:
// - Does NOT appear in iOS Bluetooth settings
// - Cannot be paired with
// - Only visible to apps scanning for our service UUID

#ifndef BLE_BEACON_H
#define BLE_BEACON_H

#include <stdint.h>
#include <stdbool.h>

// Joypad BLE Service UUID (16-bit for compactness)
// Using a custom UUID in the 0xFFxx range (vendor-specific)
#define JOYPAD_BLE_SERVICE_UUID 0xFF10

// Initialize BLE beacon with the WiFi SSID to advertise
// Call this AFTER wifi_transport_init() since CYW43 must be initialized first
// Returns true on success
bool ble_beacon_init(const char* ssid);

// Start advertising (call after init)
void ble_beacon_start(void);

// Stop advertising
void ble_beacon_stop(void);

// Check if advertising is active
bool ble_beacon_is_active(void);

// Process BLE stack (call from main loop)
void ble_beacon_task(void);

// Deinitialize beacon
void ble_beacon_deinit(void);

#endif // BLE_BEACON_H
