// SPDX-License-Identifier: MIT
// UART link between the Pico (DualSense side) and the ESP32-S3 (Switch 2 BLE
// side). Shared verbatim by both firmwares; keep it free of platform headers.
//
// Frame: A5 5A <type> <len> <payload[len]> <crc16 lo> <crc16 hi>
// CRC-16/CCITT-FALSE over type, len and payload. Little-endian fields.
#pragma once
#include <cstddef>
#include <cstdint>
#include "controller_state.h"

namespace s2link {

constexpr uint32_t BAUD = 1000000;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t SYNC0 = 0xa5, SYNC1 = 0x5a;
constexpr size_t MAX_PAYLOAD = 64;
constexpr size_t OVERHEAD = 6;
constexpr size_t MAX_FRAME = MAX_PAYLOAD + OVERHEAD;

enum Type : uint8_t {
    // Pico -> ESP32
    CONTROL = 0x01, // [version, flags(CONTROL_*)]; sent every 100 ms as a heartbeat
    STATE = 0x02,   // packed ControllerState (STATE_SIZE)
    PAIR = 0x03,    // forget the bond and advertise for a new console
    WAKE = 0x04,    // broadcast the wake advertisement for a few seconds
    // ESP32 -> Pico
    STATUS = 0x81,  // packed Status (STATUS_SIZE); every 100 ms and on change
    RUMBLE = 0x82,  // raw 33-byte HD Rumble 2 payload written by the console
    LEDS = 0x83,    // [player LED pattern] from command 0x09/0x07
};

enum ControlFlag : uint8_t { CONTROL_ENABLED = 1 };

enum StatusFlag : uint8_t {
    STATUS_READY = 1,       // NimBLE synced, GATT registered, crypto self-test passed
    STATUS_BONDED = 2,      // a console bond is stored
    STATUS_CONNECTED = 4,   // LE link to the console is up
    STATUS_ENCRYPTED = 8,   // link encrypted with the pairing LTK
    STATUS_SUBSCRIBED = 16, // console enabled input notifications
    STATUS_PAIRING = 32,    // advertising for a new console
    STATUS_ADVERTISING = 64,
    STATUS_WAKE = 128,      // wake advertisement currently on air
};

struct Status {
    uint8_t flags = 0, last_error = 0, pairing_stage = 0, features = 0;
    uint32_t interval_us = 0, reports = 0, commands = 0, rumble_packets = 0, rejected = 0;
    uint8_t local[6]{}, host[6]{}, peer[6]{}; // display order
    uint16_t mtu = 0;
    uint8_t version = PROTOCOL_VERSION;
};

constexpr size_t STATE_SIZE = 33;
constexpr size_t STATUS_SIZE = 45;
constexpr size_t RUMBLE_SIZE = 33;

uint16_t crc16(const uint8_t *data, size_t size, uint16_t crc = 0xffff);
// Returns the frame size written to out (>= size + OVERHEAD bytes), 0 if too large.
size_t encode(uint8_t type, const uint8_t *payload, size_t size, uint8_t *out);

void pack_state(const ControllerState &state, uint8_t out[STATE_SIZE]);
bool unpack_state(const uint8_t *data, size_t size, ControllerState &state);
void pack_status(const Status &status, uint8_t out[STATUS_SIZE]);
bool unpack_status(const uint8_t *data, size_t size, Status &status);

// Byte-at-a-time frame parser. Resynchronises on garbage and bad CRCs.
class Parser {
public:
    // Returns true when a complete, valid frame is available in type()/payload().
    bool push(uint8_t byte);
    uint8_t type() const { return type_; }
    uint8_t size() const { return size_; }
    const uint8_t *payload() const { return payload_; }
    uint32_t errors() const { return errors_; }
private:
    enum class Step : uint8_t { Sync0, Sync1, Type, Size, Payload, CrcLo, CrcHi };
    Step step_ = Step::Sync0;
    uint8_t type_ = 0, size_ = 0, index_ = 0, crc_lo_ = 0;
    uint8_t payload_[MAX_PAYLOAD]{};
    uint32_t errors_ = 0;
};

} // namespace s2link
