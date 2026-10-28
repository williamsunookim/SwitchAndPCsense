#include "usb_mode.h"
#include "switch_bt.h"
#include <cassert>
#include <cstring>
#include <iostream>
uint32_t fake_now=0;
uint8_t saved_mode=0;
bool save_ok=true;
bool spk_active=false;
unsigned stops=0,changes=0,disconnects=0,connects=0,saves=0;
uint8_t config_get_usb_output_mode() {return saved_mode;}
void config_set_usb_output_mode(uint8_t m) {saved_mode=m;}
bool config_save() {++saves; return save_ok;}
void switch_pro_usb_init() {}
void switch_pro_usb_reset() {}
void effects_stop() {++stops;}
void set_mic_active(bool enabled) {assert(!enabled);}
bool tud_disconnect() {++disconnects;return true;}
bool tud_connect() {++connects;return true;}
void switch_bt_mode_changed() {++changes; assert(switch_bt_enabled()==(saved_mode==1));}
int main() {
    usb_mode_init();
    assert(!usb_mode_is_switch() && !switch_bt_enabled());
    assert(std::strcmp(usb_mode_name(),"PC DualSense")==0);
    usb_mode_toggle_and_reconnect();
    assert(!usb_mode_is_switch() && switch_bt_enabled() && saved_mode==1);
    assert(stops==1 && changes==1 && connects==1 && disconnects==1);
    usb_mode_init(); assert(switch_bt_enabled()); // saved selection restored
    usb_mode_set(UsbOutputMode::SwitchPro); assert(stops==1); // no-op preserves live state
    save_ok=false;
    usb_mode_toggle_and_reconnect();
    assert(!switch_bt_enabled() && saved_mode==0 && stops==2);
    const auto before=saves;
    fake_now+=1849; usb_mode_task(); assert(saves==before); // transition waited 150 ms
    save_ok=true; ++fake_now; usb_mode_task(); assert(saves==before+1);
    fake_now+=2000; usb_mode_task(); assert(saves==before+1);
    saved_mode=1; usb_mode_apply_config(); assert(switch_bt_enabled() && changes==3);
    std::cout << "PC USB / Switch Bluetooth mode switching tests passed\n";
}
