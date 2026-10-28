#include <cassert>
#include <iostream>
#include <vector>
#include "switch_rumble_adapter.h"
#include "switch_haptics_synth.h"
#include "config.h"
uint32_t fake_now = 1;
bool connected = true, mounted = true, suspended = false, switch_mode = true;
bool usb_mode_is_switch() { return switch_mode; }
unsigned events=0;
std::vector<int8_t> samples;
bool bt_is_connected() { return connected; }
bool tud_mounted() { return mounted; }
bool tud_suspended() { return suspended; }
void effects_rumble(uint8_t left, uint8_t right) { assert(left==255 && right==0); ++events; }
Config_body &get_config() { static Config_body c{}; c.haptics_gain=1; return c; }
void audio_send_haptics_pcm(const int8_t *s, size_t frames) {
    assert(frames==32); samples.insert(samples.end(),s,s+frames*2);
}
int main() {
    SwitchRumbleState state{}; state.left.low={160,255};
    switch_rumble_adapter_apply(state); switch_rumble_adapter_task(); assert(events==1);
    bool active=false;
    for (unsigned i=0;i<samples.size();i+=2) { active |= samples[i]!=0; assert(samples[i+1]==0); }
    assert(active);
    fake_now=502; samples.clear(); switch_rumble_adapter_task();
    fake_now=514; switch_rumble_adapter_task();
    for (unsigned i=32;i<samples.size();++i) assert(samples[i]==0);
    fake_now=526; switch_rumble_adapter_task();
    samples.clear(); fake_now=550; switch_rumble_adapter_task(); assert(samples.empty());
    connected=false; switch_rumble_adapter_apply(state); switch_rumble_adapter_task();
    connected=true; fake_now=560; switch_rumble_adapter_task(); assert(samples.empty());
    mounted=false; switch_rumble_adapter_apply(state); assert(events==1);
    mounted=true; fake_now=600; switch_rumble_adapter_task(); assert(samples.empty());
    switch_rumble_adapter_apply(state); switch_rumble_adapter_task(); assert(!samples.empty());
    samples.clear(); switch_rumble_adapter_stop();
    assert(samples.size()==64); for (auto v:samples) assert(v==0);
    samples.clear(); fake_now=620; switch_rumble_adapter_task(); assert(samples.empty());
    samples.clear(); switch_mode=false;
    const auto before=events;
    switch_rumble_adapter_apply(state); switch_rumble_adapter_task();
    assert(events==before && samples.empty()); // PC cannot activate synthesis
    switch_mode=true; switch_rumble_adapter_apply(state); switch_rumble_adapter_task();
    assert(!samples.empty());
    samples.clear(); switch_mode=false; fake_now+=20; switch_rumble_adapter_task();
    assert(samples.empty());
    switch_mode=true; fake_now+=20; switch_rumble_adapter_task();
    assert(samples.empty()); // no stale interpolation/effects on mode return
    std::cout << "haptics adapter tests passed\n";
}
