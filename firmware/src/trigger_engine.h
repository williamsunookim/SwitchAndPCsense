// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
using TriggerEffect = std::array<uint8_t, 11>;
enum class TriggerMode : uint8_t { Off, Resistance, Bow, Recoil, Rumble };
struct TriggerConfig {
    uint8_t mode = 0, start = 2, end = 9;
    uint8_t curve[10]{}; // 0 free; 1..8 force per travel zone
    uint8_t kick_strength = 0, kick_ms = 60, rumble_strength = 0, frequency = 35;
};
struct TriggerSettings {
    uint8_t version = 1, profile = 0, force = 0; // auto Switch bow / PC off, BOTW_BOW, custom, explicit off
    TriggerConfig left{}, right{};
};
static_assert(sizeof(TriggerConfig) == 17 && sizeof(TriggerSettings) == 37);
bool trigger_settings_valid(const TriggerSettings &s);
TriggerSettings botw_bow_settings();
class TriggerEngine {
public:
    void reset();
    void configure(const TriggerSettings &s);
    void input(uint8_t left, uint8_t right);
    void rumble(uint8_t left, uint8_t right, uint32_t now);
    void native(unsigned side, const uint8_t effect[11]); // left=0
    TriggerEffect render(unsigned side, bool pc, uint32_t now);
    const TriggerSettings &settings() const { return settings_; }
private:
    struct Side {
        TriggerEffect native{};
        bool owned = false, pulled = false, released = false, rumble_high = false;
        uint8_t rumble = 0;
        uint32_t rumble_at = 0, kick_at = 0;
        bool kicking = false;
    } side_[2];
    TriggerSettings settings_{};
};
