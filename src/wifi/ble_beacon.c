// ble_beacon.c - BLE Non-Connectable Beacon for WiFi SSID Discovery
// SPDX-License-Identifier: Apache-2.0
//
// Broadcasts WiFi SSID via BLE so iOS apps can discover Joypad dongles.
// Uses BTstack's GAP LE API for non-connectable advertising.

#include "ble_beacon.h"

#include <stdio.h>
#include <string.h>

// Pico W CYW43 includes
#include "pico/cyw43_arch.h"
#include "pico/btstack_cyw43.h"

// BTstack includes
#include "btstack.h"
#include "btstack_run_loop.h"

// ============================================================================
// STATE
// ============================================================================

static bool initialized = false;
static bool advertising = false;
static char beacon_ssid[32];

// Advertisement data buffer (max 31 bytes)
static uint8_t adv_data[31];
static uint8_t adv_data_len = 0;

// ============================================================================
// ADVERTISEMENT DATA BUILDER
// ============================================================================

// Build advertisement data containing:
// - Flags (LE General Discoverable, BR/EDR not supported)
// - 16-bit Service UUID (JOYPAD_BLE_SERVICE_UUID)
// - Complete Local Name (the WiFi SSID)
static void build_adv_data(const char* ssid)
{
    uint8_t* p = adv_data;
    uint8_t ssid_len = strlen(ssid);

    // Limit SSID to fit in advertisement (31 - 3 flags - 4 uuid = 24 max)
    if (ssid_len > 24) ssid_len = 24;

    // Flags: LE General Discoverable Mode, BR/EDR Not Supported
    *p++ = 2;           // Length
    *p++ = 0x01;        // Type: Flags
    *p++ = 0x06;        // LE General Discoverable + BR/EDR Not Supported

    // 16-bit Service UUID
    *p++ = 3;           // Length
    *p++ = 0x03;        // Type: Complete List of 16-bit Service UUIDs
    *p++ = JOYPAD_BLE_SERVICE_UUID & 0xFF;         // UUID low byte
    *p++ = (JOYPAD_BLE_SERVICE_UUID >> 8) & 0xFF;  // UUID high byte

    // Complete Local Name (the WiFi SSID)
    *p++ = ssid_len + 1;    // Length
    *p++ = 0x09;            // Type: Complete Local Name
    memcpy(p, ssid, ssid_len);
    p += ssid_len;

    adv_data_len = p - adv_data;
}

// ============================================================================
// BTstack PACKET HANDLER
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[ble_beacon] BTstack ready\n");
                if (advertising) {
                    // Re-start advertising if it was requested before BTstack was ready
                    ble_beacon_start();
                }
            }
            break;

        case HCI_EVENT_LE_META:
            // Could handle advertising events here if needed
            break;

        default:
            break;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool ble_beacon_init(const char* ssid)
{
    if (initialized) {
        printf("[ble_beacon] Already initialized\n");
        return true;
    }

    if (!ssid || strlen(ssid) == 0) {
        printf("[ble_beacon] ERROR: Invalid SSID\n");
        return false;
    }

    // Save SSID
    strncpy(beacon_ssid, ssid, sizeof(beacon_ssid) - 1);
    beacon_ssid[sizeof(beacon_ssid) - 1] = '\0';

    printf("[ble_beacon] Initializing beacon for SSID: %s\n", beacon_ssid);

    // Build advertisement data
    build_adv_data(beacon_ssid);
    printf("[ble_beacon] Advertisement data: %d bytes\n", adv_data_len);

    // Initialize BTstack with CYW43 (WiFi should already be initialized)
    // Note: cyw43_arch_init() was already called by wifi_transport_init()
    // We just need to initialize BTstack on top of it
    async_context_t *context = cyw43_arch_async_context();
    if (!btstack_cyw43_init(context)) {
        printf("[ble_beacon] ERROR: Failed to initialize BTstack\n");
        return false;
    }
    printf("[ble_beacon] BTstack initialized\n");

    // Register packet handler
    static btstack_packet_callback_registration_t hci_event_callback_registration;
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    initialized = true;

    // Power on Bluetooth
    hci_power_control(HCI_POWER_ON);

    return true;
}

void ble_beacon_start(void)
{
    if (!initialized) {
        printf("[ble_beacon] Not initialized\n");
        return;
    }

    // Check if BTstack is ready
    if (hci_get_state() != HCI_STATE_WORKING) {
        printf("[ble_beacon] BTstack not ready, will start when ready\n");
        advertising = true;  // Mark that we want to advertise
        return;
    }

    printf("[ble_beacon] Starting non-connectable advertising\n");

    // Configure advertisement parameters for non-connectable advertising
    uint16_t adv_interval_min = 160;  // 100ms (160 * 0.625ms)
    uint16_t adv_interval_max = 320;  // 200ms (320 * 0.625ms)
    uint8_t adv_type = 3;             // ADV_NONCONN_IND (non-connectable, non-scannable)
    bd_addr_t null_addr = {0};

    gap_advertisements_set_params(adv_interval_min, adv_interval_max, adv_type,
                                   0, null_addr, 0x07, 0x00);

    // Set advertisement data
    gap_advertisements_set_data(adv_data_len, adv_data);

    // Enable advertising
    gap_advertisements_enable(1);

    advertising = true;
    printf("[ble_beacon] Advertising SSID: %s\n", beacon_ssid);
}

void ble_beacon_stop(void)
{
    if (!initialized || !advertising) return;

    printf("[ble_beacon] Stopping advertising\n");

    gap_advertisements_enable(0);
    advertising = false;
}

bool ble_beacon_is_active(void)
{
    return advertising && (hci_get_state() == HCI_STATE_WORKING);
}

void ble_beacon_task(void)
{
    if (!initialized) return;

    // BTstack processing is handled by cyw43_arch_poll() which is called
    // by wifi_transport_task(), so we don't need to do anything here.
    // This function exists for future expansion if needed.
}

void ble_beacon_deinit(void)
{
    if (!initialized) return;

    ble_beacon_stop();

    hci_power_control(HCI_POWER_OFF);

    // Note: btstack_cyw43 doesn't have a deinit function
    // CYW43 will be deinitialized by wifi_transport_deinit()

    initialized = false;
    printf("[ble_beacon] Deinitialized\n");
}
