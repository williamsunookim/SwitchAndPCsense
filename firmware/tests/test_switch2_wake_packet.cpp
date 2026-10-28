#include "switch2_wake.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
    Switch2WakeSettings s{{0x98,0xe2,0x55,0xae,0xf7,0xd8},
                          {0x98,0xe2,0x55,0x99,0x1f,0x97},0x2066};
    assert(switch2_wake_valid(s));
    uint8_t out[31]{}; switch2_wake_payload(s,out);
    const uint8_t expected[31]={2,1,6,27,255,0x53,5,1,0,3,0x7e,5,0x66,0x20,0,1,0x81,
        0x97,0x1f,0x99,0x55,0xe2,0x98,15,0,0,0,0,0,0,0};
    assert(std::memcmp(out,expected,31)==0);
    Switch2WakeSettings decoded{};
    assert(switch2_wake_decode(s.source,expected,sizeof(expected),decoded));
    assert(std::memcmp(&s,&decoded,sizeof(s))==0);
    assert(!switch2_wake_decode(s.source,expected,30,decoded));
    uint8_t bad[31]; std::memcpy(bad,expected,31);bad[16]=0;
    assert(!switch2_wake_decode(s.source,bad,31,decoded)); // reconnection != wake
    std::memcpy(bad,expected,31);bad[3]=255;
    assert(!switch2_wake_decode(s.source,bad,31,decoded));
    std::memcpy(bad,expected,31);bad[30]=1;
    assert(!switch2_wake_decode(s.source,bad,31,decoded));
    for (auto pid : {0x2067,0x2069}) {
        s.product=uint16_t(pid); assert(switch2_wake_valid(s));
        switch2_wake_payload(s,out); assert(out[12]==uint8_t(pid));
    }
    s.product=0x2009; assert(!switch2_wake_valid(s));
    s.product=0x2069; std::memset(s.source,0,6); assert(!switch2_wake_valid(s));
    std::memset(s.source,0xff,6); assert(!switch2_wake_valid(s));
    std::memcpy(s.source,s.target,6); assert(!switch2_wake_valid(s));
    assert(!switch2_wake_valid({}));
    std::cout << "Switch 2 wake packet tests passed\n";
}
