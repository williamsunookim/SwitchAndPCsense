#include <cassert>
#include <cstring>
#include <iostream>
#include "switch_pro_usb.h"
#include "switch_rumble.h"
uint32_t fake_now=0;
bool switch_mode=false;
unsigned sends=0, rumble_tasks=0, rumble_events=0;
uint8_t last[64]{};
bool usb_mode_is_switch() { return switch_mode; }
bool tud_hid_ready() { return true; }
bool tud_hid_report(uint8_t, const void *data, uint16_t size) {
    assert(size==64); std::memcpy(last,data,64); ++sends; return true;
}
void switch_rumble_adapter_stop() {}
void switch_rumble_adapter_task() { ++rumble_tasks; }
void switch_rumble_adapter_apply(const SwitchRumbleState &) { ++rumble_events; }
void command(uint8_t value) {
    switch_pro_usb_set_report(0x80,HID_REPORT_TYPE_OUTPUT,&value,1);
}
int main() {
    switch_pro_usb_init();
    ControllerState state{}; state.r2=255; state.gyro[0]=16000;
    switch_pro_usb_update(state); command(4); switch_pro_usb_task();
    assert(sends==0 && rumble_tasks==0 && rumble_events==0);
    switch_mode=true;
    switch_pro_usb_task(); assert(last[0]==0x81); // PC command was rejected
    command(2); switch_pro_usb_task();
    command(4); switch_pro_usb_task();
    fake_now=20; switch_pro_usb_task();
    assert(last[0]==0x30 && !(last[3]&0x80)); // PC input never entered motion/input state
    switch_pro_usb_update(state); fake_now=40; switch_pro_usb_task();
    assert(last[3]&0x80);
    const auto before=sends, tasks=rumble_tasks;
    switch_mode=false; fake_now=60;
    switch_pro_usb_update(ControllerState{}); command(4); switch_pro_usb_task();
    assert(sends==before && rumble_tasks==tasks);
    std::cout << "PC/Switch interpolation isolation tests passed\n";
}
