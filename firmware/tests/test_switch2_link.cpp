#include "aes128.h"
#include "switch2_link.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
std::vector<uint8_t> frame(uint8_t type, const uint8_t *payload, size_t size) {
    std::vector<uint8_t> out(size + s2link::OVERHEAD);
    assert(s2link::encode(type, payload, size, out.data()) == out.size());
    return out;
}
unsigned feed(s2link::Parser &parser, const std::vector<uint8_t> &bytes, std::vector<uint8_t> *last = nullptr) {
    unsigned frames = 0;
    for (auto b : bytes) if (parser.push(b)) {
        ++frames;
        if (last) last->assign(parser.payload(), parser.payload() + parser.size());
    }
    return frames;
}
} // namespace

int main() {
    // FIPS-197 appendix C.1.
    const uint8_t fips_key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    const uint8_t fips_in[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const uint8_t fips_out[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    uint8_t out[16];
    aes128_encrypt(fips_key, fips_in, out);
    assert(std::memcmp(out, fips_out, 16) == 0);
    // Switch 2 pairing confirm vector (ndeadly research / espp golden vector).
    const uint8_t key[16] = {0x5b,0xdc,0x0f,0x82,0x43,0x6f,0x83,0x04,0xc5,0x74,0x58,0xae,0x50,0x07,0xf5,0x69};
    const uint8_t block[16] = {0x44,0x05,0x32,0x1f,0xe9,0x15,0x8c,0xbb,0x15,0xdf,0xfe,0xd8,0x8a,0xdf,0xc6,0x6f};
    const uint8_t expected[16] = {0x13,0x4c,0x97,0xf5,0x11,0xb9,0xb6,0xdd,0x4d,0x86,0xfd,0x40,0xf5,0x36,0xe9,0xed};
    aes128_encrypt(key, block, out);
    assert(std::memcmp(out, expected, 16) == 0);

    // CRC-16/CCITT-FALSE check value.
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    assert(s2link::crc16(check, sizeof(check)) == 0x29b1);

    // State round trip, including signed motion and extremes.
    ControllerState state{};
    state.buttons = CONTROLLER_BUTTON_HOME | CONTROLLER_BUTTON_CROSS | CONTROLLER_BUTTON_MUTE;
    state.dpad = CONTROLLER_DPAD_DOWN_LEFT; state.lx = 0; state.ly = 0xffff; state.rx = 0x1234; state.ry = 0x8000;
    state.l2 = 200; state.r2 = 1; state.gyro[0] = -32768; state.gyro[1] = 32767; state.gyro[2] = -1;
    state.accel[0] = 8192; state.accel[1] = -8192; state.accel[2] = 0; state.sensor_time = 0xdeadbeef;
    state.battery = 7; state.charging = true;
    uint8_t packed[s2link::STATE_SIZE];
    s2link::pack_state(state, packed);
    ControllerState back{};
    assert(s2link::unpack_state(packed, sizeof(packed), back));
    assert(back.buttons == state.buttons && back.dpad == state.dpad && back.lx == 0 && back.ly == 0xffff);
    assert(back.rx == 0x1234 && back.ry == 0x8000 && back.l2 == 200 && back.r2 == 1);
    assert(back.gyro[0] == -32768 && back.gyro[1] == 32767 && back.gyro[2] == -1);
    assert(back.accel[0] == 8192 && back.accel[1] == -8192 && back.sensor_time == 0xdeadbeef);
    assert(back.battery == 7 && back.charging);
    assert(!s2link::unpack_state(packed, sizeof(packed) - 1, back));
    packed[4] = 9; assert(!s2link::unpack_state(packed, sizeof(packed), back));

    // Status round trip.
    s2link::Status status{};
    status.flags = s2link::STATUS_CONNECTED | s2link::STATUS_ENCRYPTED; status.last_error = 0x13;
    status.interval_us = 5000; status.reports = 123456; status.mtu = 247;
    for (uint8_t i = 0; i < 6; ++i) { status.local[i] = i; status.host[i] = uint8_t(0x10 + i); status.peer[i] = uint8_t(0x20 + i); }
    uint8_t packed_status[s2link::STATUS_SIZE];
    s2link::pack_status(status, packed_status);
    s2link::Status status_back{};
    assert(s2link::unpack_status(packed_status, sizeof(packed_status), status_back));
    assert(status_back.flags == status.flags && status_back.interval_us == 5000 && status_back.reports == 123456);
    assert(status_back.mtu == 247 && status_back.peer[5] == 0x25 && status_back.version == s2link::PROTOCOL_VERSION);

    // Framing: clean frame, garbage resync, corrupted CRC, back-to-back frames.
    s2link::Parser parser;
    std::vector<uint8_t> payload;
    s2link::pack_state(state, packed);
    const auto good = frame(s2link::STATE, packed, sizeof(packed));
    assert(feed(parser, good, &payload) == 1 && parser.type() == s2link::STATE);
    assert(payload.size() == s2link::STATE_SIZE && std::memcmp(payload.data(), packed, payload.size()) == 0);

    std::vector<uint8_t> noisy = {0x00, 0xa5, 0x13, 0xa5, 0xa5};
    noisy.insert(noisy.end(), good.begin(), good.end());
    assert(feed(parser, noisy) == 1);

    auto bad = good; bad[10] ^= 0x40;
    const uint32_t errors = parser.errors();
    assert(feed(parser, bad) == 0 && parser.errors() == errors + 1);
    assert(feed(parser, good) == 1); // recovers immediately after a bad frame

    const auto empty = frame(s2link::PAIR, nullptr, 0);
    std::vector<uint8_t> stream = empty; stream.insert(stream.end(), good.begin(), good.end());
    stream.insert(stream.end(), empty.begin(), empty.end());
    assert(feed(parser, stream) == 3 && parser.type() == s2link::PAIR && parser.size() == 0);

    std::vector<uint8_t> oversize = {0xa5, 0x5a, 0x02, uint8_t(s2link::MAX_PAYLOAD + 1)};
    assert(feed(parser, oversize) == 0);
    assert(feed(parser, good) == 1);
    uint8_t too_big[s2link::MAX_PAYLOAD + 1]{};
    uint8_t sink[s2link::MAX_FRAME + 1];
    assert(s2link::encode(s2link::STATE, too_big, sizeof(too_big), sink) == 0);

    std::cout << "switch2 link tests passed\n";
    return 0;
}
