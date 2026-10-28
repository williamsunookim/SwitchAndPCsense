// SPDX-License-Identifier: MIT
#include "motion_resampler.h"
#include <algorithm>
#include <cstring>
namespace {
int16_t lerp(int16_t a, int16_t b, uint32_t fraction_q16) {
    const int64_t product = static_cast<int64_t>(static_cast<int32_t>(b) - a) * fraction_q16;
    return static_cast<int16_t>(a + (product + (product >= 0 ? 32768 : -32768)) / 65536);
}
}
void MotionResampler::reset() { head_ = count_ = 0; }
const MotionResampler::Sample &MotionResampler::at(size_t index) const {
    return samples_[(head_ + index) % CAPACITY];
}
void MotionResampler::push(const ControllerState &state, uint32_t time_us) {
    if (count_) {
        const auto delta = static_cast<int32_t>(time_us - at(count_ - 1).time_us);
        if (delta < 0) return; // out-of-order arrival, not a clock-wrap failure
        if (delta == 0) --count_; // replace duplicate timestamp; never divide by zero
        else if (static_cast<uint32_t>(delta) > STALE_US) reset();
    }
    if (count_ == CAPACITY) { head_ = (head_ + 1) % CAPACITY; --count_; }
    auto &entry = samples_[(head_ + count_) % CAPACITY];
    entry.time_us = time_us;
    std::copy_n(state.gyro, 3, entry.gyro);
    std::copy_n(state.accel, 3, entry.accel);
    ++count_;
}
ControllerState MotionResampler::sample(uint32_t target_us, uint32_t now_us) const {
    ControllerState out{};
    if (!count_) return out;
    // A radio gap must not become a zero (free-fall) sample: hold the newest
    // accel. A real disconnect clears the history, so it still reads neutral.
    if (static_cast<int32_t>(now_us - at(count_ - 1).time_us) > static_cast<int32_t>(STALE_US)) {
        std::copy_n(at(count_ - 1).accel, 3, out.accel);
        return out;
    }
    const Sample *before = &at(0), *after = before;
    if (static_cast<int32_t>(target_us - before->time_us) > 0) {
        before = &at(count_ - 1); after = before;
        for (size_t i = 1; i < count_; ++i) {
            if (static_cast<int32_t>(target_us - at(i).time_us) <= 0) {
                before = &at(i - 1); after = &at(i); break;
            }
        }
    }
    const uint32_t span = after->time_us - before->time_us;
    // Span is at most 40000us, so t*65536 fits uint32_t. One hardware-assisted
    // 32-bit division per sample replaces six software 64-bit divisions.
    const uint32_t fraction = span ? ((target_us - before->time_us) << 16) / span : 0;
    for (size_t i = 0; i < 3; ++i) {
        out.gyro[i] = span ? lerp(before->gyro[i], after->gyro[i], fraction) : before->gyro[i];
        out.accel[i] = span ? lerp(before->accel[i], after->accel[i], fraction) : before->accel[i];
    }
    if (static_cast<int32_t>(target_us - at(count_ - 1).time_us) > static_cast<int32_t>(GYRO_HOLD_US))
        std::fill_n(out.gyro, 3, int16_t{0});
    return out;
}
