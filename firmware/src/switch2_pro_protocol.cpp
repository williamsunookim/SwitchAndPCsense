// SPDX-License-Identifier: MIT
// Protocol facts: ndeadly/switch2_controller_research, Linux hid-nintendo,
// SDL hidapi_switch2, and esp-cpp/espp. See docs/SWITCH2_PRO.md.
#include "switch2_pro_protocol.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t component[16]={0x5c,0xf6,0xee,0x79,0x2c,0xdf,0x05,0xe1,0xba,0x2b,0x63,0x25,0xc4,0x1a,0x5f,0x10};
uint32_t u32(const uint8_t *p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
void put16(uint8_t *p,int32_t n) {const auto v=uint16_t(std::clamp<int32_t>(n,-32768,32767));p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
void stick(uint8_t *p,uint16_t x,uint16_t y) {
    x>>=4;y=uint16_t(std::min<uint32_t>(65535,65536u-y)>>4);p[0]=uint8_t(x);p[1]=uint8_t((x>>8)|(y<<4));p[2]=uint8_t(y>>4);
}
bool address_ok(const uint8_t *a) {
    uint8_t any=0,nonff=0;for(unsigned i=0;i<6;++i){any|=a[i];nonff|=a[i]^255;}return any && nonff;
}
void factory(uint32_t address,uint8_t *out,size_t n,const uint8_t local[6]) {
    // Synthetic device data and centered calibration, not another pad's identity.
    uint8_t info[64];std::memset(info,255,sizeof(info));
    info[0]=1;info[1]=0;
    const char hex[]="0123456789ABCDEF";
    std::memset(info+2,0,16);info[2]='P';info[3]='2';
    for(unsigned i=0;i<6;++i){info[4+i*2]=hex[local[i]>>4];info[5+i*2]=hex[local[i]&15];}
    info[18]=0x7e;info[19]=5;info[20]=0x69;info[21]=0x20;
    info[22]=1;info[23]=6;info[24]=1;
    for(unsigned i=25;i<37;++i)info[i]=0x23;
    const uint8_t prefix[40]={1,0xad,0xd9,0x9a,0x55,0x56,0x65,0xa0,0,0x0a,0xa0,0,0x0a,0xe2,0x20,0x0e,
        0xe2,0x20,0x0e,0x9a,0xad,0xd9,0x9a,0xad,0xd9,0x0a,0xa5,0x50,0x0a,0xa5,0x50,0x2f,0xf6,0x62,0x2f,0xf6,0x62,0x0a,255,255};
    // Neutral (2048,2048), negative/positive spans (1536,1536).
    const uint8_t calib[9]={0,8,0x80,0,6,0x60,0,6,0x60};
    const uint8_t unknown40[16]={0x3b,0xe0,0xd3,0x41,0xc6,0x60,0x6a,0xbc,0x4d,0xd7,0xa2,0xbb,0x71,0x1e,0xdd,0x37};
    const uint8_t unknown100[24]={0,0,0,0,0,0,0,0,0,0,0,0,0xa6,0xf2,0x62,0xbd,0xa8,0,8,0x3d,0x2f,0xed,0x20,0x41};
    for(size_t i=0;i<n;++i) {
        const uint32_t a=address+uint32_t(i);out[i]=255;
        if(a>=0x13000 && a<0x13040)out[i]=info[a-0x13000];
        else if(a>=0x13040 && a<0x13050)out[i]=unknown40[a-0x13040];
        else if((a>=0x13080 && a<0x130a8)||(a>=0x130c0 && a<0x130e8))out[i]=prefix[(a-0x13080)%64];
        else if((a>=0x130a8 && a<0x130b1)||(a>=0x130e8 && a<0x130f1))out[i]=calib[(a-0x130a8)%64];
        else if(a>=0x13100 && a<0x13118)out[i]=unknown100[a-0x13100];
    }
}
}
void Switch2ProProtocol::reset(const uint8_t local[6]) {
    state_={};bond_={};pending_={};std::memcpy(bond_.local,local,6);std::memcpy(pending_.local,local,6);
    counter_=0;stage_=0;mask_=0x2f;enabled_=0;selected_report_=9;paired_=bond_dirty_=false;
}
void Switch2ProProtocol::restore(const Switch2ProBond &bond) {bond_=bond;paired_=true;}
bool Switch2ProProtocol::take_bond(Switch2ProBond &out) {
    if(!bond_dirty_)return false;out=bond_;bond_dirty_=false;return true;
}
bool Switch2ProProtocol::command(const uint8_t *p,size_t n,uint8_t *out,size_t &length,Switch2Aes aes) {
    length=0;
    if(n<8 || p[1]!=0x91 || p[2]!=1 || p[6] || p[7] || n-8<p[5])return false;
    const uint8_t cmd=p[0],sub=p[3];const auto *d=p+8;const size_t size=p[5];
    std::memset(out,0,224);out[0]=cmd;out[1]=1;out[2]=1;out[3]=sub;out[4]=0x10;out[5]=0x78;
    auto *r=out+8;size_t len=0;
    if(cmd==0x15) {
        if(sub==1) {
            if(size) {
                if(size<8 || d[0] || !d[1] || d[1]>2 || size<size_t(2+6*d[1]))return false;
                pending_={};std::memcpy(pending_.local,bond_.local,6);
                for(unsigned h=0;h<d[1];++h)for(unsigned i=0;i<6;++i)pending_.hosts[h][i]=d[2+h*6+5-i];
                if(!address_ok(pending_.hosts[0]))return false;
                stage_=1;
            }
            r[0]=1;r[1]=4;r[2]=1;for(unsigned i=0;i<6;++i)r[3+i]=bond_.local[5-i];len=9;
        } else if(sub==4) {
            if(stage_!=1 || size!=17 || d[0])return false;
            for(unsigned i=0;i<16;++i)pending_.key[i]=d[1+i]^component[i];
            r[0]=1;std::memcpy(r+1,component,16);len=17;stage_=2;
        } else if(sub==2) {
            if(stage_!=2 || size!=17 || d[0] || !aes)return false;
            uint8_t key[16],challenge[16];
            for(unsigned i=0;i<16;++i){key[i]=pending_.key[15-i];challenge[i]=d[16-i];}
            r[0]=1;aes(key,challenge,r+1);len=17;stage_=3;
        } else if(sub==3) {
            if(stage_!=3 || size!=1 || d[0])return false;
            bond_=pending_;paired_=bond_dirty_=true;stage_=4;r[0]=1;len=1;
        } else return false;
    } else if(cmd==2 && sub==4) {
        if(size!=8 || !d[0] || d[0]>192)return false;
        const auto address=u32(d+4);if(address>0x1fffff || d[0]>0x200000-address)return false;
        put32(r,d[0]);put32(r+4,address);factory(address,r+8,d[0],bond_.local);len=8+d[0];
    } else if(cmd==3) {
        if(sub==0x0a){if(size<1 || (d[0]!=5 && d[0]!=9))return false;selected_report_=d[0];}
        else if(sub==0x0d)len=4;
        else if(sub==7 || sub==8 || sub==9)return false; // no unauthenticated key injection
    } else if(cmd==0x0c) {
        if(sub==2 || sub==4 || sub==5) {
            if(size<1)return false;
            if(sub==2){mask_=d[0]&0x2f;enabled_=mask_;}
            else if(sub==4)enabled_|=d[0]&mask_;
            else enabled_&=uint8_t(~d[0]);
        }
        len=4;
        if(sub==1){len=8;for(unsigned i=0;i<6;++i)r[i]=1;}
        if(sub==3){r[0]=mask_;}
        if(sub==6){len=40;r[4]=2;r[9]=0x50;}
    } else if(cmd==0x10 && sub==1) {
        const uint8_t fw[12]={2,1,4,2,12,0,0,0,0,2,3,0};std::memcpy(r,fw,12);len=12;
    } else if(cmd==7)len=1;
    else if(cmd==0x16)len=24;
    else if(cmd==0x11) {
        if(sub==1){r[0]=1;len=4;}
        else if(sub==3) {
            const uint8_t b[29]={1,0x20,3,0,0,0x0a,0xe8,0x1c,0x3b,0x79,0x7d,0x8b,0x3a,0x0a,0xe8,
                0x9c,0x42,0x58,0xa0,0x0b,0x42,0x0a,0xe8,0x9c,0x41,0x58,0xa0,0x0b,0x41};std::memcpy(r,b,29);len=29;
        } else return false;
    } else if(cmd==0x18 && sub==1){const uint8_t b[8]={0,0,0x40,0xf0,0,0,0x60,0};std::memcpy(r,b,8);len=8;}
    else if(cmd==1 && sub==0x0c){const uint8_t b[4]={0x61,0x12,0x50,0x0d};std::memcpy(r,b,4);len=4;}
    else if(cmd==0x0b && sub==1){put16(r,4000);len=2;}
    else if(cmd==0x0b && sub==2){r[0]=state_.charging?0x34:0;len=2;}
    else if(cmd!=9 && cmd!=0x0a)return false; // NFC, firmware writes, unknown operations unsupported
    length=8+len;return true;
}
void Switch2ProProtocol::input(uint8_t id,uint32_t time_us,uint8_t out[63]) {
    std::memset(out,0,63);const auto b=state_.buttons;
    const bool down=state_.dpad==3 || state_.dpad==4 || state_.dpad==5;
    const bool up=state_.dpad==7 || state_.dpad==0 || state_.dpad==1;
    const bool left=state_.dpad==5 || state_.dpad==6 || state_.dpad==7;
    const bool right=state_.dpad==1 || state_.dpad==2 || state_.dpad==3;
    uint8_t x=0,y=0,z=0;
    if(b&CONTROLLER_BUTTON_CROSS)x|=1;if(b&CONTROLLER_BUTTON_CIRCLE)x|=2;
    if(b&CONTROLLER_BUTTON_SQUARE)x|=4;if(b&CONTROLLER_BUTTON_TRIANGLE)x|=8;
    if(b&CONTROLLER_BUTTON_R1)x|=16;if((b&CONTROLLER_BUTTON_R2)||state_.r2>32)x|=32;
    if(b&CONTROLLER_BUTTON_OPTIONS)x|=64;if(b&CONTROLLER_BUTTON_R3)x|=128;
    y=uint8_t(unsigned(down)|(unsigned(right)<<1)|(unsigned(left)<<2)|(unsigned(up)<<3));
    if(b&CONTROLLER_BUTTON_L1)y|=16;if((b&CONTROLLER_BUTTON_L2)||state_.l2>32)y|=32;
    if(b&CONTROLLER_BUTTON_CREATE)y|=64;if(b&CONTROLLER_BUTTON_L3)y|=128;
    if(b&CONTROLLER_BUTTON_HOME)z|=1;if(b&CONTROLLER_BUTTON_PAD)z|=2;
    if(b&CONTROLLER_BUTTON_MUTE)z|=16; // Mute maps to C; no invented GL/GR inputs.
    if(id==5) {
        put32(out,counter_++);
        out[4]=uint8_t(((x>>2)&3)|((x&2)<<2)|((x&1)<<2)|((x&16)<<2)|((x&32)<<2));
        out[5]=uint8_t(((y&64)>>6)|((x&64)>>5)|((x&128)>>5)|((y&128)>>4)|((z&1)<<4)|((z&2)<<4)|((z&16)<<2));
        out[6]=uint8_t(unsigned(down)|(unsigned(up)<<1)|(unsigned(right)<<2)|(unsigned(left)<<3)|((y&16)<<2)|((y&32)<<2));
        stick(out+10,state_.lx,state_.ly);stick(out+13,state_.rx,state_.ry);
        put16(out+31,4000);out[33]=state_.charging?0x34:0;out[41]=1;
        if(enabled_&4) {
            put32(out+42,time_us);
            const auto &a=state_.accel;const auto &g=state_.gyro;
            put16(out+48,-int32_t(a[1])/2);put16(out+50,int32_t(a[0])/2);put16(out+52,int32_t(a[2])/2);
            put16(out+54,-int32_t(g[2]));put16(out+56,-int32_t(g[0]));put16(out+58,int32_t(g[1]));
        }
    } else {
        out[0]=uint8_t(counter_++);out[1]=uint8_t((std::min<uint8_t>(state_.battery,9)<<2)|(state_.charging?3:0));
        out[2]=x;out[3]=y;out[4]=z;stick(out+5,state_.lx,state_.ly);stick(out+8,state_.rx,state_.ry);
        out[11]=(enabled_&0x20)?0x38:0x30;
        // Packed report-09 motion is undocumented: do not fabricate captured IMU data.
    }
}
bool switch2_decode_rumble(const uint8_t *p,size_t n,Switch2RumblePacket &out) {
    if(n<33 || (p[0]!=0 && p[0]!=2))return false;
    Switch2RumblePacket decoded{};
    for(unsigned side=0;side<2;++side) {
        const auto *block=p+1+side*16;
        const uint8_t type=block[0]>>4;
        if(type==0) {for(unsigned i=0;i<16;++i)if(block[i])return false;continue;}
        if(type!=5)return false;
        for(unsigned f=0;f<3;++f) {
            uint64_t bits=0;for(unsigned i=0;i<5;++i)bits|=uint64_t(block[1+f*5+i])<<(8*i);
            auto &frame=side?decoded.right[f]:decoded.left[f];
            frame.frequency[0]=uint16_t(bits&1023);frame.amplitude[0]=uint16_t((bits>>10)&1023);
            frame.frequency[1]=uint16_t((bits>>20)&1023);frame.amplitude[1]=uint16_t((bits>>30)&1023);
        }
    }
    out=decoded;return true;
}
void switch2_approximate_rumble(const Switch2RumblePacket &in,SwitchRumbleState out[3]) {
    for(unsigned f=0;f<3;++f)for(unsigned side=0;side<2;++side) {
        const auto &frame=side?in.right[f]:in.left[f];auto &actuator=side?out[f].right:out[f].left;
        // The physical frequency transfer function is not documented by Linux.
        // Preserve the code as a nominal Hz approximation, never claim calibration.
        actuator.high={std::clamp<uint16_t>(frame.frequency[0]&511,20,1250),uint8_t((frame.amplitude[0]*255u+511)/1023)};
        actuator.low={std::clamp<uint16_t>(frame.frequency[1]&511,20,1250),uint8_t((frame.amplitude[1]*255u+511)/1023)};
    }
}
