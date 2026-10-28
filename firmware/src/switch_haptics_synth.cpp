/*
 * Switch HD Rumble to stereo PCM synthesizer.
 *
 * SPDX-License-Identifier: MIT
 */

#include "switch_haptics_synth.h"

#include <algorithm>

namespace {

constexpr uint8_t AMPLITUDE_RAMP_SAMPLES = 16;

uint32_t frequency_to_increment(uint16_t frequency_hz) {
    if (frequency_hz == 0) return 0;
    return static_cast<uint32_t>((static_cast<uint64_t>(frequency_hz) << 32) /
                                 SWITCH_HAPTICS_SAMPLE_RATE);
}

} // namespace

void SwitchHapticsSynth::reset() {
    requested_ = {};
    left_low_ = {};
    left_high_ = {};
    right_low_ = {};
    right_high_ = {};
}

void SwitchHapticsSynth::set_band(Oscillator &oscillator, const SwitchRumbleBand &band) {
    const auto increment = frequency_to_increment(std::min<uint16_t>(band.frequency_hz, 1490));
    if (increment != oscillator.target_increment) {
        oscillator.target_increment = increment;
        oscillator.frequency_ramp_remaining = AMPLITUDE_RAMP_SAMPLES;
    }
    const int32_t target = static_cast<int32_t>(band.amplitude) * haptic_frequency_gain(calibration_, band.frequency_hz);
    const int32_t uncorrected = static_cast<int32_t>(band.amplitude) << 8;
    if (target != oscillator.target_amplitude_q8 || uncorrected != oscillator.target_normalization_q8) {
        oscillator.target_normalization_q8 = uncorrected;
        oscillator.target_amplitude_q8 = target;
        oscillator.ramp_remaining = AMPLITUDE_RAMP_SAMPLES;
    }
}

bool SwitchHapticsSynth::configure_calibration(const HapticCalibration &config) {
    if (!haptic_calibration_valid(config)) return false;
    calibration_ = config;
    const auto state = requested_;
    set_state(state); // smooth recalibration from the current envelope
    return true;
}
void SwitchHapticsSynth::set_state(const SwitchRumbleState &state) {
    requested_ = state;
    set_band(left_low_, state.left.low);
    set_band(left_high_, state.left.high);
    set_band(right_low_, state.right.low);
    set_band(right_high_, state.right.high);
}

int32_t SwitchHapticsSynth::render_oscillator(Oscillator &oscillator) {
    if (oscillator.ramp_remaining != 0) {
        oscillator.amplitude_q8 +=
            (oscillator.target_amplitude_q8 - oscillator.amplitude_q8) /
            oscillator.ramp_remaining;
        oscillator.normalization_amplitude_q8 += (oscillator.target_normalization_q8 - oscillator.normalization_amplitude_q8) / oscillator.ramp_remaining;
        --oscillator.ramp_remaining;
    }
    if (oscillator.frequency_ramp_remaining) {
        const int32_t delta = static_cast<int32_t>(oscillator.target_increment) -
                              static_cast<int32_t>(oscillator.increment);
        oscillator.increment += delta / oscillator.frequency_ramp_remaining;
        --oscillator.frequency_ramp_remaining;
    }
    const int32_t sample = haptic_sine_q15(oscillator.phase);
    oscillator.phase += oscillator.increment;
    return sample * oscillator.amplitude_q8;
}

int8_t SwitchHapticsSynth::mix_actuator(Oscillator &low, Oscillator &high,
                                        uint16_t gain_q8) {
    // Shift each product back from Q8 before summing so all per-sample math
    // fits in 32 bits. Normalizing by the combined amplitude keeps two strong
    // simultaneous bands from hard-clipping each other.
    const int32_t mixed = (render_oscillator(low) >> 8) +
                          (render_oscillator(high) >> 8);
    const int32_t total_amplitude =
        (low.normalization_amplitude_q8 + high.normalization_amplitude_q8 + 128) >> 8;
    const int32_t normalizer = std::max<int32_t>(255, total_amplitude);
    int32_t sample_q15 = mixed / normalizer;
    // Smooth peak normalization at boosted gain avoids flat-topping. Unity
    // gain is bit-for-bit unchanged. The public API also bounds pathological gain.
    const int32_t gain = std::min<uint16_t>(gain_q8, 512);
    const int32_t magnitude = sample_q15 < 0 ? -sample_q15 : sample_q15;
    const int32_t denominator = 256 + magnitude * std::max<int32_t>(0, gain - 256) / 32767;
    sample_q15 = sample_q15 * gain / denominator;
    const int32_t sample_u8 = (sample_q15 * 127) / 32767;
    return static_cast<int8_t>(std::clamp<int32_t>(sample_u8, -127, 127));
}

void SwitchHapticsSynth::render(int8_t *samples, size_t frames, uint16_t gain_q8) {
    if (samples == nullptr) return;
        const uint16_t left_gain = static_cast<uint16_t>((std::min<uint16_t>(gain_q8, 512) *
            (calibration_.enabled ? calibration_.left_trim_q8 : 256u)) / 256u);
        const uint16_t right_gain = static_cast<uint16_t>((std::min<uint16_t>(gain_q8, 512) *
            (calibration_.enabled ? calibration_.right_trim_q8 : 256u)) / 256u);
    for (size_t frame = 0; frame < frames; ++frame) {
        samples[frame * 2] = mix_actuator(left_low_, left_high_, left_gain);
        samples[frame * 2 + 1] = mix_actuator(right_low_, right_high_, right_gain);
    }
}

bool SwitchHapticsSynth::silent() const {
    const auto oscillator_silent = [](const Oscillator &oscillator) {
        return oscillator.amplitude_q8 == 0 && oscillator.target_amplitude_q8 == 0;
    };
    return oscillator_silent(left_low_) && oscillator_silent(left_high_) &&
           oscillator_silent(right_low_) && oscillator_silent(right_high_);
}
