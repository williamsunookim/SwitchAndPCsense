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
int main() {
    static_assert(sizeof(SetStateData) == 63);
    static_assert(offsetof(SetStateData, RightTriggerFFB) == 10);
    static_assert(offsetof(SetStateData, LeftTriggerFFB) == 21);
    state_init(); effects_load(botw_bow_settings());
    input(100); advance(20);
    SetStateData out{}; output(out); assert(out.RightTriggerFFB[0] == 0x21);
    assert(out.LeftTriggerFFB[0] == 5 && !packets.empty());
    assert(out.AllowAudioMute && !out.MotionPowerSave && !out.HapticPowerSave);
    input(0); advance(20); output(out); assert(out.RightTriggerFFB[0] == 0x21);
    input(100); advance(20); advance(500);
    output(out); assert(out.RightTriggerFFB[0] == 5);
    assert(flushes && resets && neutrals);
    input(100); advance(20); output(out); assert(out.RightTriggerFFB[0] == 0x21);
    mounted = false; advance(1); output(out); assert(out.RightTriggerFFB[0] == 5);
    mounted = true; input(100); advance(20);
    suspended = true; advance(1); output(out); assert(out.RightTriggerFFB[0] == 5);
    suspended = false; input(100); advance(20);
    connected = false; advance(1); output(out); assert(out.RightTriggerFFB[0] == 5);
    connected = true; state_init(); input(100); advance(20);
    switch_mode = false;
    SetStateData host{}; host.AllowRightTriggerFFB = 1; host.RightTriggerFFB[0] = 0x22;
    host.RightTriggerFFB[1] = 0x84;
    state_update(reinterpret_cast<const uint8_t*>(&host), sizeof(host));
    output(out); assert(out.RightTriggerFFB[0] == 0x22 && out.RightTriggerFFB[1] == 0x84);
    host = {}; state_update(reinterpret_cast<const uint8_t*>(&host), sizeof(host));
    for (unsigned i=0;i<100;++i) { input(100); advance(100); }
    output(out); assert(out.RightTriggerFFB[0] == 0x22);
    host.AllowRightTriggerFFB = 1; host.RightTriggerFFB[0] = 5;
    state_update(reinterpret_cast<const uint8_t*>(&host), sizeof(host));
    output(out); assert(out.RightTriggerFFB[0] == 0x21);
    uint8_t guard[65]; std::memset(guard, 0xa5, sizeof(guard));
    state_set(guard+1, 63); assert(guard[0] == 0xa5 && guard[64] == 0xa5);
    for (size_t i=1+sizeof(SetStateData); i<64; ++i) assert(guard[i] == 0);
    host.RightTriggerFFB[0] = 0x22;
    state_update(reinterpret_cast<const uint8_t*>(&host), 1);
    output(out); assert(out.RightTriggerFFB[0] == 0x21);
    auto s=botw_bow_settings(); assert(effects_configure(s));
    advance(1000); assert(saves == 1);
    effects_stop(); output(out); assert(out.RightTriggerFFB[0] == 5);
    std::cout << "effects runtime tests passed\n";
}
