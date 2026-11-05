// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include "controller_state.h"
#include "switch_rumble.h"

// Wire order for Nintendo pairing keys, display order for all addresses.
struct Switch2ProBond {
    uint8_t local[6]{}, peer[6]{}, hosts[2][6]{}, key[16]{};
};
using Switch2Aes = void (*)(const uint8_t *, const uint8_t *, uint8_t *);

class Switch2ProProtocol {
public:
    void reset(const uint8_t local[6]);
    void update(const ControllerState &state) { state_ = state; }
    void restore(const Switch2ProBond &bond);
    bool command(const uint8_t *data, size_t size, uint8_t *reply, size_t &length, Switch2Aes aes);
    void input(uint8_t report_id, uint32_t time_us, uint8_t out[63]);
    bool take_bond(Switch2ProBond &out);
    const Switch2ProBond &bond() const { return bond_; }
    bool paired() const { return paired_; }
    uint8_t stage() const { return stage_; }
    uint8_t features() const { return enabled_; }
    uint8_t selected_report() const { return selected_report_; }
private:
    ControllerState state_{};
    Switch2ProBond bond_{}, pending_{};
    uint32_t counter_ = 0;
    uint8_t stage_ = 0, mask_ = 0x2f, enabled_ = 0, selected_report_ = 9;
    bool paired_ = false, bond_dirty_ = false;
};

// Preserve the full 10-bit fields before the hardware-specific approximation.
struct Switch2RumbleFrame { uint16_t frequency[2]{}, amplitude[2]{}; };
struct Switch2RumblePacket { Switch2RumbleFrame left[3]{}, right[3]{}; };
bool switch2_decode_rumble(const uint8_t *data, size_t size, Switch2RumblePacket &out);
void switch2_approximate_rumble(const Switch2RumblePacket &in, SwitchRumbleState out[3]);
