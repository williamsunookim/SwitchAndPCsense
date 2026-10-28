/*
 * SPDX-License-Identifier: MIT
 */

#include "usb_mode.h"
#include "switch_bt.h"

#include <cstdio>

#include "config.h"
#include "audio.h"
#include "effects_runtime.h"
#include "pico/time.h"
#include "switch_pro_usb.h"
#include "tusb.h"
#include "wake.h"
extern bool spk_active;

namespace {

bool save_pending = false;
uint32_t save_after_ms = 0;
UsbOutputMode active_mode = UsbOutputMode::DualSense;

void select_mode(UsbOutputMode mode) {
    active_mode = mode;
    config_set_usb_output_mode(static_cast<uint8_t>(mode));
}

} // namespace

bool usb_mode_is_switch() {
#ifdef SWITCH_BT_WIRELESS
    return false; // Both profiles keep the DualSense USB descriptor for diagnostics.
#else
    return active_mode == UsbOutputMode::SwitchPro;
#endif
}

bool usb_mode_is_wireless() {
#ifdef SWITCH_BT_WIRELESS
    return active_mode == UsbOutputMode::SwitchPro;
#else
    return false;
#endif
}

const char *usb_mode_name() {
    return usb_mode_is_wireless() ? "Switch Bluetooth" : (usb_mode_is_switch() ? "Switch Pro" : "PC DualSense");
}

void usb_mode_init() {
    active_mode = config_get_usb_output_mode() == static_cast<uint8_t>(UsbOutputMode::SwitchPro)
                      ? UsbOutputMode::SwitchPro
                      : UsbOutputMode::DualSense;
    switch_pro_usb_init();
    printf("[USB] Output mode: %s\n", usb_mode_name());
}

void usb_mode_apply_config() {
    const auto mode = config_get_usb_output_mode() == static_cast<uint8_t>(UsbOutputMode::SwitchPro)
                      ? UsbOutputMode::SwitchPro
                      : UsbOutputMode::DualSense;
    usb_mode_set(mode);
}

void usb_mode_task() {
    if (!save_pending) return;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (static_cast<int32_t>(now - save_after_ms) < 0) return;
    if (config_save()) save_pending = false;
    else save_after_ms = now + 2000;
}

void usb_mode_toggle_and_reconnect() {
    const UsbOutputMode next = active_mode == UsbOutputMode::SwitchPro
                                   ? UsbOutputMode::DualSense
                                   : UsbOutputMode::SwitchPro;
    usb_mode_set(next);
}

void usb_mode_set(UsbOutputMode mode) {
    if (mode == active_mode) return;
    effects_stop();
    set_mic_active(false);
    spk_active = false;
    wake_note_usb_reconnect();
    tud_disconnect();
    select_mode(mode);
    switch_bt_mode_changed();
    switch_pro_usb_reset();
    save_pending = !config_save();
    if (save_pending) save_after_ms = to_ms_since_boot(get_absolute_time()) + 2000;

    printf("[USB] Switching output mode to %s\n", usb_mode_name());
    sleep_ms(150);
    tud_connect();
}
