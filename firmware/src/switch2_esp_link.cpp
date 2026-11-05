// SPDX-License-Identifier: MIT
// Switch output through an ESP32-S3 co-processor. The Pico keeps the DualSense
// (Bluetooth Classic) and the haptics engine; the ESP32-S3 runs the Pro
// Controller 2 BLE peripheral, because the CYW43439 cannot follow the
// console's 5 ms connection interval. They talk over UART1 (see
// docs/SWITCH2_ESP32.md for wiring and src/switch2_link.h for the framing).
#if defined(SWITCH2_PRO_NATIVE) && defined(SWITCH2_ESP32_LINK)
#include "switch_bt.h"
#include "switch2_link.h"
#include "switch2_pro_protocol.h"
#include "switch2_wake.h"
#include "switch_rumble_adapter.h"
#include "effects_runtime.h"
#include "config.h"
#include "btstack.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/time.h"
#include "pico/unique_id.h"
extern "C" {
#include "pico/btstack_chipset_cyw43.h"
}
#include <cstring>

namespace {
uart_inst_t *const PORT = uart1;
constexpr uint TX_PIN = 4; // Pico GP4 (pin 6) -> ESP32-S3 GPIO18 (RX)
constexpr uint RX_PIN = 5; // Pico GP5 (pin 7) <- ESP32-S3 GPIO17 (TX)
constexpr uint32_t ALIVE_MS = 500, CONTROL_MS = 100, STATE_MIN_US = 2000, STATE_REFRESH_MS = 20;
constexpr unsigned RING = 1024;

uint8_t rx_ring[RING], tx_ring[RING];
volatile unsigned rx_head = 0, rx_tail = 0, tx_head = 0, tx_tail = 0;
volatile uint32_t rx_overflow = 0;
uint32_t tx_dropped = 0, frames_rx = 0, rumble_frames = 0, rumble_unknown = 0, led_pattern = 0;

s2link::Parser parser;
s2link::Status status{};
bd_addr_t local{};
bool started = false, have_status = false, was_connected = false;
uint32_t last_status_ms = 0, last_control_ms = 0, last_state_ms = 0, wake_attempts = 0;
uint64_t last_state_us = 0;
ControllerState last_sent{};
bool sent_once = false;

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }
void put32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i)); }

void __not_in_flash_func(uart_irq)() {
    while (uart_is_readable(PORT)) {
        const uint8_t b = uint8_t(uart_get_hw(PORT)->dr);
        const unsigned next = (rx_head + 1) % RING;
        if (next == rx_tail) { rx_overflow = rx_overflow + 1; continue; }
        rx_ring[rx_head] = b; rx_head = next;
    }
    while (tx_tail != tx_head && uart_is_writable(PORT)) {
        uart_get_hw(PORT)->dr = tx_ring[tx_tail];
        tx_tail = (tx_tail + 1) % RING;
    }
    uart_set_irqs_enabled(PORT, true, tx_tail != tx_head);
}

bool send(uint8_t type, const uint8_t *payload, size_t size) {
    if (!started) return false;
    uint8_t frame[s2link::MAX_FRAME];
    const size_t n = s2link::encode(type, payload, size, frame);
    if (!n) return false;
    const uint32_t saved = save_and_disable_interrupts();
    const unsigned used = (tx_head + RING - tx_tail) % RING;
    if (RING - 1 - used < n) { restore_interrupts(saved); ++tx_dropped; return false; }
    for (size_t i = 0; i < n; ++i) { tx_ring[tx_head] = frame[i]; tx_head = (tx_head + 1) % RING; }
    // Prime the FIFO here: the PL011 TX interrupt only fires on a threshold
    // crossing, so an already-empty FIFO would never request data.
    while (tx_tail != tx_head && uart_is_writable(PORT)) {
        uart_get_hw(PORT)->dr = tx_ring[tx_tail];
        tx_tail = (tx_tail + 1) % RING;
    }
    uart_set_irqs_enabled(PORT, true, tx_tail != tx_head);
    restore_interrupts(saved);
    return true;
}

bool alive() { return have_status && now_ms() - last_status_ms < ALIVE_MS; }
bool flag(uint8_t f) { return alive() && (status.flags & f) != 0; }

void send_control() {
    const uint8_t payload[2] = {s2link::PROTOCOL_VERSION, uint8_t(switch_bt_enabled() ? s2link::CONTROL_ENABLED : 0)};
    if (send(s2link::CONTROL, payload, sizeof(payload))) last_control_ms = now_ms();
}

void send_state(const ControllerState &s, bool force) {
    const uint64_t now = time_us_64();
    const bool buttons_changed = !sent_once || s.buttons != last_sent.buttons || s.dpad != last_sent.dpad;
    if (!force && !buttons_changed && now - last_state_us < STATE_MIN_US) return;
    uint8_t packed[s2link::STATE_SIZE];
    s2link::pack_state(s, packed);
    if (send(s2link::STATE, packed, sizeof(packed))) {
        last_sent = s; sent_once = true; last_state_us = now; last_state_ms = now_ms();
    }
}

void handle_frame() {
    ++frames_rx;
    const uint8_t *p = parser.payload();
    const uint8_t n = parser.size();
    switch (parser.type()) {
    case s2link::STATUS: {
        s2link::Status next{};
        if (s2link::unpack_status(p, n, next)) { status = next; have_status = true; last_status_ms = now_ms(); }
        break;
    }
    case s2link::RUMBLE: {
        if (!switch_bt_connected()) break;
        Switch2RumblePacket raw{};
        if (!switch2_decode_rumble(p, n, raw)) { ++rumble_unknown; switch_rumble_adapter_stop(); break; }
        SwitchRumbleState states[3]{};
        switch2_approximate_rumble(raw, states);
        switch_rumble_adapter_apply_sequence(states);
        ++rumble_frames;
        break;
    }
    case s2link::LEDS:
        if (n >= 1) led_pattern = p[0];
        break;
    default:
        break;
    }
}
} // namespace

void switch_bt_init(btstack_packet_handler_t) {
    // Same Classic identity as the legacy build so an existing DualSense pairing survives.
    pico_unique_board_id_t id{};
    pico_get_unique_board_id(&id);
    const uint8_t identity[6] = {0x7c, 0xbb, 0x8a, id.id[5], id.id[6], uint8_t(id.id[7] ^ 0x40)};
    std::memcpy(local, identity, 6);
    const auto &wake = config_get_switch2_wake();
    if (switch2_wake_valid(wake)) std::memcpy(local, wake.source, 6);
    hci_set_chipset(btstack_chipset_cyw43_instance());
    hci_set_bd_addr(local);

    uart_init(PORT, s2link::BAUD);
    uart_set_format(PORT, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(PORT, false, false);
    uart_set_fifo_enabled(PORT, true);
    gpio_set_function(TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(RX_PIN); // idle-high when the ESP32 is unplugged
    irq_set_exclusive_handler(UART1_IRQ, uart_irq);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irqs_enabled(PORT, true, false);
    started = true;
    send_control();
}

bool switch_bt_hci(uint8_t *p, uint16_t size) {
    if (size >= 6 && p[0] == HCI_EVENT_COMMAND_COMPLETE &&
        hci_event_command_complete_get_command_opcode(p) == HCI_OPCODE_HCI_READ_LOCAL_VERSION_INFORMATION)
        hci_set_bd_addr(local);
    return false;
}
bool switch_bt_l2cap(uint8_t, uint16_t, uint8_t *, uint16_t) { return false; }

void switch_bt_task() {
    if (!started) return;
    while (rx_tail != rx_head) {
        const uint8_t b = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) % RING;
        if (parser.push(b)) handle_frame();
    }
    const uint32_t now = now_ms();
    if (now - last_control_ms >= CONTROL_MS) send_control();
    if (switch_bt_enabled() && now - last_state_ms >= STATE_REFRESH_MS) send_state(last_sent, true);
    const bool connected = switch_bt_connected();
    if (was_connected && !connected) effects_stop();
    was_connected = connected;
    if (connected) switch_rumble_adapter_task();
}

void switch_bt_update(const ControllerState &s) {
    static bool home = false;
    const bool pressed = (s.buttons & CONTROLLER_BUTTON_HOME) != 0;
    if (pressed && !home && !switch_bt_connected()) switch_bt_wake_request();
    home = pressed;
    if (switch_bt_enabled()) send_state(s, false);
}
void switch_bt_neutral() { send_state(ControllerState{}, true); }
bool switch_bt_connected() {
    constexpr uint8_t need = s2link::STATUS_CONNECTED | s2link::STATUS_ENCRYPTED | s2link::STATUS_SUBSCRIBED;
    return switch_bt_enabled() && alive() && (status.flags & need) == need;
}
bool switch_bt_is_peer(const uint8_t[6]) { return false; } // the console never talks to the Pico radio
void switch_bt_pair() { if (switch_bt_enabled()) send(s2link::PAIR, nullptr, 0); }
void switch_bt_mode_changed() { switch_bt_neutral(); send_control(); }
bool switch_bt_wake_request() {
    if (!switch_bt_enabled() || !flag(s2link::STATUS_BONDED) || flag(s2link::STATUS_CONNECTED)) return false;
    ++wake_attempts;
    return send(s2link::WAKE, nullptr, 0);
}

// ---- USB diagnostics: same page layouts as the on-Pico BLE build ----
void switch_bt_diagnostics(uint8_t out[20]) {
    std::memset(out, 0, 20); out[0] = 'W'; out[1] = 'L'; out[2] = 1;
    out[3] = uint8_t((alive() ? 1 : 0) | (flag(s2link::STATUS_BONDED) ? 2 : 0) | (switch_bt_connected() ? 4 : 0) |
                     (switch_bt_enabled() ? 8 : 0) | 16);
    out[4] = status.last_error; out[7] = flag(s2link::STATUS_ENCRYPTED); out[8] = flag(s2link::STATUS_SUBSCRIBED);
    put32(out + 9, status.reports); put32(out + 13, status.commands);
}
void switch_bt_pro2_diagnostics(uint8_t out[55]) {
    std::memset(out, 0, 55); out[0] = 'P'; out[1] = '2'; out[2] = 1;
    const uint8_t f = alive() ? status.flags : 0;
    out[3] = uint8_t(((f & s2link::STATUS_READY) ? 1 : 0) | ((f & s2link::STATUS_BONDED) ? 2 : 0) |
                     ((f & s2link::STATUS_ENCRYPTED) ? 4 : 0) | ((f & s2link::STATUS_SUBSCRIBED) ? 8 : 0) |
                     ((f & s2link::STATUS_PAIRING) ? 16 : 0) | ((f & s2link::STATUS_READY) ? 32 : 0) |
                     ((f & s2link::STATUS_ADVERTISING) ? 64 : 0));
    out[4] = status.last_error; out[5] = status.pairing_stage; out[7] = status.features;
    put32(out + 8, status.interval_us); put32(out + 12, status.reports); put32(out + 16, status.commands);
    put32(out + 20, status.rumble_packets); put32(out + 24, rumble_unknown); put32(out + 28, status.rejected);
    std::memcpy(out + 32, status.local, 6); std::memcpy(out + 38, status.peer, 6);
    out[46] = uint8_t(status.mtu); out[47] = uint8_t(status.mtu >> 8);
    put32(out + 48, wake_attempts);
}
// Page 7 on this build reports the UART link instead of an ATT trace.
void switch_bt_pro2_att_trace(uint8_t out[55]) {
    std::memset(out, 0, 55); out[0] = 'E'; out[1] = 'L'; out[2] = 1;
    out[3] = uint8_t((alive() ? 1 : 0) | (have_status ? 2 : 0));
    out[4] = status.version;
    put32(out + 5, frames_rx); put32(out + 9, parser.errors()); put32(out + 13, rx_overflow);
    put32(out + 17, tx_dropped); put32(out + 21, have_status ? now_ms() - last_status_ms : 0xffffffffu);
    put32(out + 25, rumble_frames); out[29] = uint8_t(led_pattern); out[30] = status.flags;
}
void switch_bt_pro2_wake_status(uint8_t out[32]) {
    std::memset(out, 0, 32); out[0] = 'S'; out[1] = '2'; out[2] = 1;
    out[3] = uint8_t(64 | (flag(s2link::STATUS_BONDED) ? 1 : 0) | (flag(s2link::STATUS_WAKE) ? 2 : 0));
    out[5] = status.last_error; put32(out + 6, wake_attempts);
    std::memcpy(out + 10, status.local, 6); std::memcpy(out + 16, status.host, 6);
    out[22] = 0x69; out[23] = 0x20;
}
void switch_bt_motion_diagnostics(uint8_t out[55]) { switch_bt_pro2_diagnostics(out); }
void switch_bt_gap_diagnostics(uint8_t out[24]) { std::memset(out, 0, 24); }
void switch_bt_event_page(uint8_t out[59], unsigned) { std::memset(out, 0, 59); out[0] = 'E'; }
void switch_bt_trace(uint8_t out[26]) { std::memset(out, 0, 26); out[0] = 'T'; std::memcpy(out + 20, local, 6); }
#endif
