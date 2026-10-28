// SPDX-License-Identifier: MIT
#pragma once
#include "controller_state.h"
#include <cstddef>
#include <cstdint>
class MotionCalibration {
public:
 void reset();
 bool load_factory(const uint8_t *report,size_t length);
 void apply(ControllerState &state,uint32_t now_ms);
 uint8_t status() const { return (factory_valid_?1:0)|(zero_ready_?2:0)|(collecting_?4:0); }
 const int16_t *last_gyro() const { return last_gyro_; }
 const int16_t *zero() const { return zero_; }
private:
 int32_t gyro_gain_[3]{65536,65536,65536},accel_gain_[3]{65536,65536,65536};
 int16_t gyro_bias_[3]{},accel_offset_[3]{},zero_[3]{},last_gyro_[3]{},minimum_[3]{},maximum_[3]{};
 int32_t sums_[3]{};
 uint32_t started_=0,count_=0;
 // Automatic rest re-zero: residual bias after factory calibration makes the
 // console's integrated orientation drift, so re-measure whenever truly still.
 int16_t auto_min_[6]{},auto_max_[6]{};
 int32_t auto_sums_[3]{};
 uint32_t auto_started_=0,auto_count_=0;
 void auto_restart(const ControllerState &state,uint32_t now);
 bool factory_valid_=false,zero_ready_=false,collecting_=false,done_held_=false;
};
