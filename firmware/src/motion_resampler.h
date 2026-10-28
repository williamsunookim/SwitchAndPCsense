// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include "controller_state.h"

// Arrival-clock resampling, never extrapolation. Only sensors are delayed.
class MotionResampler {
public:
    static constexpr uint32_t STEP_US = 5000;
    // No playout delay: the newest Switch sample is the newest DualSense sample.
    static constexpr uint32_t DELAY_US = 0;
    static constexpr uint32_t STALE_US = 40000;
    // Gyro is a rate: repeating an old one keeps the console rotating after the
    // controller stopped. Beyond this age past the newest sample, report no rotation.
    static constexpr uint32_t GYRO_HOLD_US = 12000;
    void reset();
    void push(const ControllerState &state, uint32_t time_us);
    ControllerState sample(uint32_t target_us, uint32_t now_us) const;
private:
    struct Sample { uint32_t time_us; int16_t gyro[3], accel[3]; };
    static constexpr size_t CAPACITY = 32;
    Sample samples_[CAPACITY]{};
    size_t head_ = 0, count_ = 0;
    const Sample &at(size_t index) const;
};
