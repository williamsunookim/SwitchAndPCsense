// SPDX-License-Identifier: MIT
#include "switch2_wake.h"
#include <cstring>

namespace {
bool address_valid(const uint8_t *a) {
    uint8_t any = 0, different = 0;
    for (unsigned i = 0; i < 6; ++i) { any |= a[i]; different |= a[i] ^ 0xff; }
    return any && different;
}
}
bool switch2_wake_valid(const Switch2WakeSettings &s) {
    return address_valid(s.source) && address_valid(s.target) &&
        std::memcmp(s.source, s.target, 6) != 0 &&
        (s.product == 0x2066 || s.product == 0x2067 || s.product == 0x2069);
}
bool switch2_wake_decode(const uint8_t source[6], const uint8_t *data, uint16_t size, Switch2WakeSettings &out) {
    // Walk bounded AD structures, including captures with additional fields.
    for (uint16_t offset = 0; offset < size;) {
        const uint8_t length = data[offset];
        if (!length) break;
        if (offset + 1u + length > size) return false;
        if (length == 27 && data[offset+1] == 0xff) {
            const auto *m = data + offset + 2;
            const uint8_t header[] = {0x53,5,1,0,3,0x7e,5};
            if (std::memcmp(m, header, sizeof(header)) == 0 &&
                m[9] == 0 && m[10] == 1 && m[11] == 0x81 && m[18] == 15) {
                bool reserved_zero = true;
                for (unsigned i=19; i<26; ++i) reserved_zero &= m[i] == 0;
                Switch2WakeSettings candidate{};
                std::memcpy(candidate.source, source, 6);
                candidate.product = uint16_t(m[7]) | uint16_t(m[8] << 8);
                for (unsigned i=0; i<6; ++i) candidate.target[i] = m[17-i];
                if (reserved_zero && switch2_wake_valid(candidate)) { out = candidate; return true; }
            }
        }
        offset = uint16_t(offset + 1u + length);
    }
    return false;
}
void switch2_wake_payload(const Switch2WakeSettings &s, uint8_t out[31]) {
    // Nintendo manufacturer advertisement; see docs/SWITCH2_WAKE.md.
    const uint8_t base[31] = {2,1,6,27,0xff,0x53,5,1,0,3,0x7e,5,0,0,0,1,0x81,
                             0,0,0,0,0,0,0x0f,0,0,0,0,0,0,0};
    std::memcpy(out, base, 31);
    out[12] = uint8_t(s.product); out[13] = uint8_t(s.product >> 8);
    for (unsigned i = 0; i < 6; ++i) out[17+i] = s.target[5-i];
}
