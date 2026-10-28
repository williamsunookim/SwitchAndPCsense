// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#ifndef HAPTIC_CORRECTION_ENABLED
#define HAPTIC_CORRECTION_ENABLED 1
#endif
#ifndef HAPTIC_LEFT_TRIM_Q8
#define HAPTIC_LEFT_TRIM_Q8 256
#endif
#ifndef HAPTIC_RIGHT_TRIM_Q8
#define HAPTIC_RIGHT_TRIM_Q8 256
#endif
static_assert(HAPTIC_LEFT_TRIM_Q8 >= 0 && HAPTIC_LEFT_TRIM_Q8 <= 256);
static_assert(HAPTIC_RIGHT_TRIM_Q8 >= 0 && HAPTIC_RIGHT_TRIM_Q8 <= 256);
// Conservative shaping preset, NOT an inverse measured actuator response.
// Q8 gain 256 = unity. Values may attenuate only; no automatic force increase.
struct HapticCalibration {
    bool enabled = HAPTIC_CORRECTION_ENABLED;
    uint16_t left_trim_q8 = HAPTIC_LEFT_TRIM_Q8, right_trim_q8 = HAPTIC_RIGHT_TRIM_Q8;
    uint16_t gain_q8[7] = {128, 218, 256, 256, 218, 154, 90};
};
constexpr uint16_t HAPTIC_CALIBRATION_HZ[7] = {40, 80, 160, 320, 640, 1000, 1490};
bool haptic_calibration_valid(const HapticCalibration &config);
uint16_t haptic_frequency_gain(const HapticCalibration &config, uint16_t frequency_hz);
int32_t haptic_sine_q15(uint32_t phase);
