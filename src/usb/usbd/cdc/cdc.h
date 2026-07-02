// cdc.h - USB CDC (Virtual Serial Port) interface
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Single CDC data channel for commands, config, responses, and debug log streaming.

#ifndef CDC_H
#define CDC_H

#include <stdint.h>
#include <stdbool.h>

// CDC port index
#define CDC_PORT_DATA   0

// Initialize CDC subsystem
void cdc_init(void);

// Process CDC tasks (call from main loop)
void cdc_task(void);

// ============================================================================
// DATA PORT (CDC 0) - Commands and responses
// ============================================================================

// Check if data port is connected
bool cdc_data_connected(void);

// Check bytes available to read
uint32_t cdc_data_available(void);

// Read from data port (returns bytes read)
uint32_t cdc_data_read(uint8_t* buffer, uint32_t bufsize);

// Read single byte (-1 if none available)
int32_t cdc_data_read_byte(void);

// Write to data port (returns bytes written)
uint32_t cdc_data_write(const uint8_t* buffer, uint32_t bufsize);

// Write string to data port
uint32_t cdc_data_write_str(const char* str);

// Flush data port
void cdc_data_flush(void);

#endif // CDC_H
