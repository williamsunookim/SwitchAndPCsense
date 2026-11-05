#include "switch_rumble_adapter.h"
#include "switch_haptics_synth.h"
#include "config.h"
#include <cassert>
#include <vector>
#include <iostream>
uint32_t fake_now=1;
bool wireless=true,connected=true;
std::vector<int8_t> samples;
bool usb_mode_is_switch(){return false;}
bool usb_mode_is_wireless(){return wireless;}
bool bt_is_connected(){return connected;}
bool switch_bt_connected(){return connected;}
bool tud_mounted(){return true;}
bool tud_suspended(){return false;}
Config_body &get_config(){static Config_body c{};c.haptics_gain=1;return c;}
void effects_rumble(uint8_t,uint8_t){}
void audio_send_haptics_pcm(const int8_t *p,size_t n){samples.insert(samples.end(),p,p+n*2);}
int main(){
    SwitchRumbleState sequence[3]{};sequence[1].right.high={300,255};
    switch_rumble_adapter_apply_sequence(sequence);switch_rumble_adapter_task();
    assert(samples.size()==64);
    bool right=false;for(unsigned i=0;i<32;++i){assert(samples[i*2]==0);if(samples[i*2+1])right=true;}
    assert(right); // silent first frame must not discard a later active frame
    fake_now+=12;switch_rumble_adapter_task();
    switch_rumble_adapter_stop();samples.clear();wireless=false;
    switch_rumble_adapter_apply_sequence(sequence);switch_rumble_adapter_task();assert(samples.empty());
    wireless=true;connected=false;switch_rumble_adapter_apply_sequence(sequence);switch_rumble_adapter_task();assert(samples.empty());
    std::cout << "Pro Controller 2 scheduled PCM haptics and mode isolation passed\n";
}
