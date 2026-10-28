#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "motion_calibration.h"
#include "output_sequence.h"
#include "send_gate.h"
uint32_t crcbyte(uint32_t c,uint8_t b) { c^=b;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));return c; }
void put(uint8_t *p,int16_t v) {p[0]=uint8_t(v);p[1]=uint8_t(uint16_t(v)>>8);}
void sign(uint8_t *r) {uint32_t c=crcbyte(0xffffffffu,0xa3);for(unsigned i=0;i<37;++i)c=crcbyte(c,r[i]);c=~c;for(unsigned i=0;i<4;++i)r[37+i]=uint8_t(c>>(8*i));}
int main() {
 bool waiting=false;
 request_send(waiting,[&] {waiting=false;return 0;});assert(!waiting);
 request_send(waiting,[] {return 0;});assert(waiting);
 bool duplicate=false;request_send(waiting,[&] {duplicate=true;return 0;});assert(!duplicate);
 waiting=false;request_send(waiting,[] {return 1;});assert(!waiting);
 MotionCalibration m;uint8_t report[41]{5};
 put(report+19,540);put(report+21,540);
 for(unsigned i=0;i<3;++i) {put(report+7+4*i,8640);put(report+9+4*i,-8640);put(report+23+4*i,8292);put(report+25+4*i,-8092);}
 sign(report);assert(m.load_factory(report,41));assert(m.status()==1);
 ControllerState s{};s.accel[2]=8292;s.gyro[0]=160;m.apply(s,0);assert(s.accel[2]==8192 && s.gyro[0]==160);
 report[10]^=1;assert(!m.load_factory(report,41));assert(!m.load_factory(report,40));
 for(uint32_t t=0;t<=2100;t+=10) {s={};s.accel[2]=8292;s.gyro[0]=160;s.gyro[1]=-48;s.buttons=CONTROLLER_BUTTON_MUTE|CONTROLLER_BUTTON_CREATE;m.apply(s,t);}
 assert(m.status()&2);assert(s.gyro[0]==0 && s.gyro[1]==0);
 s={};s.accel[2]=8292;s.gyro[0]=320;m.apply(s,2200);assert(s.gyro[0]==160);
 m.reset();assert(m.status()==0);
 for(uint32_t t=0;t<=2500;t+=10) {s={};s.accel[2]=8192;s.gyro[0]=(t%20)?0:100;s.buttons=CONTROLLER_BUTTON_MUTE|CONTROLLER_BUTTON_CREATE;m.apply(s,t);}
 assert(!(m.status()&2));
 // Factory gyro bias is subtracted before the sensitivity gain.
 m.reset();for(unsigned i=0;i<3;++i)put(report+1+2*i,int16_t(-7+3*int(i)));sign(report);assert(m.load_factory(report,41));
 s={};s.accel[2]=8292;s.gyro[0]=-7;s.gyro[1]=-4;s.gyro[2]=-1;m.apply(s,0);assert(s.gyro[0]==0 && s.gyro[1]==0 && s.gyro[2]==0);
 // Residual drift at rest is re-zeroed automatically, without the chord.
 for(uint32_t t=0;t<=1600;t+=4) {s={};s.accel[2]=8292+int16_t(t%3);s.gyro[0]=int16_t(-7+12+(t%8?0:2));s.gyro[2]=int16_t(-1-9);m.apply(s,t);}
 s={};s.accel[2]=8292;s.gyro[0]=-7+12;s.gyro[2]=-1-9;m.apply(s,1700);assert(std::abs(s.gyro[0])<=1 && std::abs(s.gyro[2])<=1);
 // Real rotation and a steady slow pan are never absorbed into the zero.
 m.reset();assert(m.load_factory(report,41));
 for(uint32_t t=0;t<=4000;t+=4) {s={};s.accel[2]=8292;s.gyro[1]=-4+200;m.apply(s,t);}
 assert(s.gyro[1]==200);
 for(uint32_t t=0;t<=4000;t+=4) {s={};s.accel[2]=8292;s.gyro[0]=-7+int16_t((t/40)%2?20:-20);m.apply(s,t+5000);}
 assert(m.zero()[0]==0);
 OutputSequence sequence;uint8_t packet[2]{};
 for(unsigned i=0;i<35;++i) {packet[0]=uint8_t(i%3==0?0x31:(i%3==1?0x32:0x36));packet[1]=0x0b;sequence.stamp(packet,2);assert(packet[1]==uint8_t(((i&15)<<4)|0x0b));}
 packet[0]=0x01;packet[1]=0x57;sequence.stamp(packet,2);assert(packet[1]==0x57);
 std::cout << "motion calibration and output sequence tests passed\n";
}
