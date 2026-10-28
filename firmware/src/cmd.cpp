//
// Created by awalol on 2026/5/4.
//

#include "cmd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "bt.h"
#include "switch_bt.h"
#include "config.h"
#include "device/usbd.h"
#include "pico/time.h"
#include "audio.h"
#include "wake.h"
#include "effects_runtime.h"
#include "resource_benchmark.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"

// spk_active (main.cpp) + audio_mic_active() (audio.cpp) are surfaced in the
// 0xf9 feature report so the config UI can display the real gated mic/speaker
// state, reflecting the disable_mic / disable_speaker settings.
extern bool spk_active;
static uint8_t motion_page = 0;
extern uint8_t boot_diag[4];

bool is_pico_cmd(uint8_t report_id) {
    if (report_id == 0xf6 ||
        report_id == 0xf7 ||
        report_id == 0xf8 ||
        report_id == 0xf9
    ) {
        return true;
    }
    return false;
}

uint16_t pico_cmd_get(uint8_t report_id, uint8_t *buffer, uint16_t reqlen) {
    if (report_id == 0xf6) {
        const auto len = std::min<size_t>(reqlen, sizeof(TriggerSettings));
        std::memcpy(buffer, &effects_settings(), len);
        if(switch_bt_enabled() && reqlen>=63) {switch_bt_trace(buffer+37);return 63;}
        return len;
    }
    if (report_id == 0xf7) {
        printf("[HID] Receive 0xf7 getting config\n");
        if (sizeof(Config_body) > reqlen) {
            printf("[Config] Warning: Config_body overflow\n");
        }
        const auto len = std::min(sizeof(Config_body),static_cast<size_t>(reqlen));
        memcpy(buffer,&get_config(),len);
        return len;
    }
    if (report_id == 0xf8) {
        printf("[HID] Receive 0xf8 getting firmware version\n");
        const auto len = std::min(strlen(PICO_PROGRAM_VERSION_STRING), static_cast<size_t>(reqlen));
        memcpy(buffer, PICO_PROGRAM_VERSION_STRING, len);
        return len;
    }
    // F6 func 0x30 selects this page: Switch motion diagnostics, readable from
    // the PC USB side while the console is linked over Bluetooth.
    // Page 4: DualSense sensor-clock spacing histogram.
    if (report_id == 0xf9 && motion_page == 4 && reqlen >= 25) {
        buffer[0] = 'G'; switch_bt_gap_diagnostics(buffer + 1); return 25;
    }
    // Pages 2/3: wireless event history, newest 7 / next 7.
    if (report_id == 0xf9 && motion_page >= 2 && reqlen >= 59) {
        switch_bt_event_page(buffer, (motion_page - 2) * 7u);
        return 59;
    }
    if (report_id == 0xf9 && motion_page == 1 && reqlen >= 55) {
        switch_bt_motion_diagnostics(buffer);
        if (reqlen >= 59) { std::memcpy(buffer + 55, boot_diag, 4); return 59; }
        return 55;
    }
    if (report_id == 0xf9) {
        // [-128,0]
        int8_t rssi = 0;
        bt_get_signal_strength(&rssi);
        if (reqlen == 0) {
            return 0;
        }
        buffer[0] = rssi;
        // byte 1: real audio gating state, for the config UI to display.
        //   bit7 = valid marker (firmware without this byte leaves it 0)
        //   bit0 = controller mic actually streaming (host opened it AND !disable_mic)
        //   bit1 = controller speaker actually driven (host opened it AND !disable_speaker)
        if (reqlen >= 2) {
            uint8_t flags = 0x80;
#if !DISABLE_SPEAKER_PROC
            if (audio_mic_active() && !get_config().disable_mic) flags |= 0x01;
            if (spk_active && !get_config().disable_speaker) flags |= 0x02;
#endif
            buffer[1] = flags;
            if (reqlen >= 14) {
                effects_diagnostics(buffer + 2);
                if (reqlen >= 42) { resource_diagnostics(buffer + 14);
                    if (reqlen>=62) {switch_bt_diagnostics(buffer+42);return 62;}
                    return 42; }
                return 14;
            }
            return 2;
        }
#if ENABLE_VERBOSE
        printf("[HID] 0xf9 RSSI=%d raw=0x%02X\n", rssi, buffer[0]);
#endif
        return 1;
    }
    return 0;
}

void pico_cmd_set(uint8_t report_id, uint8_t const *buffer, uint16_t bufsize) {
    (void) report_id;
    if (bufsize == 0) {
        return;
    }

    if (report_id == 0xf6 && buffer[0] == 0x20 && bufsize >= 1 + sizeof(TriggerSettings)) {
        TriggerSettings s; std::memcpy(&s, buffer + 1, sizeof(s)); effects_configure(s); return;
    }
    if (report_id == 0xf6 && buffer[0] == 0x30) {motion_page = buffer[1];return;}
    if (report_id == 0xf6 && buffer[0] == 0x21) {switch_bt_pair();return;}
    if (report_id == 0xf6 && buffer[0] == 0x22) {if(!bt_is_connected())bt_bootsel_click_action();return;}
    if (report_id == 0xf6 && buffer[0] == 0x04) {
        effects_stop();
        const auto until = time_us_64() + 50000;
        while (time_us_64() < until) cyw43_arch_poll();
        reset_usb_boot(0,0);
    }
    // 0x01 update config in variable
    // 0x02 write config to flash
    // 0x03 reconnect tinyusb device;
    if (buffer[0] == 0x01) {
#if ENABLE_VERBOSE
        printf("[CMD] Enter config set func\n");
#endif
        set_config(buffer + 1, bufsize - 1);
    }
    if (buffer[0] == 0x02) {
        printf("[CMD] Enter config save func\n");
        config_save();
    }
    if (buffer[0] == 0x03) {
        printf("[CMD] Enter tud reconnect func\n");
        effects_stop();
        wake_note_usb_reconnect();   // this disconnect is intentional, not a host sleep
        tud_disconnect();
        sleep_ms(150);
        tud_connect();
    }
}
