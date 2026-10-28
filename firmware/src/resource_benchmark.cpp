// SPDX-License-Identifier: MIT
#include "resource_benchmark.h"
#include <algorithm>
#include "hardware/clocks.h"
#include "pico/time.h"
#include "motion_resampler.h"
#include "switch_haptics_synth.h"
#include "bt.h"
#include "usb_mode.h"
#include "utils.h"
extern "C" char __flash_binary_end;
namespace {
uint32_t synth_max_us=0, motion_max_us=0, image_size=0, image_crc=0;
volatile int32_t benchmark_sink=0;
}
void resource_benchmark_init() {
    image_size = reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    image_crc = crc32_seeded(reinterpret_cast<const uint8_t *>(XIP_BASE),image_size,0);
    MotionResampler motion;
    ControllerState input{};
    for (unsigned i=0;i<32;++i) {
        for (unsigned j=0;j<3;++j) { input.gyro[j]=static_cast<int16_t>(i*100+j); input.accel[j]=static_cast<int16_t>(i*200-j); }
        motion.push(input,i*1000);
    }
    SwitchHapticsSynth synth;
    int8_t samples[64]{};
    SwitchRumbleState rumble{};
    for (unsigned iteration=0;iteration<64;++iteration) {
        const uint16_t frequency=static_cast<uint16_t>(80+iteration*17);
        const uint8_t amplitude=static_cast<uint8_t>(128+iteration);
        rumble.left.low={frequency,amplitude}; rumble.left.high={static_cast<uint16_t>(frequency+100),amplitude};
        rumble.right.low={static_cast<uint16_t>(1490-frequency),amplitude}; rumble.right.high={frequency,amplitude};
        uint64_t start=time_us_64();
        synth.set_state(rumble); synth.render(samples,32,512);
        synth_max_us=std::max<uint32_t>(synth_max_us,static_cast<uint32_t>(time_us_64()-start));
        benchmark_sink=samples[0];
        start=time_us_64();
        for (unsigned i=0;i<3;++i) {
            const auto state=motion.sample(20499-i*5000,31000);
            benchmark_sink=state.gyro[0]+state.accel[2];
        }
        motion_max_us=std::max<uint32_t>(motion_max_us,static_cast<uint32_t>(time_us_64()-start));
    }
}
void resource_diagnostics(uint8_t out[28]) {
    out[0]='P'; out[1]='W'; out[2]=1;
    out[3]=(usb_mode_is_switch()?1:0)|(bt_is_connected()?2:0)|
           ((synth_max_us>2000||motion_max_us>500)?8:0);
    const uint32_t values[6]={synth_max_us,motion_max_us,clock_get_hz(clk_sys)/1000,
                             to_ms_since_boot(get_absolute_time()),image_size,image_crc};
    for (unsigned i=0;i<6;++i) for (unsigned j=0;j<4;++j)
        out[4+i*4+j]=static_cast<uint8_t>(values[i]>>(j*8));
}
