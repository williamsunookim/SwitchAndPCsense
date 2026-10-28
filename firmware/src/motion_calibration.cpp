// SPDX-License-Identifier: MIT
// Report layout/units checked against Sony support in Linux, implemented independently.
#include "motion_calibration.h"
#include <algorithm>
#include <cstdlib>
namespace {
int16_t read16(const uint8_t *p) { return static_cast<int16_t>(uint16_t(p[0])|(uint16_t(p[1])<<8)); }
uint32_t crc_byte(uint32_t crc,uint8_t value) {
 crc^=value;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));return crc;
}
int16_t correct(int32_t raw,int32_t gain) {
 const int64_t value=static_cast<int64_t>(raw)*gain;
 return static_cast<int16_t>(std::clamp<int64_t>((value+(value>=0?32768:-32768))/65536,-32768,32767));
}
}
void MotionCalibration::reset() { *this=MotionCalibration{}; }
bool MotionCalibration::load_factory(const uint8_t *r,size_t n) {
 if (!r || n!=41 || r[0]!=5) return false;
 uint32_t crc=crc_byte(0xffffffffu,0xa3);
 for(unsigned i=0;i<37;++i)crc=crc_byte(crc,r[i]);
 const uint32_t received=uint32_t(r[37])|(uint32_t(r[38])<<8)|(uint32_t(r[39])<<16)|(uint32_t(r[40])<<24);
 if (~crc!=received) return false;
 const int32_t speed=int32_t(read16(r+19))+read16(r+21);
 if(speed<=0 || speed>4000) return false;
 int32_t gains[3],accels[3];int16_t offsets[3],biases[3];
 for(unsigned i=0;i<3;++i) {
  const int32_t bias=read16(r+1+i*2),plus=read16(r+7+i*4),minus=read16(r+9+i*4);
  const int32_t span=std::abs(plus-bias)+std::abs(minus-bias);
  const int32_t ap=read16(r+23+i*4),am=read16(r+25+i*4),ar=ap-am;
  if(span<=0 || ar<8192 || ar>32768) return false;
  gains[i]=static_cast<int32_t>((static_cast<int64_t>(speed)*16*65536)/span);
  accels[i]=static_cast<int32_t>((int64_t(16384)*65536)/ar);
  offsets[i]=static_cast<int16_t>(ap-ar/2);biases[i]=static_cast<int16_t>(bias);
  if(gains[i]<32768 || gains[i]>131072) return false;
 }
 for(unsigned i=0;i<3;++i) { gyro_gain_[i]=gains[i];gyro_bias_[i]=biases[i];accel_gain_[i]=accels[i];accel_offset_[i]=offsets[i];zero_[i]=0; }
 factory_valid_=true;zero_ready_=false;auto_count_=0;return true;
}
void MotionCalibration::auto_restart(const ControllerState &s,uint32_t now) {
 auto_started_=now;auto_count_=0;
 for(unsigned i=0;i<3;++i) { auto_sums_[i]=0;auto_min_[i]=auto_max_[i]=s.gyro[i];auto_min_[3+i]=auto_max_[3+i]=s.accel[i]; }
}
void MotionCalibration::apply(ControllerState &state,uint32_t now) {
 for(unsigned i=0;i<3;++i) {
  // Same model as Linux hid-playstation: (raw - factory bias) * sensitivity.
  state.gyro[i]=correct(int32_t(state.gyro[i])-gyro_bias_[i],gyro_gain_[i]);
  state.accel[i]=correct(int32_t(state.accel[i])-accel_offset_[i],accel_gain_[i]);
 }
 const uint32_t chord=CONTROLLER_BUTTON_MUTE|CONTROLLER_BUTTON_CREATE;
 const bool requested=(state.buttons&chord)==chord;
 if(!requested) { collecting_=false;done_held_=false;count_=0; }
 if(requested && !done_held_) {
  const int64_t gravity=int64_t(state.accel[0])*state.accel[0]+int64_t(state.accel[1])*state.accel[1]+int64_t(state.accel[2])*state.accel[2];
  bool still=gravity>=int64_t(6144)*6144 && gravity<=int64_t(10240)*10240;
  for(auto g:state.gyro) still=still && std::abs(int(g))<=320;
  if(!collecting_ || !still) { count_=0;started_=now;for(unsigned i=0;i<3;++i) { sums_[i]=0;minimum_[i]=maximum_[i]=state.gyro[i]; } }
  collecting_=still;
  if(still) {
   for(unsigned i=0;i<3;++i) {
    minimum_[i]=std::min(minimum_[i],state.gyro[i]);maximum_[i]=std::max(maximum_[i],state.gyro[i]);
    if(int(maximum_[i])-minimum_[i]>32) { count_=0;started_=now;for(unsigned j=0;j<3;++j) { sums_[j]=0;minimum_[j]=maximum_[j]=state.gyro[j]; }break; }
   }
   for(unsigned i=0;i<3;++i)sums_[i]+=state.gyro[i];
   ++count_;
   if(now-started_>=2000 && count_>=64) {
    for(unsigned i=0;i<3;++i)zero_[i]=static_cast<int16_t>(sums_[i]/static_cast<int32_t>(count_));
    zero_ready_=true;done_held_=true;collecting_=false;
   }
  }
 }
 // Auto re-zero: within 2 deg/s of the current zero, gyro spread <=1.5 deg/s and
 // accel spread <=0.05 g near 1 g for 1.5 s. A deliberate slow pan rarely stays
 // that steady; a controller set down or held still always does.
 if(!collecting_) {
  const int64_t gravity=int64_t(state.accel[0])*state.accel[0]+int64_t(state.accel[1])*state.accel[1]+int64_t(state.accel[2])*state.accel[2];
  bool still=gravity>=int64_t(6144)*6144 && gravity<=int64_t(10240)*10240;
  for(unsigned i=0;i<3;++i) still=still && std::abs(int(state.gyro[i])-zero_[i])<=32;
  if(!still || !auto_count_) auto_restart(state,now);
  if(still) {
   bool steady=true;
   for(unsigned i=0;i<6;++i) {
    const int16_t v=i<3?state.gyro[i]:state.accel[i-3];
    auto_min_[i]=std::min(auto_min_[i],v);auto_max_[i]=std::max(auto_max_[i],v);
    steady=steady && int(auto_max_[i])-auto_min_[i]<=(i<3?24:410);
   }
   if(!steady) auto_restart(state,now);
   for(unsigned i=0;i<3;++i)auto_sums_[i]+=state.gyro[i];
   ++auto_count_;
   if(now-auto_started_>=1500 && auto_count_>=48) {
    for(unsigned i=0;i<3;++i)zero_[i]=static_cast<int16_t>(auto_sums_[i]/static_cast<int32_t>(auto_count_));
    auto_restart(state,now);
   }
  }
 }
 for(unsigned i=0;i<3;++i) { state.gyro[i]=static_cast<int16_t>(std::clamp<int32_t>(int32_t(state.gyro[i])-zero_[i],-32768,32767));last_gyro_[i]=state.gyro[i]; }
}
