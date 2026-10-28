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
bool console_connected=true;
bool switch_bt_connected() {return console_connected;}
int main() {
    mounted=false;suspended=true;switch_mode=false;
    SwitchRumbleState state{};state.left.low={160,255};
    switch_rumble_adapter_apply(state);switch_rumble_adapter_task();assert(events==1 && !samples.empty());
    console_connected=false;samples.clear();fake_now+=20;switch_rumble_adapter_task();assert(samples.empty());
    switch_rumble_adapter_apply(state);assert(events==1);
    console_connected=true;fake_now+=20;switch_rumble_adapter_task();assert(samples.empty());
    switch_rumble_adapter_apply(state);switch_rumble_adapter_task();assert(events==2 && !samples.empty());
    connected=false;samples.clear();fake_now+=20;switch_rumble_adapter_task();assert(samples.empty());
    std::cout << "wireless haptics without USB and disconnect safety passed\n";
}
