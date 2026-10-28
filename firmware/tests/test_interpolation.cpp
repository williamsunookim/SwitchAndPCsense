#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include "motion_resampler.h"
#include "haptic_calibration.h"
#include "switch_haptics_synth.h"

void motion_tests() {
    MotionResampler r;
    auto out = r.sample(0,0); assert(out.gyro[0]==0);
    ControllerState a{},b{},c{};
    a.gyro[0]=-1000; b.gyro[0]=1000; c.gyro[0]=3000;
    a.accel[2]=-8192; b.accel[2]=8192; c.accel[2]=24576;
    r.push(a,1000); r.push(b,9000); r.push(c,17000);
    assert(r.sample(5000,18000).gyro[0]==0);
    assert(r.sample(10000,18000).gyro[0]==1250);
    assert(r.sample(10000,18000).accel[2]==10240);
    assert(r.sample(0,18000).gyro[0]==-1000); // startup clamps earliest
    assert(r.sample(19000,18000).gyro[0]==3000); // hold, no extrapolation
    assert(r.sample(17000,57001).gyro[0]==0 && r.sample(17000,57001).accel[2]==24576); // radio gap: hold accel, never free-fall; no phantom rotation
    assert(r.sample(29000,29000).gyro[0]==3000); // 12ms past newest: still held
    assert(r.sample(29001,29001).gyro[0]==0 && r.sample(29001,29001).accel[2]==24576); // older: rotation stops
    assert(r.sample(10000,16999).gyro[0]==1250); // ms-flooring may precede newest arrival
    r.push(a,9000); assert(r.sample(17000,18000).gyro[0]==3000); // ignore late packet
    r.push(b,17000); assert(r.sample(17000,18000).gyro[0]==1000); // replace duplicate
    r.push(c,100000); assert(r.sample(95000,100001).gyro[0]==3000); // gap resets
    r.reset(); assert(r.sample(100000,100001).gyro[0]==0);
    a.gyro[0]=std::numeric_limits<int16_t>::min();
    b.gyro[0]=std::numeric_limits<int16_t>::max();
    r.push(a,0xfffff000u); r.push(b,0x1000u);
    auto middle=r.sample(0,0x2000);
    assert(middle.gyro[0]==0);
    for (uint32_t t=0;t<1000;++t) {
        c.gyro[0]=static_cast<int16_t>(t); r.push(c,10000+t*1000);
    }
    assert(r.sample(10000+995000,10000+999000).gyro[0]==995);
}
void wave_tests() {
    assert(haptic_sine_q15(0)==0);
    assert(haptic_sine_q15(0x40000000)==32767);
    assert(haptic_sine_q15(0x80000000)==0);
    assert(haptic_sine_q15(0xc0000000)==-32767);
    double error=0;
    for (uint32_t i=0;i<65536;++i) {
        const auto phase=i<<16;
        const double expected=std::sin(double(phase)*6.283185307179586/4294967296.0)*32767.0;
        error=std::max(error,std::abs(haptic_sine_q15(phase)-expected));
        assert(haptic_sine_q15(phase)==-haptic_sine_q15(phase+0x80000000u));
    }
    assert(error<4.0);
    std::cout << "quarter-wave interpolation max Q15 error: " << error << '\n';
}
void calibration_tests() {
    HapticCalibration config;
    assert(haptic_calibration_valid(config));
    assert(haptic_frequency_gain(config,40)==128);
    assert(haptic_frequency_gain(config,120)==237);
    assert(haptic_frequency_gain(config,160)==256);
    assert(haptic_frequency_gain(config,2000)==90);
    config.enabled=false; assert(haptic_frequency_gain(config,40)==256);
    SwitchHapticsSynth bypass,corrected;
    assert(bypass.configure_calibration(config));
    SwitchRumbleState s{}; s.left.low={40,255}; s.left.high={40,255};
    bypass.set_state(s); corrected.set_state(s);
    int8_t a[4096]{}, b[4096]{};
    bypass.render(a,2048); corrected.render(b,2048);
    int peak_a=0,peak_b=0;
    for (unsigned i=32;i<2048;++i) {
        peak_a=std::max(peak_a,std::abs(int(a[i*2])));
        peak_b=std::max(peak_b,std::abs(int(b[i*2])));
        assert(b[i*2+1]==0);
    }
    assert(peak_a>=125 && peak_b>=61 && peak_b<=64); // EQ must survive mix normalization
    config.enabled=true; config.left_trim_q8=0;
    assert(corrected.configure_calibration(config)); corrected.render(b,2048);
    for (auto v:b) assert(v==0);
    config.left_trim_q8=257; assert(!corrected.configure_calibration(config));
    config.left_trim_q8=256; config.gain_q8[2]=257;
    assert(!corrected.configure_calibration(config));
    corrected.reset(); assert(corrected.silent());
}
int main() {
    motion_tests(); wave_tests(); calibration_tests();
    std::cout << "interpolation and tactile correction tests passed\n";
}
