// SPDX-License-Identifier: MIT
// ESP32-S3 Pro Controller 2 BLE peripheral for SwitchAndPCsense.
//
// The Pico keeps the DualSense (Bluetooth Classic) and streams ControllerState
// over UART; this firmware presents it to a Switch 2 as a native Pro
// Controller 2 and forwards HD Rumble 2 back to the Pico.
//
// Protocol facts: ndeadly/switch2_controller_research, Linux hid-nintendo,
// SDL hidapi_switch2, esp-cpp/espp (MIT) switch2_pro. The command handling,
// pairing and report packing live in firmware/src/switch2_pro_protocol.cpp,
// shared with the Pico build and its host tests.
#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "os/os_mempool.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "aes128.h"
#include "switch2_link.h"
#include "switch2_pro_protocol.h"

extern "C" void ble_store_config_init(void);

namespace {
constexpr const char *TAG = "procon2";

// ---- Wiring (see docs/SWITCH2_ESP32.md) ----
// Defaults match ESP32-S3-DevKitC-1. Override for other boards, e.g. XIAO
// ESP32S3: idf.py build -DLINK_TX_PIN=5 -DLINK_RX_PIN=6 (pins D4/D5).
#ifndef LINK_TX_PIN
#define LINK_TX_PIN 17
#endif
#ifndef LINK_RX_PIN
#define LINK_RX_PIN 18
#endif
constexpr uart_port_t LINK_UART = UART_NUM_1;
constexpr int LINK_TX = LINK_TX_PIN; // -> Pico GP5 (UART1 RX)
constexpr int LINK_RX = LINK_RX_PIN; // <- Pico GP4 (UART1 TX)
constexpr gpio_num_t BOOT_BUTTON = GPIO_NUM_0; // hold 3 s: forget console and pair again

constexpr uint32_t WAKE_MS = 3000, PICO_TIMEOUT_MS = 1000, STATUS_MS = 100;
constexpr size_t RESPONSE_PREFIX = 14, RUMBLE_PREFIX = 33;

// ---- Shared state, guarded by `lock` ----
SemaphoreHandle_t lock;
// Recursive: a GATT write handler holds the lock while notifying, and NimBLE
// delivers BLE_GAP_EVENT_NOTIFY_TX to gap_event synchronously on the same task.
struct Guard {
    Guard() { xSemaphoreTakeRecursive(lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGiveRecursive(lock); }
};

Switch2ProProtocol protocol;
uint8_t local_display[6]{};
uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
ble_addr_t peer_id{};
bool synced = false, gatt_ok = false, crypto_ok = false;
bool enabled = false, known = false, pairing = true, encrypted = false, advertising = false;
bool sub05 = false, sub09 = false, sub_rsp1 = false, sub_rsp2 = false, bond_pending = false;
uint32_t wake_until = 0, last_pico_ms = 0, interval_us = 15000;
int64_t next_input_us = 0;
s2link::Status st{};

uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }
bool same6(const uint8_t *a, const uint8_t *b) { return std::memcmp(a, b, 6) == 0; }
void reverse6(const uint8_t *in, uint8_t *out) { for (unsigned i = 0; i < 6; ++i) out[i] = in[5 - i]; }

// Neutral inputs that keep the last known DualSense power state: a zeroed
// battery field makes the console warn "battery low". Until the Pico reports,
// claim a full battery rather than an empty one.
uint8_t last_battery = 9;
bool last_charging = false;
ControllerState neutral_state() {
    ControllerState s{};
    s.battery = last_battery;
    s.charging = last_charging;
    return s;
}

// The protocol reset also clears the input state; seed it with the last power
// state so the console never sees a zero battery between reset and the next frame.
void reset_protocol() {
    protocol.reset(local_display);
    protocol.update(neutral_state());
}

// ---- UART link to the Pico ----
void link_send(uint8_t type, const uint8_t *payload, size_t size) {
    uint8_t frame[s2link::MAX_FRAME];
    const size_t n = s2link::encode(type, payload, size, frame);
    if (n) uart_write_bytes(LINK_UART, frame, n);
}

// ---- Bond persistence ----
constexpr uint32_t BOND_MAGIC = 0x50324232; // "P2B2"
struct BondRecord {
    uint32_t magic;
    Switch2ProBond bond;
    ble_addr_t peer; // NimBLE identity address (little-endian val)
    uint32_t checksum;
};
uint32_t checksum(const BondRecord &r) {
    const auto *p = reinterpret_cast<const uint8_t *>(&r);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(BondRecord, checksum); ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}
bool bond_load(BondRecord &rec) {
    nvs_handle_t h;
    if (nvs_open("procon2", NVS_READONLY, &h) != ESP_OK) return false;
    size_t size = sizeof(rec);
    const bool ok = nvs_get_blob(h, "bond", &rec, &size) == ESP_OK && size == sizeof(rec) &&
                    rec.magic == BOND_MAGIC && rec.checksum == checksum(rec);
    nvs_close(h);
    return ok;
}
bool bond_save(const Switch2ProBond &bond, const ble_addr_t &peer) {
    BondRecord rec{};
    rec.magic = BOND_MAGIC; rec.bond = bond; rec.peer = peer; rec.checksum = checksum(rec);
    nvs_handle_t h;
    if (nvs_open("procon2", NVS_READWRITE, &h) != ESP_OK) return false;
    const bool ok = nvs_set_blob(h, "bond", &rec, sizeof(rec)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
void bond_erase() {
    nvs_handle_t h;
    if (nvs_open("procon2", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "bond");
    nvs_commit(h);
    nvs_close(h);
}

// The console encrypts with the 0x15 LTK directly (no SMP key distribution),
// so the key must already sit in NimBLE's store when its LTK request arrives.
bool inject_ltk(const ble_addr_t &peer, const uint8_t key[16]) {
    ble_store_value_sec sec{};
    sec.peer_addr = peer;
    sec.key_size = 16;
    sec.ediv = 0;
    sec.rand_num = 0;
    std::memcpy(sec.ltk, key, 16); // A1 ^ B1, the order the controller uses
    sec.ltk_present = 1;
    sec.authenticated = 1;
    const int rc = ble_store_write_our_sec(&sec);
    if (rc) { ESP_LOGE(TAG, "LTK injection failed rc=%d", rc); st.last_error = 0xe1; }
    return rc == 0;
}

// ---- GATT ----
enum Chr : uint8_t {
    C281, C282, C283, IN05, IN09, RUMBLE, CMD, COMBINED, FWUPDATE, RSP1, RSP2, RSP3,
    IN_FDE, W_FDF, W_CC06, IN_F9, W_380, CHR_COUNT
};
uint16_t value_handle[CHR_COUNT]{};
constexpr unsigned DSC_COUNT = 7;
uint8_t dsc_value[DSC_COUNT][32]{};
uint8_t dsc_length[DSC_COUNT]{};

ble_uuid128_t uuid128(const char *text) {
    ble_uuid128_t u{};
    u.u.type = BLE_UUID_TYPE_128;
    unsigned index = 0;
    bool high = true;
    uint8_t value = 0;
    for (const char *p = text; *p && index < 16; ++p) {
        if (*p == '-') continue;
        const uint8_t digit = uint8_t(*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10);
        if (high) value = uint8_t(digit << 4);
        else u.value[15 - index++] = uint8_t(value | digit); // NimBLE stores UUIDs little-endian
        high = !high;
    }
    return u;
}

void notify(uint16_t handle, const uint8_t *data, size_t size) {
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;
    os_mbuf *om = ble_hs_mbuf_from_flat(data, uint16_t(size));
    if (!om) { st.last_error = 0xf5; return; }
    const int rc = ble_gatts_notify_custom(conn, handle, om);
    if (rc && rc != BLE_HS_ENOMEM) st.last_error = uint8_t(rc);
}

void forward_rumble(const uint8_t *data, size_t size) {
    if (!encrypted || !protocol.paired() || size < s2link::RUMBLE_SIZE) return;
    link_send(s2link::RUMBLE, data, s2link::RUMBLE_SIZE);
    ++st.rumble_packets;
}

void handle_command(const uint8_t *data, size_t size, bool combined) {
    uint8_t out[RESPONSE_PREFIX + 224]{};
    uint8_t *response = out + (combined ? RESPONSE_PREFIX : 0);
    size_t length = 0;
    if (protocol.command(data, size, response, length, aes128_encrypt)) {
        ++st.commands;
        if (data[0] == 0x09 && data[3] == 0x07 && size >= 9) {
            const uint8_t leds = data[8];
            link_send(s2link::LEDS, &leds, 1);
        }
    } else if (size >= 8 && data[0] != 0x15) {
        // Unknown/unsupported command: bare ACK so the console's init sequence advances.
        ++st.rejected;
        std::memset(response, 0, 8);
        response[0] = data[0]; response[1] = 1; response[2] = data[2]; response[3] = data[3];
        response[4] = 0x10; response[5] = 0x78;
        length = 8;
    } else {
        ++st.rejected;
        return; // malformed or out-of-order pairing: no reply, like a real pad
    }
    Switch2ProBond bond{};
    if (protocol.take_bond(bond)) {
        // Pairing finalised: remember who we paired with and arm encryption.
        reverse6(peer_id.val, bond.peer);
        protocol.restore(bond);
        inject_ltk(peer_id, bond.key);
        bond_pending = true;
    }
    notify(value_handle[combined ? RSP2 : RSP1], out, length + (combined ? RESPONSE_PREFIX : 0));
}

int chr_access(uint16_t conn_handle, uint16_t, ble_gatt_access_ctxt *ctxt, void *arg) {
    Guard guard;
    const auto id = Chr(reinterpret_cast<uintptr_t>(arg));
    if (conn_handle != conn) return BLE_ATT_ERR_UNLIKELY;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t data[63]{};
        size_t size = 1;
        if (id == IN05 || id == IN09 || id == IN_FDE || id == IN_F9) {
            if (encrypted && protocol.paired()) protocol.input(id == IN05 ? 5 : 9, uint32_t(esp_timer_get_time()), data);
            size = 63;
        }
        return os_mbuf_append(ctxt->om, data, size) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || !enabled) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t buffer[256];
    uint16_t size = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer), &size)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    switch (id) {
    case RUMBLE:
        forward_rumble(buffer, size);
        return 0;
    case CMD:
        handle_command(buffer, size, false);
        return 0;
    case COMBINED:
        if (size < RUMBLE_PREFIX) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        forward_rumble(buffer, RUMBLE_PREFIX);
        if (size > RUMBLE_PREFIX) handle_command(buffer + RUMBLE_PREFIX, size - RUMBLE_PREFIX, true);
        return 0;
    default:
        return 0; // firmware update, audio and unknown channels are accepted and ignored
    }
}

int dsc_access(uint16_t conn_handle, uint16_t, ble_gatt_access_ctxt *ctxt, void *arg) {
    Guard guard;
    const auto index = unsigned(reinterpret_cast<uintptr_t>(arg));
    if (conn_handle != conn || index >= DSC_COUNT) return BLE_ATT_ERR_UNLIKELY;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC)
        return os_mbuf_append(ctxt->om, dsc_value[index], dsc_length[index]) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    uint16_t size = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, dsc_value[index], sizeof(dsc_value[index]), &size))
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    dsc_length[index] = uint8_t(size);
    return 0;
}

// Handle layout reproduced from Pro Controller 2 captures: our services are
// registered before GAP/GATT so the console sees 0x000a/0x000e inputs,
// 0x0012 rumble, 0x0014/0x0016 commands and 0x001a/0x001e responses.
ble_uuid128_t svc_uuid[2], chr_uuid[CHR_COUNT], rate_uuid, response_uuid;
ble_gatt_dsc_def dsc_defs[DSC_COUNT][2]{};
ble_gatt_chr_def svc1_chrs[4]{}, svc2_chrs[15]{};
ble_gatt_svc_def services[3]{};

bool build_gatt() {
    static const char *const ids[CHR_COUNT] = {
        "00c5af5d-1964-4e30-8f51-1956f96bd281", "00c5af5d-1964-4e30-8f51-1956f96bd282",
        "00c5af5d-1964-4e30-8f51-1956f96bd283", "ab7de9be-89fe-49ad-828f-118f09df7fd2",
        "7492866c-ec3e-4619-8258-32755ffcc0f8", "cc483f51-9258-427d-a939-630c31f72b05",
        "649d4ac9-8eb7-4e6c-af44-1ea54fe5f005", "3dacbc7e-6955-40b5-8eaf-6f9809e8b379",
        "4147423d-fdae-4df7-a4f7-d23e5df59f8d", "c765a961-d9d8-4d36-a20a-5315b111836a",
        "506d9f7d-4278-4e95-a549-326ba77657e0", "d3bd69d2-841c-4241-ab15-f86f406d2a80",
        "ab7de9be-89fe-49ad-828f-118f09df7fde", "ab7de9be-89fe-49ad-828f-118f09df7fdf",
        "cc483f51-9258-427d-a939-630c31f72b06", "7492866c-ec3e-4619-8258-32755ffcc0f9",
        "3dacbc7e-6955-40b5-8eaf-6f9809e8b380"};
    svc_uuid[0] = uuid128("00c5af5d-1964-4e30-8f51-1956f96bd280");
    svc_uuid[1] = uuid128("ab7de9be-89fe-49ad-828f-118f09df7fd0");
    rate_uuid = uuid128("679d5510-5a24-4dee-9557-95df80486ecb");
    response_uuid = uuid128("b746df8c-f358-495b-9cd2-e3bbeda4f979");
    for (unsigned i = 0; i < CHR_COUNT; ++i) chr_uuid[i] = uuid128(ids[i]);

    unsigned next_dsc = 0;
    auto descriptor = [&](const ble_uuid128_t &uuid) {
        auto *d = dsc_defs[next_dsc];
        d[0].uuid = &uuid.u;
        d[0].att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE;
        d[0].access_cb = dsc_access;
        d[0].arg = reinterpret_cast<void *>(uintptr_t(next_dsc));
        ++next_dsc;
        return d;
    };
    auto set = [&](ble_gatt_chr_def &c, Chr id, ble_gatt_chr_flags flags, ble_gatt_dsc_def *dsc = nullptr) {
        c.uuid = &chr_uuid[id].u;
        c.access_cb = chr_access;
        c.arg = reinterpret_cast<void *>(uintptr_t(id));
        c.descriptors = dsc;
        c.flags = flags;
        c.val_handle = &value_handle[id];
    };
    constexpr ble_gatt_chr_flags rn = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
    constexpr ble_gatt_chr_flags wn = BLE_GATT_CHR_F_WRITE_NO_RSP;
    set(svc1_chrs[0], C281, BLE_GATT_CHR_F_READ);
    set(svc1_chrs[1], C282, BLE_GATT_CHR_F_WRITE);
    set(svc1_chrs[2], C283, BLE_GATT_CHR_F_READ);
    unsigned i = 0;
    set(svc2_chrs[i++], IN05, rn, descriptor(rate_uuid));
    set(svc2_chrs[i++], IN09, rn, descriptor(rate_uuid));
    set(svc2_chrs[i++], RUMBLE, wn);
    set(svc2_chrs[i++], CMD, wn);
    set(svc2_chrs[i++], COMBINED, wn);
    set(svc2_chrs[i++], FWUPDATE, wn);
    set(svc2_chrs[i++], RSP1, BLE_GATT_CHR_F_NOTIFY, descriptor(response_uuid));
    set(svc2_chrs[i++], RSP2, BLE_GATT_CHR_F_NOTIFY, descriptor(response_uuid));
    set(svc2_chrs[i++], RSP3, BLE_GATT_CHR_F_NOTIFY, descriptor(response_uuid));
    set(svc2_chrs[i++], IN_FDE, rn, descriptor(rate_uuid));
    set(svc2_chrs[i++], W_FDF, wn);
    set(svc2_chrs[i++], W_CC06, wn);
    set(svc2_chrs[i++], IN_F9, rn, descriptor(rate_uuid));
    set(svc2_chrs[i++], W_380, wn);
    services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[0].uuid = &svc_uuid[0].u;
    services[0].characteristics = svc1_chrs;
    services[1].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[1].uuid = &svc_uuid[1].u;
    services[1].characteristics = svc2_chrs;

    int rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (rc) ESP_LOGE(TAG, "GATT registration failed rc=%d", rc);
    return rc == 0;
}

void log_handles() {
    ESP_LOGI(TAG, "handles: in05=0x%04x in09=0x%04x rumble=0x%04x cmd=0x%04x combined=0x%04x rsp1=0x%04x rsp2=0x%04x",
             value_handle[IN05], value_handle[IN09], value_handle[RUMBLE], value_handle[CMD],
             value_handle[COMBINED], value_handle[RSP1], value_handle[RSP2]);
    if (value_handle[IN05] != 0x0a || value_handle[IN09] != 0x0e || value_handle[COMBINED] != 0x16 ||
        value_handle[RSP2] != 0x1e)
        ESP_LOGW(TAG, "handle map differs from the captured Pro Controller 2 layout");
}

// ---- Advertising ----
int gap_event(ble_gap_event *event, void *);

void advertise() {
    if (!synced) return;
    if (ble_gap_adv_active()) ble_gap_adv_stop();
    advertising = false;
    if (!enabled || conn != BLE_HS_CONN_HANDLE_NONE || !gatt_ok || !crypto_ok) return;
    // Flags + 26-byte Nintendo manufacturer data = the full 31-byte legacy payload.
    uint8_t adv[31] = {2, 1, 6, 27, 0xff, 0x53, 0x05, 0x01, 0x00, 0x03, 0x7e, 0x05, 0x69, 0x20, 0x00, 0x01};
    adv[16] = 0;
    adv[23] = 0x0f;
    if (known && !pairing) {
        reverse6(protocol.bond().hosts[0], adv + 17); // host address, wire order
        if (wake_until && int32_t(wake_until - now_ms()) > 0) adv[16] = 0x81;
    }
    static const uint8_t response[] = {15, 9, 'P', 'r', 'o', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r'};
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = params.itvl_max = 0x30; // 30 ms
    int rc = ble_gap_adv_set_data(adv, sizeof(adv));
    if (!rc) rc = ble_gap_adv_rsp_set_data(response, sizeof(response));
    if (!rc) rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &params, gap_event, nullptr);
    if (rc) { st.last_error = uint8_t(rc); ESP_LOGE(TAG, "advertising failed rc=%d", rc); return; }
    advertising = true;
}

void forget_console() {
    if (known) ble_store_util_delete_peer(&peer_id);
    bond_erase();
    reset_protocol();
    known = false; pairing = true; bond_pending = false; wake_until = 0;
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    else advertise();
    ESP_LOGI(TAG, "bond cleared: advertising for a new console");
}

void session_reset() {
    encrypted = sub05 = sub09 = sub_rsp1 = sub_rsp2 = false;
    std::memset(dsc_length, 0, sizeof(dsc_length));
    next_input_us = 0;
}

void update_interval() {
    ble_gap_conn_desc desc{};
    if (conn != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(conn, &desc) == 0) interval_us = uint32_t(desc.conn_itvl) * 1250;
}

int gap_event(ble_gap_event *event, void *) {
    Guard guard;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        advertising = false;
        if (event->connect.status) { st.last_error = uint8_t(event->connect.status); advertise(); break; }
        ble_gap_conn_desc desc{};
        if (ble_gap_conn_find(event->connect.conn_handle, &desc)) break;
        if (!enabled || (known && !pairing && ble_addr_cmp(&desc.peer_id_addr, &peer_id) != 0)) {
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        conn = event->connect.conn_handle;
        peer_id = desc.peer_id_addr;
        session_reset();
        const auto bond = protocol.bond();
        const bool restore = known && !pairing;
        reset_protocol();
        if (restore) protocol.restore(bond);
        interval_us = uint32_t(desc.conn_itvl) * 1250;
        ESP_LOGI(TAG, "connected: interval=%.2f ms latency=%u timeout=%u ms", desc.conn_itvl * 1.25f,
                 desc.conn_latency, desc.supervision_timeout * 10);
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle != conn) break;
        ESP_LOGW(TAG, "disconnected: reason=0x%03x", event->disconnect.reason);
        st.last_error = uint8_t(event->disconnect.reason);
        conn = BLE_HS_CONN_HANDLE_NONE;
        session_reset();
        if (pairing && !known) reset_protocol();
        advertise();
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        update_interval();
        ESP_LOGI(TAG, "connection update: status=%d interval=%lu us", event->conn_update.status,
                 static_cast<unsigned long>(interval_us));
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.conn_handle != conn) break;
        encrypted = event->enc_change.status == 0;
        ESP_LOGI(TAG, "encryption: status=%d", event->enc_change.status);
        if (!encrypted) { st.last_error = uint8_t(event->enc_change.status); ble_gap_terminate(conn, BLE_ERR_AUTH_FAIL); break; }
        if (protocol.paired()) {
            if (bond_pending) {
                if (bond_save(protocol.bond(), peer_id)) bond_pending = false;
                else st.last_error = 0xf1;
            }
            known = true; pairing = false; wake_until = 0;
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE: {
        const auto h = event->subscribe.attr_handle;
        const bool on = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "subscribe: handle=0x%04x notify=%d", h, on);
        if (h == value_handle[IN05]) sub05 = on;
        else if (h == value_handle[IN09]) sub09 = on;
        else if (h == value_handle[RSP1]) sub_rsp1 = on;
        else if (h == value_handle[RSP2]) sub_rsp2 = on;
        break;
    }
    case BLE_GAP_EVENT_MTU:
        st.mtu = event->mtu.value;
        ESP_LOGI(TAG, "mtu: %u", event->mtu.value);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // The console never uses SMP; refuse rather than overwrite the injected key.
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:
        break;
    }
    return 0;
}

// ---- NimBLE host ----
void on_sync() {
    Guard guard;
    ble_hs_util_ensure_addr(0);
    uint8_t le[6]{};
    if (ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, le, nullptr)) {
        ESP_LOGE(TAG, "no public Bluetooth address");
        st.last_error = 0xe2;
        return;
    }
    reverse6(le, local_display);
    reset_protocol();
    BondRecord rec{};
    if (bond_load(rec) && same6(rec.bond.local, local_display)) {
        protocol.restore(rec.bond);
        peer_id = rec.peer;
        known = inject_ltk(peer_id, rec.bond.key);
        pairing = !known;
        ESP_LOGI(TAG, "bond loaded: reconnect mode");
    }
    synced = true;
    log_handles();
    advertise();
}

void on_reset(int reason) {
    Guard guard;
    synced = false; advertising = false; conn = BLE_HS_CONN_HANDLE_NONE;
    session_reset();
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

void host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ---- Tasks ----
// Backpressure: notifications that the controller has not sent yet stay in the
// host's msys_1 pool. Without a cap the 5 ms producer keeps that pool full and every
// report waits behind ~100 stale ones (hundreds of ms of input lag). Skip a report
// while more than a few are still queued; this matches the link's real drain rate.
constexpr int MAX_QUEUED_MBUFS = 3;
bool tx_backlogged() {
    os_mempool *mp = nullptr;
    os_mempool_info info{};
    while ((mp = os_mempool_info_get_next(mp, &info)) != nullptr)
        if (std::strcmp(info.omi_name, "msys_1") == 0)
            return info.omi_num_blocks - info.omi_num_free >= MAX_QUEUED_MBUFS;
    return false;
}

// Input reports paced at the connection interval (5 ms once the console
// switches the link down; 15 ms during pairing).
void input_task(void *) {
    uint8_t report[63];
    for (;;) {
        vTaskDelay(1);
        Guard guard;
        if (conn == BLE_HS_CONN_HANDLE_NONE || !encrypted || !protocol.paired() || !(sub05 || sub09)) continue;
        const int64_t now = esp_timer_get_time();
        if (now < next_input_us) continue;
        if (tx_backlogged()) continue; // retry next tick with fresher state
        const bool use09 = sub09;
        protocol.input(use09 ? 9 : 5, uint32_t(now), report);
        notify(value_handle[use09 ? IN09 : IN05], report, sizeof(report));
        ++st.reports;
        next_input_us = now + std::max<uint32_t>(interval_us, 3750);
    }
}

void send_status() {
    st.flags = uint8_t((synced && gatt_ok && crypto_ok ? s2link::STATUS_READY : 0) |
                       (known ? s2link::STATUS_BONDED : 0) |
                       (conn != BLE_HS_CONN_HANDLE_NONE ? s2link::STATUS_CONNECTED : 0) |
                       (encrypted ? s2link::STATUS_ENCRYPTED : 0) |
                       ((sub05 || sub09) ? s2link::STATUS_SUBSCRIBED : 0) |
                       (pairing ? s2link::STATUS_PAIRING : 0) |
                       (advertising ? s2link::STATUS_ADVERTISING : 0) |
                       (wake_until ? s2link::STATUS_WAKE : 0));
    st.pairing_stage = protocol.stage();
    st.features = protocol.features();
    st.interval_us = conn != BLE_HS_CONN_HANDLE_NONE ? interval_us : 0;
    std::memcpy(st.local, local_display, 6);
    if (known) std::memcpy(st.host, protocol.bond().hosts[0], 6); else std::memset(st.host, 0, 6);
    reverse6(peer_id.val, st.peer);
    uint8_t packed[s2link::STATUS_SIZE];
    s2link::pack_status(st, packed);
    link_send(s2link::STATUS, packed, sizeof(packed));
}

void handle_link_frame(const s2link::Parser &parser) {
    const uint8_t *p = parser.payload();
    const uint8_t n = parser.size();
    switch (parser.type()) {
    case s2link::CONTROL:
        if (n >= 2 && p[0] == s2link::PROTOCOL_VERSION) {
            const bool next = (p[1] & s2link::CONTROL_ENABLED) != 0;
            if (next != enabled) {
                enabled = next;
                ESP_LOGI(TAG, "Pico mode: %s", enabled ? "Switch" : "PC");
                if (!enabled) {
                    protocol.update(neutral_state());
                    wake_until = 0;
                    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
                }
                advertise();
            }
        }
        break;
    case s2link::STATE: {
        ControllerState state{};
        if (s2link::unpack_state(p, n, state)) {
            last_battery = state.battery;
            last_charging = state.charging;
            protocol.update(state);
        }
        break;
    }
    case s2link::PAIR:
        if (enabled) forget_console();
        break;
    case s2link::WAKE:
        if (enabled && known && conn == BLE_HS_CONN_HANDLE_NONE) {
            wake_until = now_ms() + WAKE_MS;
            if (!wake_until) wake_until = 1;
            ESP_LOGI(TAG, "wake advertisement");
            advertise();
        }
        break;
    default:
        break;
    }
}

void link_task(void *) {
    s2link::Parser parser;
    uint8_t buffer[256];
    uint32_t last_status = 0, button_down_since = 0;
    for (;;) {
        // Block for the first byte only, then drain what is already buffered:
        // asking for the full buffer would wait out the timeout on every frame.
        int n = uart_read_bytes(LINK_UART, buffer, 1, pdMS_TO_TICKS(10));
        size_t pending = 0;
        if (n == 1 && uart_get_buffered_data_len(LINK_UART, &pending) == ESP_OK && pending) {
            const int more = uart_read_bytes(LINK_UART, buffer + 1, std::min(pending, sizeof(buffer) - 1), 0);
            if (more > 0) n += more;
        }
        Guard guard;
        for (int i = 0; i < n; ++i) {
            if (!parser.push(buffer[i])) continue;
            last_pico_ms = now_ms();
            handle_link_frame(parser);
        }
        const uint32_t now = now_ms();
        if (last_pico_ms && now - last_pico_ms > PICO_TIMEOUT_MS) {
            protocol.update(neutral_state()); // Pico gone: hold neutral rather than a stuck input
            last_pico_ms = 0;
        }
        if (wake_until && int32_t(now - wake_until) >= 0) { wake_until = 0; advertise(); }
        // BOOT held for 3 s: forget the console (standalone fallback for PAIR).
        if (gpio_get_level(BOOT_BUTTON) == 0) {
            if (!button_down_since) button_down_since = now ? now : 1;
            else if (button_down_since != UINT32_MAX && now - button_down_since > 3000) {
                forget_console();
                button_down_since = UINT32_MAX;
            }
        } else button_down_since = 0;
        if (now - last_status >= STATUS_MS) { last_status = now; send_status(); }
    }
}

bool crypto_self_test() {
    const uint8_t key[16] = {0x5b,0xdc,0x0f,0x82,0x43,0x6f,0x83,0x04,0xc5,0x74,0x58,0xae,0x50,0x07,0xf5,0x69};
    const uint8_t block[16] = {0x44,0x05,0x32,0x1f,0xe9,0x15,0x8c,0xbb,0x15,0xdf,0xfe,0xd8,0x8a,0xdf,0xc6,0x6f};
    const uint8_t expected[16] = {0x13,0x4c,0x97,0xf5,0x11,0xb9,0xb6,0xdd,0x4d,0x86,0xfd,0x40,0xf5,0x36,0xe9,0xed};
    uint8_t out[16];
    aes128_encrypt(key, block, out);
    return std::memcmp(out, expected, 16) == 0;
}
} // namespace

extern "C" void app_main() {
    lock = xSemaphoreCreateRecursiveMutex();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    uart_config_t uart{};
    uart.baud_rate = int(s2link::BAUD);
    uart.data_bits = UART_DATA_8_BITS;
    uart.parity = UART_PARITY_DISABLE;
    uart.stop_bits = UART_STOP_BITS_1;
    uart.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, 2048, 2048, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &uart));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TX, LINK_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    gpio_set_pull_mode(gpio_num_t(LINK_RX), GPIO_PULLUP_ONLY); // idle-high when the Pico is unplugged

    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << BOOT_BUTTON;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&button);

    crypto_ok = crypto_self_test();
    if (!crypto_ok) { ESP_LOGE(TAG, "AES self-test failed"); st.last_error = 0xe0; }

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // No SMP: the console pairs over command 0x15 and drops peers that start SMP.
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    // Our services first so their handles match the real controller's map.
    gatt_ok = build_gatt();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Pro Controller");
    ble_svc_gap_device_appearance_set(0x03c4); // gamepad, as the real controller reports
    ble_store_config_init();

    xTaskCreate(link_task, "s2link", 4096, nullptr, 5, nullptr);
    xTaskCreate(input_task, "s2input", 4096, nullptr, 6, nullptr);
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "Pro Controller 2 co-processor started (link v%u)", s2link::PROTOCOL_VERSION);
}
