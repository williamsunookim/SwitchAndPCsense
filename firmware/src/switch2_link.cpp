// SPDX-License-Identifier: MIT
#include "switch2_link.h"
#include <cstring>

namespace s2link {
namespace {
void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
uint16_t get16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
} // namespace

uint16_t crc16(const uint8_t *data, size_t size, uint16_t crc) {
    for (size_t i = 0; i < size; ++i) {
        crc = uint16_t(crc ^ (uint16_t(data[i]) << 8));
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = uint16_t((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

size_t encode(uint8_t type, const uint8_t *payload, size_t size, uint8_t *out) {
    if (size > MAX_PAYLOAD) return 0;
    out[0] = SYNC0; out[1] = SYNC1; out[2] = type; out[3] = uint8_t(size);
    if (size) std::memcpy(out + 4, payload, size);
    const uint16_t crc = crc16(out + 2, size + 2);
    put16(out + 4 + size, crc);
    return size + OVERHEAD;
}

void pack_state(const ControllerState &s, uint8_t out[STATE_SIZE]) {
    put32(out, s.buttons); out[4] = s.dpad;
    put16(out + 5, s.lx); put16(out + 7, s.ly); put16(out + 9, s.rx); put16(out + 11, s.ry);
    out[13] = s.l2; out[14] = s.r2;
    for (unsigned i = 0; i < 3; ++i) {
        put16(out + 15 + 2 * i, uint16_t(s.gyro[i]));
        put16(out + 21 + 2 * i, uint16_t(s.accel[i]));
    }
    put32(out + 27, s.sensor_time); out[31] = s.battery; out[32] = s.charging ? 1 : 0;
}

bool unpack_state(const uint8_t *p, size_t size, ControllerState &s) {
    if (size != STATE_SIZE || p[4] > CONTROLLER_DPAD_NEUTRAL || p[32] > 1) return false;
    s.buttons = get32(p); s.dpad = p[4];
    s.lx = get16(p + 5); s.ly = get16(p + 7); s.rx = get16(p + 9); s.ry = get16(p + 11);
    s.l2 = p[13]; s.r2 = p[14];
    for (unsigned i = 0; i < 3; ++i) {
        s.gyro[i] = int16_t(get16(p + 15 + 2 * i));
        s.accel[i] = int16_t(get16(p + 21 + 2 * i));
    }
    s.sensor_time = get32(p + 27); s.battery = p[31]; s.charging = p[32] != 0;
    return true;
}

void pack_status(const Status &s, uint8_t out[STATUS_SIZE]) {
    out[0] = s.flags; out[1] = s.last_error; out[2] = s.pairing_stage; out[3] = s.features;
    put32(out + 4, s.interval_us); put32(out + 8, s.reports); put32(out + 12, s.commands);
    put32(out + 16, s.rumble_packets); put32(out + 20, s.rejected);
    std::memcpy(out + 24, s.local, 6); std::memcpy(out + 30, s.host, 6); std::memcpy(out + 36, s.peer, 6);
    put16(out + 42, s.mtu); out[44] = s.version;
}

bool unpack_status(const uint8_t *p, size_t size, Status &s) {
    if (size != STATUS_SIZE) return false;
    s.flags = p[0]; s.last_error = p[1]; s.pairing_stage = p[2]; s.features = p[3];
    s.interval_us = get32(p + 4); s.reports = get32(p + 8); s.commands = get32(p + 12);
    s.rumble_packets = get32(p + 16); s.rejected = get32(p + 20);
    std::memcpy(s.local, p + 24, 6); std::memcpy(s.host, p + 30, 6); std::memcpy(s.peer, p + 36, 6);
    s.mtu = get16(p + 42); s.version = p[44];
    return true;
}

bool Parser::push(uint8_t b) {
    switch (step_) {
    case Step::Sync0:
        if (b == SYNC0) step_ = Step::Sync1;
        return false;
    case Step::Sync1:
        if (b == SYNC1) step_ = Step::Type;
        else if (b != SYNC0) { step_ = Step::Sync0; ++errors_; }
        return false;
    case Step::Type:
        type_ = b; step_ = Step::Size;
        return false;
    case Step::Size:
        if (b > MAX_PAYLOAD) { step_ = b == SYNC0 ? Step::Sync1 : Step::Sync0; ++errors_; return false; }
        size_ = b; index_ = 0; step_ = size_ ? Step::Payload : Step::CrcLo;
        return false;
    case Step::Payload:
        payload_[index_++] = b;
        if (index_ == size_) step_ = Step::CrcLo;
        return false;
    case Step::CrcLo:
        crc_lo_ = b; step_ = Step::CrcHi;
        return false;
    case Step::CrcHi: {
        step_ = Step::Sync0;
        const uint8_t header[2] = {type_, size_};
        const uint16_t crc = crc16(payload_, size_, crc16(header, 2));
        if (crc != uint16_t(crc_lo_ | (b << 8))) { ++errors_; return false; }
        return true;
    }
    }
    return false;
}

} // namespace s2link
