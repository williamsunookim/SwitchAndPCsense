//
// Created by awalol on 2026/3/4.
//

#include <cstdio>
#include <cstring>
#include <algorithm>
#include "bsp/board_api.h"
#include "bt.h"
#include "button_functions.h"
#include "utils.h"
#include "resample.h"
#include "audio.h"
#include "wake.h"
#ifdef ENABLE_WAKE_HID
#include "ps_shortcut.h"
#endif
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "state_mgr.h"
#include "effects_runtime.h"
#include "resource_benchmark.h"
#include "motion_calibration.h"
#if ENABLE_SERIAL
#include "pico/stdio_usb.h"
#endif
#include "config.h"
#include "cmd.h"
#include "dse.h"
#include "dualsense_parser.h"
#include "switch_pro_usb.h"
#include "switch_bt.h"
#include "usb_mode.h"
#if ENABLE_BATT_LED
#include "battery_led.h"
#endif

// Pico SDK speciifically for waiting on conditions
#include "pico/critical_section.h"

static MotionCalibration motion_calibration;
// Reset cause for diagnostics: [0]=1 if watchdog, [1]=stuck main-loop stage,
// [2..3]=watchdog resets since power-on (scratch[1] survives soft resets).
uint8_t boot_diag[4]{};
static void boot_diag_capture(bool by_watchdog) {
    const uint32_t stage = watchdog_hw->scratch[0];
    uint32_t count = (watchdog_hw->scratch[2] == 0x57444354u) ? watchdog_hw->scratch[1] : 0;
    if (by_watchdog) ++count;
    watchdog_hw->scratch[1] = count; watchdog_hw->scratch[2] = 0x57444354u;
    boot_diag[0] = by_watchdog ? 1 : 0;
    boot_diag[1] = (stage >> 16) == 0x5744u ? static_cast<uint8_t>(stage) : 0;
    boot_diag[2] = static_cast<uint8_t>(count); boot_diag[3] = static_cast<uint8_t>(count >> 8);
    watchdog_hw->scratch[0] = 0;
}
int reportSeqCounter = 0;
uint8_t packetCounter = 0;
bool spk_active = false;

uint8_t interrupt_in_data[63] = {
    0x7f, 0x7d, 0x7f, 0x7e, 0x00, 0x00, 0xa7,
    0x08, 0x00, 0x00, 0x00, 0x52, 0x43, 0x30, 0x41,
    0x01, 0x00, 0x0e, 0x00, 0xef, 0xff, 0x03, 0x03,
    0x7b, 0x1b, 0x18, 0xf0, 0xcc, 0x9c, 0x60, 0x00,
    0xfc, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xa7, 0xad, 0x60, 0x00, 0x29, 0x18, 0x00,
    0x53, 0x9f, 0x28, 0x35, 0xa5, 0xa8, 0x0c, 0x8b
};

critical_section_t report_cs;
volatile bool report_dirty = false;

void __not_in_flash_func(interrupt_loop)() {
    if (switch_bt_enabled()) return; // Wireless profile exposes USB diagnostics only.
    if (usb_mode_is_switch()) {
        switch_pro_usb_task();
        return;
    }
    if (!tud_hid_ready()) return;

    // TODO: Refactor for better code reuse
    if (get_config().polling_rate_mode != 2) {
        if (!tud_hid_report(0x01, interrupt_in_data, 63)) {
            printf("[USBHID] tud_hid_report error\n");
        }
        return;
    }

    bool should_send = false;
    // Local buffer to hold the report data while we prepare it to send. 
    uint8_t safe_report[63];


    critical_section_enter_blocking(&report_cs);
    if (report_dirty) {
        memcpy(safe_report, interrupt_in_data, 63);
        report_dirty = false;
        should_send = true;
    }
    critical_section_exit(&report_cs);

    // Only send to TinyUSB if we actually grabbed fresh data
    if (should_send) {
        if (!tud_hid_report(0x01, safe_report, 63)) {
            printf("[USBHID] tud_hid_report error\n");

            // If the report failed to queue, restore the dirty flag 
            // so we try again on the next loop iteration.
            critical_section_enter_blocking(&report_cs);
            report_dirty = true;
            critical_section_exit(&report_cs);
        }
    }
}

void __not_in_flash_func(on_bt_data)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    // A disconnect notification carries the base project's synthetic report.
    // Replace it with a fully neutral input, including IMU and touch contacts.
    if (channel == INTERRUPT && !bt_is_connected()) { motion_calibration.reset();bridge_neutral_input(); return; }
    if (channel==CONTROL && len>=42 && data[0]==0xa3 && data[1]==5) motion_calibration.load_factory(data+1,len-1);
    // printf("[Main] BT data callback: channel=%u len=%u\n", channel, len);
    if (channel == INTERRUPT && len >= 66 && data[1] == 0x31 && !(data[2] & 2)) {
        ControllerState input{};
        if (dualsense_parse_input(data + 3, len - 3, input)) effects_input(input);
    }
    if (channel == INTERRUPT && len > 2 && data[1] == 0x31) {
        if (usb_mode_is_switch() || switch_bt_enabled()) {
            if (len < 66 || ((data[2] >> 1) & 1)) return;
            ControllerState state{};
            if (dualsense_parse_input(data + 3, len - 3, state)) {
                memcpy(interrupt_in_data, data + 3, sizeof(interrupt_in_data));
                motion_calibration.apply(state,to_ms_since_boot(get_absolute_time()));
                if (switch_bt_enabled()) switch_bt_update(state);
                else switch_pro_usb_update(state);
#if ENABLE_BATT_LED
                battery_led_note_report();
#endif
            }
            return;
        }
        if (len < 66) return;
        // Mic audio: controller signals mic payload via bit1 of data[2];
        // the opus-encoded mic frame starts at data+4.
        if ((data[2] >> 1) & 1) {
            if (len >= 4) {
                mic_add_queue(data + 4, len - 4);
            }
            return;
        }
        if ((data[56] & 1) != (interrupt_in_data[53] & 1)) {
            set_headset(data[56] & 1);
        }

        // Wake-on-PS must observe every BT input report regardless of polling
        // mode: the wake feature has its own state to maintain (button-byte
        // diff for edge detection) and short-circuiting it on non-2 polling
        // modes silently breaks wake while the host is suspended.
        wake_on_bt_input(data + 3, len - 3);
        #ifdef ENABLE_WAKE_HID
        ps_shortcut_tick(data + 3, len - 3);
        #endif

        if (get_config().polling_rate_mode != 2) {
            memcpy(interrupt_in_data, data + 3, 63);
#if ENABLE_BATT_LED
            battery_led_note_report();
#endif
            return;
        }

        // We add the critical section here to avoid any race conditions when writing to the interrupt_in_data buffer,
        // which is shared between the main loop and this callback.
        // The critical section ensures that only one thread can access the buffer at a time,
        // preventing data corruption and ensuring thread safety.
        // We also set the report_dirty flag to true to indicate that new data is available
        //  and needs to be sent in the next interrupt report.
        critical_section_enter_blocking(&report_cs);
        memcpy(interrupt_in_data, data + 3, 63);
        report_dirty = true;
        critical_section_exit(&report_cs);
#if ENABLE_BATT_LED
        battery_led_note_report();
#endif
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    if (usb_mode_is_switch()) {
        return switch_pro_usb_get_report(report_id, report_type, buffer, reqlen);
    }
#ifdef ENABLE_WAKE_HID
    if (itf == 1) {
        if (reqlen >= 8) {
            memset(buffer, 0, 8);
            return 8;
        }
        return 0;
    }
#endif
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    if (is_pico_cmd(report_id)) {
        return pico_cmd_get(report_id, buffer, reqlen);
    }

    // DSE profiles: while the unlock + prefetch is still in progress, return 0
    // (NAK) for profile reads so the PS app retries rather than caching an
    // empty snapshot. Still kick off the background BT fetch.
    if (dse_is_profile_report(report_id) && !dse_profiles_ready()) {
        get_feature_data(report_id, reqlen);
        return 0;
    }

    std::vector<uint8_t> feature_data = get_feature_data(report_id, reqlen);
    if (!feature_data.empty()) {
        memcpy(buffer, feature_data.data() + 1, feature_data.size() - 1);
    }

    return feature_data.empty() ? 0 : feature_data.size() - 1;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;
    uint8_t const itf = tu_u16_low(p_request->wIndex); // wInterface
    uint8_t const alt = tu_u16_low(p_request->wValue); // bAlternateSetting

    if (itf == 1) {
        printf("[AUDIO] Set interface Speaker to alternate setting %d\n", alt);
        spk_active = alt;
    }
    if (itf == 2) { // ITF_NUM_AUDIO_STREAMING_IN (microphone)
        printf("[AUDIO] Set interface Microphone to alternate setting %d\n", alt);
        set_mic_active(alt);
    }

    return true;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    if (buffer == nullptr || bufsize == 0) return;
    if (usb_mode_is_switch()) {
        switch_pro_usb_set_report(report_id, report_type, buffer, bufsize);
        return;
    }
#ifdef ENABLE_WAKE_HID
    if (itf == 1) {
        // Drop keyboard SET_REPORT (host LED state).
        return;
    }
#endif
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;

    if (is_pico_cmd(report_id)) {
#if ENABLE_VERBOSE
        printf("[HID] Receive 0xf6 setting config, funcid:0x%02X\n", buffer[0]);
#endif
        pico_cmd_set(report_id, buffer, bufsize);
        return;
    }

    if (switch_bt_enabled()) return; // PC USB cannot own the wireless console outputs.

    // Normalize control SET_REPORT (ID separate) and interrupt OUT (ID in buffer).
    if (report_id == 0x02) {
        if (bufsize < 47) return;
        uint8_t packet[64]{}; packet[0] = 0x02;
        const auto count = std::min<size_t>(bufsize, sizeof(packet) - 1);
        std::memcpy(packet + 1, buffer, count);
        tud_hid_set_report_cb(itf, 0, HID_REPORT_TYPE_OUTPUT, packet, count + 1);
        return;
    }
    // INTERRUPT OUT
    if (report_id == 0 && bufsize >= 48) {
        switch (buffer[0]) {
            case 0x02: {
                state_update(buffer + 1, bufsize - 1);
                bool send_now = ((buffer[1] >> 1) & 1) || // UseRumbleNotHaptics
                                ((buffer[39] >> 3) & 1); // UseRumbleNotHaptics2
                if (!send_now && spk_active) {
                    break;
                }
                uint8_t outputData[78]{};
                outputData[0] = 0x31;
                outputData[1] = reportSeqCounter << 4;
                if (++reportSeqCounter == 256) {
                    reportSeqCounter = 0;
                }
                outputData[2] = 0x10;
                // memcpy(outputData + 3, buffer + 1, bufsize - 1);
                state_set(outputData + 3, sizeof(SetStateData));
                bt_write(outputData, sizeof(outputData));
                break;
            }
        }
    }
    if (report_id == 0x80 ||
        // DSE: Write Profile Block
        report_id == 0x60 ||
        report_id == 0x62 ||
        report_id == 0x61) {
        set_feature_data(report_id, const_cast<uint8_t *>(buffer), bufsize);
    }
}

void bridge_neutral_input() {
    std::memset(interrupt_in_data, 0, sizeof(interrupt_in_data));
    interrupt_in_data[0] = interrupt_in_data[1] = interrupt_in_data[2] = interrupt_in_data[3] = 128;
    interrupt_in_data[7] = 8;
    interrupt_in_data[32] = interrupt_in_data[36] = 0x80;
    report_dirty = true;
    switch_pro_usb_neutral();
    switch_bt_neutral();
}

int main() {
#if SYS_CLOCK_KHZ > 133000 && !PICO_RP2350
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(1000);
#endif
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);

    board_init();
    // USB descriptors depend on the persisted profile, so load it before
    // TinyUSB can answer the host's first enumeration request.
    config_load();
    usb_mode_init();
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
// Keep USB enumerated with neutral inputs while waiting for Bluetooth.
    board_init_after_tusb();
#if ENABLE_SERIAL
    stdio_usb_init();
#endif

    if (cyw43_arch_init()) {
        printf("Failed to initialize CYW43\n");
        return 1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

#if ENABLE_BATT_LED
    battery_led_init();
#endif

    boot_diag_capture(watchdog_caused_reboot());
#if !ENABLE_SERIAL
    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
        // 当崩溃重启以后，闪三下灯
        for (int i = 0; i < 6; i++) {
            if (i % 2 == 0) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            } else {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
            }
            sleep_ms(500);
        }
    } else {
        printf("Clean boot\n");
    }
#endif

    // Initialize the critical section for the report buffer
    critical_section_init(&report_cs);
    wake_init();

    bt_init();
    bt_register_data_callback(on_bt_data);

    audio_init();
    state_init();
    bridge_neutral_input();
    resource_benchmark_init();

#if !ENABLE_SERIAL
    watchdog_enable(1000, true);
#endif

    // Watchdog scratch registers survive a watchdog reset: STAGE() records the
    // main-loop step, so after a hang reboot boot_diag reports where it stuck.
#define STAGE(n) (watchdog_hw->scratch[0] = 0x57440000u | (n))
    while (1) {
#if !ENABLE_SERIAL
        watchdog_update();
#endif
        STAGE(1); cyw43_arch_poll();
        STAGE(2); tud_task();
        STAGE(3); switch_bt_task();
        if (!usb_mode_is_switch() && !switch_bt_enabled()) {
            STAGE(4); wake_task();
            if (tud_mounted() && !tud_suspended()) audio_loop();
        }
        STAGE(5); effects_task();
        STAGE(6); interrupt_loop();
#if ENABLE_BATT_LED
        STAGE(7); battery_led_tick();
#endif
        STAGE(8); button_check();
        STAGE(9); usb_mode_task();
        STAGE(10); bt_inquiring_led();
        STAGE(11); dse_task();
    }
}
