#include <cassert>
#include <cstring>
#include <iostream>
#include "trigger_engine.h"
int main() {
    TriggerEngine e;
    auto s = botw_bow_settings();
    assert(trigger_settings_valid(s)); e.configure(s);
    assert(e.render(1, false, 0)[0] == 0x21);
    e.input(0, 32);
    auto bow = e.render(1, false, 0);
    assert(bow[0] == 0x21 && bow[1] == 0xfc && bow[2] == 3);
    assert(e.render(0, false, 0)[0] == 5);
    e.input(0, 20); assert(e.render(1, false, 1) == bow);
    e.input(0, 16); assert(e.render(1, false, 2)[0] == 0x21);
    e.input(0, 20); assert(e.render(1, false, 3)[0] == 0x21);
    e.input(0, 32);
    uint8_t native[11] = {0x22, 4, 1, 5, 9, 8, 7, 6, 5, 4, 3};
    e.native(1, native);
    for (uint32_t t : {0u, 3001u, 1000000u})
        assert(std::memcmp(e.render(1, true, t).data(), native, 11) == 0);
    assert(e.render(1, false, 1) == bow);
    s.force = 1; e.configure(s); assert(e.render(1, true, 2) == bow);
    s.force = 0; e.configure(s); assert(e.render(1, true, 3)[0] == 0x22);
    native[0] = 5; e.native(1, native); assert(e.render(1, true, 4) == bow);
    s.right.kick_strength = 4; s.right.kick_ms = 60; e.configure(s);
    e.input(0, 0); assert(e.render(1, false, 100)[0] == 0x26);
    assert(e.render(1, false, 159)[0] == 0x26);
    assert(e.render(1, false, 160)[0] == 0x21);
    s.left = s.right; s.left.mode = 1; s.left.curve[3] = 0;
    e.configure(s); e.input(100, 0);
    assert((e.render(0, false, 200)[1] & 8) == 0);
    s.right.mode = 4; s.right.rumble_strength = 8; e.configure(s);
    e.input(0, 100); e.rumble(0, 255, 300);
    assert(e.render(1, false, 300)[0] == 0x26);
    for (uint32_t t = 310; t < 500; t += 10) {
        e.rumble(0, 255, t);
        if (t >= 360) assert(e.render(1, false, t)[0] == 5);
    }
    e.rumble(0, 0, 500); e.rumble(0, 255, 510);
    assert(e.render(1, false, 510)[0] == 0x26);
    assert(e.render(1, false, 700)[0] == 5);
    s.right.mode = 3; s.right.kick_ms = 40; e.configure(s); e.reset();
    e.rumble(0, 255, 0xfffffff0u);
    assert(e.render(1, false, 4)[0] == 0x26);
    assert(e.render(1, false, 24)[0] == 5);
    s.right.curve[2] = 9; assert(!trigger_settings_valid(s));
    e.configure(s); assert(e.settings().right.curve[2] != 9);
    e.reset(); assert(e.render(1, true, 10)[0] == 5);
    TriggerEngine automatic;
    assert(automatic.render(1,false,0)[0]==0x21);
    assert(automatic.render(1,true,0)[0]==5);
    auto disabled=botw_bow_settings();disabled.profile=3;automatic.configure(disabled);
    assert(automatic.render(1,false,0)[0]==5);
    std::cout << "trigger engine tests passed\n";
}
