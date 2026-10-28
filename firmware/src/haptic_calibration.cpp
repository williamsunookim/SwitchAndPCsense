// SPDX-License-Identifier: MIT
#include "haptic_calibration.h"
#include <algorithm>
namespace {
// Offline-generated quarter sine, 64 intervals. Linear interpolation uses Q16.
constexpr int16_t QUARTER[65] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790,
    27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
    32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767,
};
}
bool haptic_calibration_valid(const HapticCalibration &c) {
    if (c.left_trim_q8 > 256 || c.right_trim_q8 > 256) return false;
    for (auto gain : c.gain_q8) if (gain > 256) return false;
    return true;
}
uint16_t haptic_frequency_gain(const HapticCalibration &c, uint16_t frequency_hz) {
    if (!c.enabled) return 256;
    if (frequency_hz <= HAPTIC_CALIBRATION_HZ[0]) return c.gain_q8[0];
    for (unsigned i=1;i<7;++i) if (frequency_hz <= HAPTIC_CALIBRATION_HZ[i]) {
        const int32_t span = HAPTIC_CALIBRATION_HZ[i] - HAPTIC_CALIBRATION_HZ[i-1];
        const int32_t t = frequency_hz - HAPTIC_CALIBRATION_HZ[i-1];
        const int32_t delta = static_cast<int32_t>(c.gain_q8[i]) - c.gain_q8[i-1];
        return static_cast<uint16_t>(c.gain_q8[i-1] + delta * t / span);
    }
    return c.gain_q8[6];
}
int32_t haptic_sine_q15(uint32_t phase) {
    uint32_t position = phase & 0x3fffffffu;
    if (phase & 0x40000000u) position = 0x40000000u - position;
    const unsigned index = position >> 24;
    int32_t sample = 32767;
    if (index < 64) {
        const int32_t fraction = (position >> 8) & 0xffff;
        sample = QUARTER[index] + ((QUARTER[index+1] - QUARTER[index]) * fraction + 32768) / 65536;
    }
    return (phase & 0x80000000u) ? -sample : sample;
}
