// SPDX-License-Identifier: MIT
#pragma once

// Call only when pairing, pairing-clear confirmation and low-battery warning
// do not own the onboard LED. One pulse = PC USB; two = Switch Bluetooth.
void mode_led_task();
