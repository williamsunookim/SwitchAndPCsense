#include "switch2_pro_protocol.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
namespace {
const uint8_t a1[16]={0x35,3,0xe9,0x29,0x82,0x87,0x71,0x24,0xbe,0xa8,0x0c,0x66,0x46,0x15,0x83,0x4b};
const uint8_t a2[16]={0x6f,0xc6,0xdf,0x8a,0xd8,0xfe,0xdf,0x15,0xbb,0x8c,0x15,0xe9,0x1f,0x32,5,0x44};
const uint8_t ltk[16]={0x69,0xf5,7,0x50,0xae,0x58,0x74,0xc5,4,0x83,0x6f,0x43,0x82,0x0f,0xdc,0x5b};
const uint8_t b2[16]={0x13,0x4c,0x97,0xf5,0x11,0xb9,0xb6,0xdd,0x4d,0x86,0xfd,0x40,0xf5,0x36,0xe9,0xed};
void aes_vector(const uint8_t *key,const uint8_t *block,uint8_t *out){
    for(unsigned i=0;i<16;++i){assert(key[i]==ltk[15-i]);assert(block[i]==a2[15-i]);}
    std::memcpy(out,b2,16);
}
std::vector<uint8_t> request(uint8_t cmd,uint8_t sub,const std::vector<uint8_t> &data={}){
    std::vector<uint8_t> p={cmd,0x91,1,sub,0,uint8_t(data.size()),0,0};p.insert(p.end(),data.begin(),data.end());return p;
}
}
int main(){
    Switch2ProProtocol protocol;const uint8_t local[6]={0x7c,0xbb,0x8a,1,2,3};protocol.reset(local);
    uint8_t reply[224]{};size_t length=0;
    auto run=[&](const std::vector<uint8_t> &p){return protocol.command(p.data(),p.size(),reply,length,aes_vector);};
    assert(!run(request(0x15,3,{0})) && !protocol.paired());
    const auto addr=request(0x15,1,{0,2,0x81,0xeb,0x3a,0xeb,0xf1,0x48,0x80,0xeb,0x3a,0xeb,0xf1,0x48});
    assert(run(addr));assert(length==17 && reply[8]==1 && reply[11]==3 && reply[16]==0x7c);
    std::vector<uint8_t> key(17),challenge(17);std::memcpy(key.data()+1,a1,16);std::memcpy(challenge.data()+1,a2,16);
    assert(!run(request(0x15,2,challenge))); // cannot skip key exchange
    assert(run(request(0x15,4,key)));assert(reply[9]==0x5c && length==25);
    assert(run(request(0x15,2,challenge)));assert(std::memcmp(reply+9,b2,16)==0);
    assert(run(request(0x15,3,{0})));assert(protocol.paired());
    Switch2ProBond bond{};assert(protocol.take_bond(bond));assert(!protocol.take_bond(bond));
    assert(std::memcmp(bond.key,ltk,16)==0 && bond.hosts[0][0]==0x48 && bond.hosts[0][5]==0x81);
    protocol.reset(local);protocol.restore(bond);assert(protocol.paired());
    assert(run(request(0x10,1)));assert(reply[11]==2); // controller type = Pro
    assert(!run(request(0x0d,1,{1,2,3}))); // do not acknowledge nonexistent firmware writes
    assert(run(request(2,4,{64,0x7e,0,0,0,0x30,1,0})));
    assert(length==80 && reply[8]==64 && reply[8+8+18]==0x7e && reply[8+8+20]==0x69);
    auto invalid=request(2,4,{255,0,0,0,0,0x30,1,0});assert(!run(invalid));
    invalid=request(2,4,{64,0,0,0,0xf0,0xff,0x1f,0});assert(!run(invalid));
    for(size_t n=0;n<8;++n){invalid=addr;invalid.resize(n);assert(!run(invalid));}
    invalid=addr;invalid.pop_back();assert(!run(invalid));
    ControllerState input{};input.buttons=CONTROLLER_BUTTON_CROSS|CONTROLLER_BUTTON_HOME|CONTROLLER_BUTTON_MUTE;
    input.dpad=CONTROLLER_DPAD_UP;input.gyro[2]=200;input.accel[2]=8192;protocol.update(input);
    uint8_t report[63];protocol.input(9,123456,report);
    assert(report[2]==1 && report[3]==8 && report[4]==17 && report[6]==8 && report[7]==0x80);
    assert(report[14]==0); // no fabricated compressed IMU frames
    assert(run(request(0x0c,2,{0x2f,0,0,0})));protocol.input(5,123456,report);
    assert(report[4]==4 && report[6]==2 && report[42]==0x40 && report[43]==0xe2);
    assert(report[52]==0 && report[53]==16); // +/-8G common-report scale
    assert(report[54]==0x38 && report[55]==0xff); // gyro Z maps to -X
    assert(run(request(3,0x0a,{5,0,0,0})));assert(protocol.selected_report()==5);
    // Independent Linux/SDL packet example: four 10-bit fields, not old rumble.
    uint8_t packet[33]{};packet[1]=packet[17]=0x50;
    const uint8_t encoded[5]={0x87,0xc5,0x21,0x91,0x38};std::memcpy(packet+2,encoded,5);
    Switch2RumblePacket decoded{};assert(switch2_decode_rumble(packet,33,decoded));
    assert(decoded.left[0].frequency[0]==391 && decoded.left[0].amplitude[0]==113);
    assert(decoded.left[0].frequency[1]==274 && decoded.left[0].amplitude[1]==226);
    assert(decoded.right[0].amplitude[0]==0);
    assert(!switch2_decode_rumble(packet,32,decoded));packet[1]=0xf0;assert(!switch2_decode_rumble(packet,33,decoded));
    packet[1]=0x50;std::memset(packet+2,255,5);assert(switch2_decode_rumble(packet,33,decoded));
    SwitchRumbleState approximate[3]{};switch2_approximate_rumble(decoded,approximate);
    assert(approximate[0].left.high.amplitude==255 && approximate[1].silent());
    std::cout << "Pro Controller 2 pairing, reports and 10-bit rumble tests passed\n";
}
