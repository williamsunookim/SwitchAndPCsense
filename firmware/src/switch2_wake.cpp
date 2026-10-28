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
bool switch2_wake_configure(const Switch2WakeSettings &s) {
    // An all-zero record disables wake. Other malformed records are rejected.
    const Switch2WakeSettings empty{};
    if (!switch2_wake_valid(s) && std::memcmp(&s, &empty, sizeof(s)) != 0) return false;
    if (advertising || requested) { result = 3; return false; }
    const auto previous = config_get_switch2_wake();
    config_set_switch2_wake(s);
    if (!config_save()) { config_set_switch2_wake(previous); result = 6; return false; }
    result = 5;
    // Activate only after reboot: the source is also our Classic public identity.
    return true;
}
bool switch2_wake_request() {
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
    requested = false; home_down = false;
    if (advertising) stop();
}
void switch2_wake_task(bool console_connected) {
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
    if (size < 6 || p[0] != HCI_EVENT_COMMAND_COMPLETE) return;
    const auto opcode = hci_event_command_complete_get_command_opcode(p);
    if (opcode == HCI_OPCODE_HCI_LE_SET_ADVERTISING_PARAMETERS ||
        opcode == HCI_OPCODE_HCI_LE_SET_ADVERTISING_DATA ||
        opcode == HCI_OPCODE_HCI_LE_SET_ADVERTISE_ENABLE) {
        const uint8_t error = hci_event_command_complete_get_return_parameters(p)[0];
        if (error) { hci_error = error; result = 7; if (advertising) stop(); }
    }
}
void switch2_wake_status(uint8_t out[24]) {
    std::memset(out, 0, 24); out[0]='S'; out[1]='2'; out[2]=1;
    const auto &saved = config_get_switch2_wake();
    out[3]=(switch2_wake_valid(active)?1:0)|(advertising?2:0)|
        (std::memcmp(&active,&saved,sizeof(active))!=0?4:0);
    out[4]=result; out[5]=hci_error;
    for (unsigned i=0; i<4; ++i) out[6+i]=uint8_t(attempts>>(8*i));
    std::memcpy(out+10,saved.source,6); std::memcpy(out+16,saved.target,6);
    out[22]=uint8_t(saved.product); out[23]=uint8_t(saved.product>>8);
}
