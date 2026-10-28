// SPDX-License-Identifier: MIT
#include "effects_runtime.h"
#include <algorithm>
#include <cstring>
#include "audio.h"
#include "bt.h"
#include "config.h"
#include "pico/time.h"
#include "state_mgr.h"
#include "switch_rumble_adapter.h"
#include "switch_pro_usb.h"
#include "tusb.h"
#include "usb_mode.h"
#include "switch_bt.h"
#include "utils.h"
extern int reportSeqCounter;
namespace {
TriggerEngine engine;
uint32_t last_input = 0, last_send = 0, chord_start = 0;
bool have_input = false, live = false, chord_fired = false, chord_down = false;
bool save_pending = false;
uint32_t save_at = 0, safety_stops = 0;
TriggerEffect sent_left{}, sent_right{};
uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }
}
const TriggerSettings &effects_settings() { return engine.settings(); }
void effects_load(const TriggerSettings &s) { engine.configure(s); }
bool effects_configure(const TriggerSettings &s) {
    if (!trigger_settings_valid(s)) return false;
    engine.configure(s);
    save_pending = true; save_at = now_ms() + 1000;
    return true;
}
void effects_new_session() {
    engine.reset(); have_input = false; live = false;
    chord_down = false; chord_fired = false;
    sent_left = {}; sent_right = {};
}
void effects_input(const ControllerState &input) {
    if (!bt_is_connected()) return;
    last_input = now_ms(); have_input = true;
    engine.input(input.l2, input.r2);
    const uint32_t chord = CONTROLLER_BUTTON_MUTE | CONTROLLER_BUTTON_L1 | CONTROLLER_BUTTON_R1;
    const bool down = (input.buttons & chord) == chord;
    if (down && !chord_down) { chord_start = last_input; chord_fired = false; }
    if (down && !chord_fired && last_input - chord_start >= 1000) {
        auto s = botw_bow_settings();
        if (engine.settings().profile != 3) s.profile=3;
        effects_configure(s); chord_fired = true;
    }
    chord_down = down;
}
void effects_rumble(uint8_t left, uint8_t right) { engine.rumble(left, right, now_ms()); }
void effects_native(unsigned side, const uint8_t effect[11]) { engine.native(side, effect); }
void effects_overlay(uint8_t left[11], uint8_t right[11]) {
    TriggerEffect l{}, r{}; l[0] = r[0] = 0x05;
    if (live) { l = engine.render(0, !(usb_mode_is_switch() || switch_bt_enabled()), now_ms()); r = engine.render(1, !(usb_mode_is_switch() || switch_bt_enabled()), now_ms()); }
    std::copy(l.begin(), l.end(), left); std::copy(r.begin(), r.end(), right);
}
void effects_diagnostics(uint8_t out[12]) {
    out[0] = 1; out[1] = engine.settings().profile;
    out[2] = (live ? 1 : 0) | ((usb_mode_is_switch() || switch_bt_enabled()) ? 2 : 0);
    out[3] = engine.settings().force;
    const uint32_t age = have_input ? now_ms() - last_input : UINT32_MAX;
    for (unsigned i=0;i<4;++i) {
        out[4+i] = static_cast<uint8_t>(safety_stops >> (8*i));
        out[8+i] = static_cast<uint8_t>(age >> (8*i));
    }
}
void effects_stop() {
    ++safety_stops;
    engine.reset(); live = false; have_input = false;
    sent_left = {}; sent_right = {};
    state_clear_outputs();
    bt_flush_output();
    switch_rumble_adapter_stop();
    audio_reset_session();
    if (bt_is_connected()) {
        uint8_t packet[78]{}; packet[0] = 0x31; packet[1] = (reportSeqCounter++ & 15) << 4; packet[2] = 0x10;
        state_set(packet + 3, sizeof(SetStateData)); bt_write(packet, sizeof(packet));
    }
    bridge_neutral_input();
}
void effects_task() {
    const uint32_t now = now_ms();
    const bool available = bt_is_connected() && (switch_bt_enabled() ? switch_bt_connected() : (tud_mounted() && !tud_suspended())) &&
                           have_input && now - last_input < 500;
    if (!available) { if (live) effects_stop(); }
    else {
        if (!live) update_mic_status();
        live = true;
        if (now - last_send >= 20) {
            const auto l = engine.render(0, !(usb_mode_is_switch() || switch_bt_enabled()), now);
            const auto r = engine.render(1, !(usb_mode_is_switch() || switch_bt_enabled()), now);
            const bool vibration = l[0] == 0x26 || r[0] == 0x26;
            if (l != sent_left || r != sent_right || vibration) {
                uint8_t packet[78]{}; packet[0] = 0x31; packet[1] = (reportSeqCounter++ & 15) << 4; packet[2] = 0x10;
                state_set(packet + 3, sizeof(SetStateData)); bt_write(packet, sizeof(packet));
                sent_left = l; sent_right = r;
            }
            last_send = now;
        }
    }
    if (save_pending && static_cast<int32_t>(now - save_at) >= 0) {
        if (config_save()) save_pending = false;
        else save_at = now + 2000;
    }
}
