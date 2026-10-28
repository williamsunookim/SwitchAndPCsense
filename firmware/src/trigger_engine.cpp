// SPDX-License-Identifier: MIT
#include "trigger_engine.h"
#include <algorithm>
#include <cstring>
bool trigger_settings_valid(const TriggerSettings &s) {
    if (s.version != 1 || s.profile > 3 || s.force > 1) return false;
    for (const auto *c : {&s.left, &s.right}) {
        if (c->mode > 4 || c->start > 9 || c->end > 9 || c->start > c->end ||
            c->kick_strength > 8 || c->kick_ms > 200 || c->rumble_strength > 8 ||
            c->frequency == 0) return false;
        for (auto force : c->curve) if (force > 8) return false;
    }
    return true;
}
TriggerSettings botw_bow_settings() {
    TriggerSettings s; s.profile = 1;
    s.right.mode = static_cast<uint8_t>(TriggerMode::Bow);
    const uint8_t curve[10] = {0, 0, 1, 2, 2, 3, 4, 5, 6, 6};
    std::memcpy(s.right.curve, curve, sizeof(curve)); s.right.end = 9;
    return s;
}
void TriggerEngine::reset() { for (auto &s : side_) s = {}; }
void TriggerEngine::configure(const TriggerSettings &s) {
    if (!trigger_settings_valid(s)) return;
    settings_ = s;
    for (auto &side : side_) { side.kicking = false; side.released = false; }
}
void TriggerEngine::input(uint8_t left, uint8_t right) {
    const uint8_t positions[2] = {left, right};
    for (unsigned i = 0; i < 2; ++i) {
        auto &s = side_[i];
        const bool pulled = s.pulled ? positions[i] > 16 : positions[i] >= 32;
        if (s.pulled && !pulled) s.released = true;
        s.pulled = pulled;
    }
}
void TriggerEngine::rumble(uint8_t left, uint8_t right, uint32_t now) {
    const uint8_t levels[2] = {left, right};
    for (unsigned i = 0; i < 2; ++i) {
        auto &s = side_[i];
        if (now - s.rumble_at >= 500) s.rumble_high = false;
        const bool high = levels[i] >= 96;
        if (high && !s.rumble_high) { s.kick_at = now; s.kicking = true; }
        s.rumble_high = high;
        s.rumble = levels[i]; s.rumble_at = now;
    }
}
void TriggerEngine::native(unsigned side, const uint8_t effect[11]) {
    if (side >= 2) return;
    auto &s = side_[side];
    std::copy_n(effect, 11, s.native.begin());
    s.owned = effect[0] != 0 && effect[0] != 0x05;
    s.released = false; s.kicking = false;
}
TriggerEffect TriggerEngine::render(unsigned side, bool pc, uint32_t now) {
    TriggerEffect out{}; out[0] = 0x05;
    if (side >= 2) return out;
    auto &s = side_[side];
    if (pc && s.owned && !settings_.force) {
        s.released = false; s.kicking = false; return s.native;
    }
    if (settings_.profile == 3 || (pc && settings_.profile == 0)) return out;
    static const TriggerSettings automatic=botw_bow_settings();
    const auto &selected = !pc && settings_.profile==0 ? automatic : settings_;
    const auto &c = side == 0 ? selected.left : selected.right;
    const auto mode = static_cast<TriggerMode>(c.mode);
    if (s.released) {
        if (mode == TriggerMode::Bow && c.kick_strength) { s.kicking = true; s.kick_at = now; }
        s.released = false;
    }
    if (s.kicking && now - s.kick_at >= c.kick_ms) s.kicking = false;
    uint8_t vibration = 0;
    if ((mode == TriggerMode::Bow || mode == TriggerMode::Recoil) && s.kicking)
        vibration = c.kick_strength;
    if (mode == TriggerMode::Rumble && s.pulled && now - s.rumble_at < 150 &&
        s.kicking && c.rumble_strength)
        vibration = static_cast<uint8_t>((s.rumble * c.rumble_strength + 254) / 255);
    uint16_t mask = 0; uint32_t forces = 0;
    if (vibration) {
        out[0] = 0x26; out[9] = c.frequency;
        for (unsigned z = c.start; z <= c.end; ++z) {
            mask |= 1u << z; forces |= uint32_t(vibration - 1) << (3 * z);
        }
    } else if ((mode == TriggerMode::Resistance && s.pulled) || mode == TriggerMode::Bow) {
        out[0] = 0x21;
        for (unsigned z = c.start; z <= c.end; ++z) if (c.curve[z]) {
            mask |= 1u << z; forces |= uint32_t(c.curve[z] - 1) << (3 * z);
        }
    }
    if (!mask) { out = {}; out[0] = 0x05; return out; }
    out[1] = static_cast<uint8_t>(mask); out[2] = static_cast<uint8_t>(mask >> 8);
    for (unsigned i = 0; i < 4; ++i) out[3 + i] = static_cast<uint8_t>(forces >> (8 * i));
    return out;
}
