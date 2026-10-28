// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

// Addresses use display order (AA:BB:CC:DD:EE:FF), not HCI wire order.
struct Switch2WakeSettings {
    uint8_t source[6]{};
    uint8_t target[6]{};
    uint16_t product = 0;
};
static_assert(sizeof(Switch2WakeSettings) == 14);
bool switch2_wake_valid(const Switch2WakeSettings &settings);
void switch2_wake_payload(const Switch2WakeSettings &settings, uint8_t out[31]);

// Radio functions run on core0's polled BTstack loop only.
void switch2_wake_init();
void switch2_wake_ready();
void switch2_wake_task(bool console_connected);
void switch2_wake_input(bool home);
void switch2_wake_neutral();
void switch2_wake_cancel();
bool switch2_wake_request();
void switch2_wake_status(uint8_t out[24]);
void switch2_wake_hci(uint8_t *packet, uint16_t size);
bool switch2_wake_configure(const Switch2WakeSettings &settings);
