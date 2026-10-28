#include "switch2_wake.h"
#include "config.h"
#include <cassert>
#include <cstring>
#include <iostream>
uint32_t fake_now=0;
bool wireless=true;
bool usb_mode_is_wireless() {return wireless;}
Switch2WakeSettings saved{};
bool save_ok=true;
unsigned starts=0,stops=0;
unsigned scans=0,scan_stops=0;
void gap_set_scan_params(uint8_t type,uint16_t interval,uint16_t window,uint8_t policy) {assert(!type && interval>=window && !policy);}
void gap_set_scan_duplicate_filter(bool enabled) {assert(!enabled);}
void gap_start_scan() {++scans;}
void gap_stop_scan() {++scan_stops;}
const Switch2WakeSettings &config_get_switch2_wake() {return saved;}
void config_set_switch2_wake(const Switch2WakeSettings &s) {saved=s;}
bool config_save() {return save_ok;}
void gap_random_address_set_mode(int mode) {assert(mode==0);}
void gap_advertisements_set_params(uint16_t a,uint16_t b,uint8_t type,uint8_t address_type,uint8_t *,uint8_t channels,uint8_t filter) {
    assert(a==0x30 && b==0x30 && type==3 && address_type==0 && channels==7 && filter==0);
}
void gap_advertisements_set_data(uint8_t n,uint8_t *p) {assert(n==31 && p[16]==0x81);}
void gap_advertisements_enable(int enabled) {if(enabled) ++starts; else ++stops;}
int main() {
    switch2_wake_init(); switch2_wake_ready(); assert(!switch2_wake_request());
    const Switch2WakeSettings s{{0x98,0xe2,0x55,1,2,3},{0x98,0xe2,0x55,4,5,6},0x2069};
    assert(switch2_wake_configure(s));
    uint8_t status[24]{}; switch2_wake_status(status); assert((status[3]&7)==4 && status[4]==5);
    assert(!switch2_wake_request()); // not active until reboot/identity change
    switch2_wake_init();
    switch2_wake_input(true); switch2_wake_task(false); assert(starts==1);
    switch2_wake_input(true); switch2_wake_task(false); assert(starts==1);
    assert(!switch2_wake_configure({})); // do not change config during a burst
    fake_now=1999; switch2_wake_task(false); assert(stops==0);
    fake_now=2000; switch2_wake_task(false); assert(stops==1);
    assert(!switch2_wake_request());
    fake_now=7000; switch2_wake_neutral(); switch2_wake_input(true);
    switch2_wake_task(true); assert(starts==1); // connected console cancels request
    switch2_wake_neutral(); switch2_wake_input(true); switch2_wake_task(false);
    assert(starts==2);
    uint8_t error[]={0x0e,4,1,8,0x20,0x0c};
    switch2_wake_hci(error,5); assert(stops==1); // short event ignored
    switch2_wake_hci(error,6); assert(stops==2);
    switch2_wake_status(status); assert(status[5]==0x0c);
    save_ok=false; assert(!switch2_wake_configure({}));
    assert(std::memcmp(&saved,&s,sizeof(s))==0); // RAM rollback on flash failure
    save_ok=true; assert(switch2_wake_configure({}));
    assert(!switch2_wake_valid(saved));
    saved=s;
    fake_now=0xfffffff0u;
    assert(switch2_wake_request()); switch2_wake_task(false);
    assert(starts==3);
    fake_now+=2000; switch2_wake_task(false); assert(stops==3); // millisecond rollover
    wireless=false;
    fake_now+=5000; assert(!switch2_wake_request());
    wireless=true; assert(switch2_wake_request()); switch2_wake_task(false);
    switch2_wake_cancel(); assert(stops==4);
    // Learning works in PC mode and does not replace settings until the full
    // window completes, allowing multiple different donors to be rejected.
    wireless=false;
    uint8_t filter[6]{};
    uint8_t event[43]={0xda,41,0,0};
    for(unsigned i=0;i<6;++i)event[4+i]=s.source[5-i];
    event[11]=31; switch2_wake_payload(s,event+12);
    const auto old=saved;
    assert(switch2_wake_learn(5,filter)); assert(scans==1);
    switch2_wake_hci(event,42); // truncated report ignored
    switch2_wake_status(status); assert(!(status[3]&16));
    switch2_wake_hci(event,43);
    switch2_wake_status(status); assert((status[3]&24)==24);
    assert(std::memcmp(&saved,&old,sizeof(old))==0);
    fake_now+=5000;switch2_wake_task(false);
    switch2_wake_status(status);assert(status[4]==5 && !(status[3]&8) && scan_stops==1);
    assert(switch2_wake_learn(5,filter));
    switch2_wake_hci(event,43);event[4]^=1;switch2_wake_hci(event,43);
    fake_now+=5000;switch2_wake_task(false);
    switch2_wake_status(status);assert(status[4]==12);
    assert(std::memcmp(&saved,&old,sizeof(old))==0);
    assert(switch2_wake_learn(5,filter));
    event[3]=1; switch2_wake_hci(event,43); // unsupported random-address donor
    fake_now+=5000; switch2_wake_task(false);
    switch2_wake_status(status); assert(status[4]==11);
    assert(switch2_wake_learn(5,filter));switch2_wake_learn_cancel();
    switch2_wake_status(status);assert(status[4]==13 && !(status[3]&8));
    assert(switch2_wake_learn(5,filter));
    uint8_t scan_error[]={0x0e,4,1,0x0c,0x20,0x0c};
    switch2_wake_hci(scan_error,sizeof(scan_error));
    switch2_wake_status(status);assert(status[4]==7 && status[5]==0x0c && !(status[3]&8));
    event[3]=0;
    assert(switch2_wake_learn(5,s.source)); // event source differs from filter
    switch2_wake_hci(event,43);fake_now+=5000;switch2_wake_task(false);
    switch2_wake_status(status);assert(status[4]==11);
    save_ok=false;
    assert(switch2_wake_learn(5,filter));switch2_wake_hci(event,43);
    fake_now+=5000;switch2_wake_task(false);
    switch2_wake_status(status);assert(status[4]==6 && !(status[3]&8));
    assert(std::memcmp(&saved,&old,sizeof(old))==0);save_ok=true;
    wireless=true;assert(!switch2_wake_learn(5,filter));
    std::cout << "Switch 2 wake runtime tests passed\n";
}
