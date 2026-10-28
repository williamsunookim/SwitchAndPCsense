// SPDX-License-Identifier: MIT
#include "switch2_wake.h"
#include "config.h"
#include "usb_mode.h"
#include "btstack.h"
#include "pico/time.h"
#include <cstring>

namespace {
Switch2WakeSettings active{};
uint8_t payload[31]{};
bool ready = false, advertising = false, home_down = false, requested = false;
bool cooldown = false;
uint32_t started = 0, last_stop = 0, attempts = 0;
bool learning = false, have_candidate = false, ambiguous = false;
uint32_t learn_started = 0, learn_duration = 0;
uint8_t filter[6]{};
Switch2WakeSettings candidate{};
uint32_t scan_reports = 0, scan_matches = 0;
uint8_t result = 0, hci_error = 0; // 1 queued, 2 ended, 3 busy, 4 no config, 5 saved, 6 save failed, 7 HCI error, 8 connected
uint32_t now() { return to_ms_since_boot(get_absolute_time()); }
void stop() {
    gap_advertisements_enable(0);
    advertising = false; cooldown = true; last_stop = now();
}
}
void switch2_wake_init() {
    active = config_get_switch2_wake();
    switch2_wake_payload(active, payload);
}
void switch2_wake_ready() { ready = true; }
bool switch2_wake_learn(uint8_t seconds, const uint8_t source_filter[6]) {
    if (!ready) { result = 4; return false; }
    if (usb_mode_is_wireless()) { result = 15; return false; }
    if (learning || advertising || requested) { result = 3; return false; }
    if (seconds < 5 || seconds > 120) return false;
    std::memcpy(filter, source_filter, 6);
    have_candidate = ambiguous = false;
    scan_reports = scan_matches = 0;
    learning = true; learn_started = now(); learn_duration = uint32_t(seconds) * 1000;
    hci_error = 0; result = 10;
    gap_set_scan_params(0, 0x60, 0x30, 0); // passive scan, 60 ms / 30 ms
    gap_set_scan_duplicate_filter(false);
    gap_start_scan();
    return true;
}
void switch2_wake_learn_cancel() {
    if (!learning) return;
    gap_stop_scan(); learning = false; result = 13;
}
bool switch2_wake_configure(const Switch2WakeSettings &s) {
    // An all-zero record disables wake. Other malformed records are rejected.
    const Switch2WakeSettings empty{};
    if (!switch2_wake_valid(s) && std::memcmp(&s, &empty, sizeof(s)) != 0) return false;
    if (learning || advertising || requested) { result = 3; return false; }
    const auto previous = config_get_switch2_wake();
    config_set_switch2_wake(s);
    if (!config_save()) { config_set_switch2_wake(previous); result = 6; return false; }
    result = 5;
    // Activate only after reboot: the source is also our Classic public identity.
    return true;
}
bool switch2_wake_request() {
    if (learning) { result = 3; return false; }
    if (!usb_mode_is_wireless()) { result = 9; return false; }
    if (!ready || !switch2_wake_valid(active)) { result = 4; return false; }
    if (advertising || requested || (cooldown && now()-last_stop < 5000)) { result = 3; return false; }
    requested = true; result = 1; return true;
}
void switch2_wake_input(bool home) {
    if (home && !home_down) (void)switch2_wake_request();
    home_down = home;
}
void switch2_wake_neutral() { home_down = false; }
void switch2_wake_cancel() {
    switch2_wake_learn_cancel();
    requested = false; home_down = false;
    if (advertising) stop();
}
void switch2_wake_task(bool console_connected) {
    if (learning) {
        if (usb_mode_is_wireless()) { switch2_wake_learn_cancel(); return; }
        if (now() - learn_started >= learn_duration) {
            gap_stop_scan(); learning = false;
            if (ambiguous) result = 12;
            else if (!have_candidate) result = 11;
            else (void)switch2_wake_configure(candidate);
        }
        return;
    }
    if (console_connected) {
        if (requested || advertising) result = 8;
        requested = false;
        if (advertising) stop();
        return;
    }
    if (requested) {
        requested = false; hci_error = 0;
        bd_addr_t unused{};
        gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_OFF);
        // Nonconnectable replay: do not accept a third ACL link or impersonate
        // the donor's GATT services. 30 ms interval on all primary channels.
        gap_advertisements_set_params(0x30, 0x30, 3, 0, unused, 7, 0);
        gap_advertisements_set_data(sizeof(payload), payload);
        gap_advertisements_enable(1);
        advertising = true; started = now(); ++attempts;
    }
    if (advertising && now()-started >= 2000) { stop(); result = 2; }
}
void switch2_wake_hci(uint8_t *p, uint16_t size) {
    if (learning && size >= 12 && p[0] == GAP_EVENT_ADVERTISING_REPORT) {
        ++scan_reports;
        const auto length = gap_event_advertising_report_get_data_length(p);
        // Replay uses our public identity; random/private advertisers are not supported.
        if (12u + length > size || gap_event_advertising_report_get_address_type(p) != 0) return;
        bd_addr_t source{}; gap_event_advertising_report_get_address(p, source);
        uint8_t any = 0; for (auto b : filter) any |= b;
        if (any && std::memcmp(filter, source, 6) != 0) return;
        Switch2WakeSettings decoded{};
        if (switch2_wake_decode(source, gap_event_advertising_report_get_data(p), length, decoded)) {
            ++scan_matches;
            if (have_candidate && std::memcmp(&candidate, &decoded, sizeof(decoded)) != 0) ambiguous = true;
            else {candidate = decoded; have_candidate = true;}
        }
        return;
    }
    if (size < 6 || p[0] != HCI_EVENT_COMMAND_COMPLETE) return;
    const auto opcode = hci_event_command_complete_get_command_opcode(p);
    if (learning && (opcode == HCI_OPCODE_HCI_LE_SET_SCAN_PARAMETERS || opcode == HCI_OPCODE_HCI_LE_SET_SCAN_ENABLE)) {
        const uint8_t error = hci_event_command_complete_get_return_parameters(p)[0];
        if (error) { switch2_wake_learn_cancel(); hci_error = error; result = 7; }
    }
    if (opcode == HCI_OPCODE_HCI_LE_SET_ADVERTISING_PARAMETERS ||
        opcode == HCI_OPCODE_HCI_LE_SET_ADVERTISING_DATA ||
        opcode == HCI_OPCODE_HCI_LE_SET_ADVERTISE_ENABLE) {
        const uint8_t error = hci_event_command_complete_get_return_parameters(p)[0];
        if (error) { hci_error = error; result = 7; if (advertising) stop(); }
    }
}
void switch2_wake_status(uint8_t *out, uint16_t size) {
    if (size < 24) return;
    std::memset(out, 0, 24); out[0]='S'; out[1]='2'; out[2]=1;
    const auto &saved = config_get_switch2_wake();
    out[3]=32|(learning?8:0)|(have_candidate?16:0)|(switch2_wake_valid(active)?1:0)|(advertising?2:0)|
        (std::memcmp(&active,&saved,sizeof(active))!=0?4:0);
    out[4]=result; out[5]=hci_error;
    for (unsigned i=0; i<4; ++i) out[6+i]=uint8_t(attempts>>(8*i));
    std::memcpy(out+10,saved.source,6); std::memcpy(out+16,saved.target,6);
    out[22]=uint8_t(saved.product); out[23]=uint8_t(saved.product>>8);
    if (size >= 32) for (unsigned i=0; i<4; ++i) {
        out[24+i]=uint8_t(scan_reports>>(8*i)); out[28+i]=uint8_t(scan_matches>>(8*i));
    }
}
