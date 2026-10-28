#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include "effects_runtime.h"
#include "config.h"
#include "state_mgr.h"
#include "utils.h"
uint32_t fake_now = 0;
int reportSeqCounter = 0;
bool connected = true, mounted = true, suspended = false, switch_mode = true;
unsigned flushes = 0, resets = 0, neutrals = 0, saves = 0;
std::vector<std::vector<uint8_t>> packets;
bool bt_is_connected() { return connected; }
bool tud_mounted() { return mounted; }
bool tud_suspended() { return suspended; }
bool usb_mode_is_switch() { return switch_mode; }
void bt_flush_output() { ++flushes; packets.clear(); }
void bt_write(const uint8_t *p, uint16_t n) { packets.emplace_back(p, p+n); }
void switch_rumble_adapter_stop() {}
void audio_reset_session() { ++resets; }
void update_mic_status() {}
void bridge_neutral_input() { ++neutrals; }
bool config_save() { ++saves; return true; }
Config_body &get_config() { static Config_body c{}; return c; }
void advance(uint32_t ms) { fake_now += ms; effects_task(); }
void input(uint8_t r2) { ControllerState s{}; s.r2 = r2; effects_input(s); }
void output(SetStateData &s) { uint8_t b[sizeof(s)]{}; state_set(b, sizeof(b)); std::memcpy(&s,b,sizeof(s)); }
bool console_connected=true;
bool switch_bt_connected() {return console_connected;}
int main() {
    mounted=false;suspended=true;switch_mode=false;
    state_init();TriggerSettings automatic{};effects_load(automatic);
    input(0);advance(20);SetStateData out{};output(out);
    assert(out.RightTriggerFFB[0]==0x21); // USB is suspended, Switch is wireless.
    assert(!out.MotionPowerSave && !out.HapticPowerSave);
    console_connected=false;advance(1);output(out);assert(out.RightTriggerFFB[0]==5);
    console_connected=true;input(255);advance(20);output(out);assert(out.RightTriggerFFB[0]==0x21);
    connected=false;advance(1);output(out);assert(out.RightTriggerFFB[0]==5);
    std::cout << "wireless effects without USB and disconnect safety passed\n";
}
