// SPDX-License-Identifier: MIT
// Native Pro Controller 2 BLE peripheral. The DualSense remains a Classic HID
// peer in bt.cpp. Nintendo's custom pairing uses raw LL LTK replies, not SMP.
#if defined(SWITCH2_PRO_NATIVE) && !defined(SWITCH2_ESP32_LINK)
#include "switch_bt.h"
#include "switch2_pro_protocol.h"
#include "bt.h"
#include "btstack.h"
#include "btstack_tlv.h"
#include "ble/att_db_util.h"
#include "ble/att_server.h"
#include "btstack_crypto.h"
#include "hci_dump.h"
#include "effects_runtime.h"
#include "switch_rumble_adapter.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
extern "C" {
#include "pico/btstack_chipset_cyw43.h"
}
#include "pico/unique_id.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr uint32_t BOND_TAG=0x50324231; // P2B1, separate from legacy Classic host
constexpr uint32_t ID_TAG=0x53574945;
struct BondRecord { uint32_t magic;Switch2ProBond bond;uint32_t checksum; };
static_assert(sizeof(Switch2ProBond)==40);
Switch2ProProtocol protocol;
bd_addr_t local{},peer{};
hci_con_handle_t connection=HCI_CON_HANDLE_INVALID;
bool working=false,encrypted=false,pairing=true,subscribed=false,send_requested=false;
bool advertising=false,advertise_pending=false,ltk_pending=false,bond_pending=false,known=false,crypto_ok=false;
uint16_t ccc[7]{};
constexpr uint16_t ccc_handles[7]={0x0b,0x0f,0x1b,0x1f,0x23,0x27,0x2f};
constexpr uint16_t vendor_handles[8]={5,0x0c,0x10,0x1c,0x20,0x24,0x28,0x30};
uint8_t vendor_values[8][32]{},vendor_lengths[8]{};
uint8_t adv[31]{},last_error=0,last_cmd=0,last_sub=0;
uint32_t reports=0,commands=0,rejected=0,rumble_packets=0,unknown_rumble=0,wake_attempts=0;
uint32_t next_input=0,interval_us=15000,save_after=0,wake_until=0,connected_at=0;
uint8_t events[16][8]{};unsigned event_head=0,event_count=0;
uint8_t att_events[16][8]{};unsigned att_head=0,att_count=0;
uint32_t att_rx=0,att_tx=0;
struct Reply { uint8_t bytes[240];uint16_t size,handle; };
Reply replies[4]{};unsigned reply_head=0,reply_count=0;
uint32_t now_ms(){return to_ms_since_boot(get_absolute_time());}
void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
uint32_t checksum(const Switch2ProBond &b){uint32_t h=2166136261u;const auto *p=reinterpret_cast<const uint8_t *>(&b);for(unsigned i=0;i<sizeof(b);++i){h^=p[i];h*=16777619u;}return h;}
void trace(uint8_t event,uint16_t handle,uint8_t status){
 auto *p=events[event_head];event_head=(event_head+1)%16;event_count=std::min(event_count+1,16u);
 p[0]=event;p[1]=uint8_t(handle);p[2]=uint8_t(handle>>8);p[3]=status;put32(p+4,now_ms());
}
bool same(const uint8_t *a,const uint8_t *b){return std::memcmp(a,b,6)==0;}
void log_packet(uint8_t type,uint8_t incoming,uint8_t *p,uint16_t size){
 if(type!=HCI_ACL_DATA_PACKET || size<9 || connection==HCI_CON_HANDLE_INVALID)return;
 if(((uint16_t(p[0])|uint16_t(p[1]<<8))&0xfff)!=connection || p[6]<4 || p[6]>6 || p[7]!=0)return;
 if(p[6]==4){if(incoming)++att_rx;else ++att_tx;}
 auto *e=att_events[att_head];att_head=(att_head+1)%16;att_count=std::min(att_count+1,16u);
 e[0]=incoming;e[1]=p[8];e[2]=size>=10?p[9]:0;e[3]=size>=11?p[10]:0;
 e[4]=uint8_t(size-8);e[5]=uint8_t((size-8)>>8);e[6]=p[6];e[7]=p[7];
}
void log_message(int,const char *,va_list){}
const hci_dump_t dump={nullptr,log_packet,log_message};
void aes(const uint8_t *key,const uint8_t *block,uint8_t *out){btstack_aes128_calc(key,block,out);}
bool crypto_test(){
 const uint8_t key[16]={0x5b,0xdc,0x0f,0x82,0x43,0x6f,0x83,0x04,0xc5,0x74,0x58,0xae,0x50,0x07,0xf5,0x69};
 const uint8_t block[16]={0x44,0x05,0x32,0x1f,0xe9,0x15,0x8c,0xbb,0x15,0xdf,0xfe,0xd8,0x8a,0xdf,0xc6,0x6f};
 const uint8_t expected[16]={0x13,0x4c,0x97,0xf5,0x11,0xb9,0xb6,0xdd,0x4d,0x86,0xfd,0x40,0xf5,0x36,0xe9,0xed};
 uint8_t out[16];aes(key,block,out);return std::memcmp(out,expected,16)==0;
}
void session_reset(){encrypted=subscribed=send_requested=ltk_pending=false;std::memset(ccc,0,sizeof(ccc));reply_count=reply_head=0;}
void set_advertising(){
 if(!working)return;
 gap_advertisements_enable(0);advertising=false;
 if(!switch_bt_enabled() || connection!=HCI_CON_HANDLE_INVALID || !crypto_ok)return;
 const uint8_t base[31]={2,1,6,27,255,0x53,5,1,0,3,0x7e,5,0x69,0x20,0,1,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0};
 std::memcpy(adv,base,31);
 if(known && !pairing){
  const auto &b=protocol.bond();for(unsigned i=0;i<6;++i)adv[17+i]=b.hosts[0][5-i];
  if(wake_until && int32_t(wake_until-now_ms())>0)adv[16]=0x81;
 }
 bd_addr_t unused{};gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_OFF);
 gap_advertisements_set_params(0x30,0x30,0,0,unused,7,0);
 gap_advertisements_set_data(31,adv);
 static uint8_t name[]={15,9,'P','r','o',' ','C','o','n','t','r','o','l','l','e','r'};
 gap_scan_response_set_data(sizeof(name),name);
 gap_advertisements_enable(1);advertising=true;
}
void record_bond(){
 void *ctx=nullptr;const btstack_tlv_t *tlv=nullptr;btstack_tlv_get_instance(&tlv,&ctx);
 BondRecord rec{BOND_TAG,protocol.bond(),checksum(protocol.bond())};
 if(tlv && tlv->store_tag(ctx,BOND_TAG,reinterpret_cast<const uint8_t *>(&rec),sizeof(rec))==0){bond_pending=false;known=true;pairing=false;}
 else {last_error=0xf1;save_after=now_ms()+2000;}
}
void queue_reply(const uint8_t *data,size_t size,uint16_t handle,bool combined){
 if(reply_count==4 || size+(combined?14:0)>sizeof(Reply::bytes)){
  ++rejected;last_error=0xf2;gap_disconnect(connection);return;
 }
 auto &r=replies[(reply_head+reply_count)%4];r.handle=handle;r.size=uint16_t(size+(combined?14:0));
 std::memset(r.bytes,0,combined?14:0);std::memcpy(r.bytes+(combined?14:0),data,size);++reply_count;
}
void rumble(const uint8_t *data,size_t size){
 if(!encrypted || !protocol.paired())return;
 Switch2RumblePacket raw{};
 if(!switch2_decode_rumble(data,size,raw)){++unknown_rumble;switch_rumble_adapter_stop();return;}
 SwitchRumbleState states[3]{};switch2_approximate_rumble(raw,states);
 switch_rumble_adapter_apply_sequence(states);++rumble_packets;
}
uint16_t read(hci_con_handle_t handle,uint16_t attribute,uint16_t offset,uint8_t *buffer,uint16_t size){
 if(handle!=connection)return 0;
 for(unsigned i=0;i<7;++i)if(attribute==ccc_handles[i])return att_read_callback_handle_little_endian_16(ccc[i],offset,buffer,size);
 for(unsigned i=0;i<8;++i)if(attribute==vendor_handles[i])return att_read_callback_handle_blob(vendor_values[i],vendor_lengths[i],offset,buffer,size);
 uint8_t data[63]{};size_t length=0;
 if(attribute==0x0a || attribute==0x0e){
  if(encrypted && protocol.paired())protocol.input(attribute==0x0a?5:9,uint32_t(time_us_64()),data);
  length=63;
 } else if(attribute==3 || attribute==7)length=1;
 else if(attribute==0x35){const char name[]="Pro Controller";return att_read_callback_handle_blob(reinterpret_cast<const uint8_t *>(name),sizeof(name)-1,offset,buffer,size);}
 else if(attribute==0x37){data[0]=0xc4;data[1]=3;length=2;}
 return att_read_callback_handle_blob(data,uint16_t(length),offset,buffer,size);
}
int write(hci_con_handle_t handle,uint16_t attribute,uint16_t transaction,uint16_t offset,uint8_t *data,uint16_t size){
 if(handle!=connection || !switch_bt_enabled())return ATT_ERROR_WRITE_NOT_PERMITTED;
 if(transaction!=ATT_TRANSACTION_MODE_NONE || offset)return ATT_ERROR_REQUEST_NOT_SUPPORTED;
 for(unsigned i=0;i<8;++i)if(attribute==vendor_handles[i]){
  if(size>32)return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
  std::memcpy(vendor_values[i],data,size);vendor_lengths[i]=uint8_t(size);return 0;
 }
 for(unsigned i=0;i<7;++i)if(attribute==ccc_handles[i]){
  if(size!=2 || data[1] || data[0]>1)return ATT_ERROR_VALUE_NOT_ALLOWED;
  ccc[i]=data[0];subscribed=ccc[0] || ccc[1];return 0;
 }
 if(attribute==0x12){rumble(data,size);return 0;}
 if(attribute==0x14 || attribute==0x16){
  const bool combined=attribute==0x16;
  if(combined){if(size<33)return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;rumble(data,33);data+=33;size-=33;if(!size)return 0;}
  uint8_t response[224]{};size_t length=0;
  if(size>=4){last_cmd=data[0];last_sub=data[3];}
  if(!protocol.command(data,size,response,length,aes)){++rejected;return ATT_ERROR_REQUEST_NOT_SUPPORTED;}
  ++commands;
  Switch2ProBond bond{};
  if(protocol.take_bond(bond)){
   std::memcpy(bond.peer,peer,6);protocol.restore(bond);bond_pending=true;save_after=now_ms()+500;
  }
  queue_reply(response,length,combined?0x1e:0x1a,combined);return 0;
 }
 // Firmware flashing and audio paths are deliberately unavailable.
 return ATT_ERROR_WRITE_NOT_PERMITTED;
}
void uuid(const char *text,uint8_t out[16]){
 unsigned index=0;uint8_t value=0;bool high=true;
 for(const char *p=text;*p;++p){if(*p=='-')continue;const uint8_t digit=uint8_t(*p<='9'?*p-'0':*p-'a'+10);if(high)value=digit<<4;else out[index++]=value|digit;high=!high;}
}
void service(const char *id){uint8_t b[16];uuid(id,b);att_db_util_add_service_uuid128(b);}
uint16_t characteristic(const char *id,uint16_t flags){uint8_t b[16],dummy=0;uuid(id,b);return att_db_util_add_characteristic_uuid128(b,flags|ATT_PROPERTY_DYNAMIC,ATT_SECURITY_NONE,ATT_SECURITY_NONE,&dummy,0);}
void descriptor(const char *id){uint8_t b[16],dummy=0;uuid(id,b);att_db_util_add_descriptor_uuid128(b,ATT_PROPERTY_READ|ATT_PROPERTY_WRITE|ATT_PROPERTY_DYNAMIC,ATT_SECURITY_NONE,ATT_SECURITY_NONE,&dummy,0);}
bool build_gatt(){
 constexpr uint16_t rn=ATT_PROPERTY_READ|ATT_PROPERTY_NOTIFY;
 constexpr uint16_t wn=ATT_PROPERTY_WRITE_WITHOUT_RESPONSE;
 const char *rate="679d5510-5a24-4dee-9557-95df80486ecb",*response="b746df8c-f358-495b-9cd2-e3bbeda4f979";
 att_db_util_init();service("00c5af5d-1964-4e30-8f51-1956f96bd280");
 characteristic("00c5af5d-1964-4e30-8f51-1956f96bd281",ATT_PROPERTY_READ);
 characteristic("00c5af5d-1964-4e30-8f51-1956f96bd282",ATT_PROPERTY_WRITE);
 characteristic("00c5af5d-1964-4e30-8f51-1956f96bd283",ATT_PROPERTY_READ);
 service("ab7de9be-89fe-49ad-828f-118f09df7fd0");
 bool ok=characteristic("ab7de9be-89fe-49ad-828f-118f09df7fd2",rn)==0x0a;descriptor(rate);
 ok &= characteristic("7492866c-ec3e-4619-8258-32755ffcc0f8",rn)==0x0e;descriptor(rate);
 ok &= characteristic("cc483f51-9258-427d-a939-630c31f72b05",wn)==0x12;
 ok &= characteristic("649d4ac9-8eb7-4e6c-af44-1ea54fe5f005",wn)==0x14;
 ok &= characteristic("3dacbc7e-6955-40b5-8eaf-6f9809e8b379",wn)==0x16;
 characteristic("4147423d-fdae-4df7-a4f7-d23e5df59f8d",wn);
 ok &= characteristic("c765a961-d9d8-4d36-a20a-5315b111836a",ATT_PROPERTY_NOTIFY)==0x1a;descriptor(response);
 ok &= characteristic("506d9f7d-4278-4e95-a549-326ba77657e0",ATT_PROPERTY_NOTIFY)==0x1e;descriptor(response);
 characteristic("d3bd69d2-841c-4241-ab15-f86f406d2a80",ATT_PROPERTY_NOTIFY);descriptor(response);
 characteristic("ab7de9be-89fe-49ad-828f-118f09df7fde",rn);descriptor(rate);
 characteristic("ab7de9be-89fe-49ad-828f-118f09df7fdf",wn);
 characteristic("cc483f51-9258-427d-a939-630c31f72b06",wn);
 characteristic("7492866c-ec3e-4619-8258-32755ffcc0f9",rn);descriptor(rate);
 ok &= characteristic("3dacbc7e-6955-40b5-8eaf-6f9809e8b380",wn)==0x32;
 att_db_util_add_service_uuid16(0x1800);uint8_t dummy=0;
 ok &= att_db_util_add_characteristic_uuid16(0x2a00,ATT_PROPERTY_READ|ATT_PROPERTY_DYNAMIC,0,0,&dummy,0)==0x35;
 att_db_util_add_characteristic_uuid16(0x2a01,ATT_PROPERTY_READ|ATT_PROPERTY_DYNAMIC,0,0,&dummy,0);
 att_db_util_add_service_uuid16(0x1801);
 if(ok)att_server_init(att_db_util_get_address(),read,write);
 return ok;
}
void send_ready(uint8_t type,uint16_t,uint8_t *packet,uint16_t size){
 if(type!=HCI_EVENT_PACKET || size<2 || packet[0]!=ATT_EVENT_CAN_SEND_NOW)return;
 send_requested=false;
 if(connection==HCI_CON_HANDLE_INVALID || !switch_bt_enabled())return;
 const auto mtu=att_server_get_mtu(connection);
 if(reply_count){
  auto &r=replies[reply_head];
  const auto slot=r.handle==0x1a?2:3;
  if(!ccc[slot]){last_error=0xf3;return;}
  if(r.size+3>mtu){last_error=0xf4;return;}
  last_error=att_server_notify(connection,r.handle,r.bytes,r.size);
  if(!last_error){reply_head=(reply_head+1)%4;--reply_count;}
 } else if(encrypted && protocol.paired() && subscribed && mtu>=66){
  uint8_t report[63];const uint16_t attribute=ccc[1]?0x0e:0x0a;
  protocol.input(attribute==0x0e?9:5,uint32_t(time_us_64()),report);
  last_error=att_server_notify(connection,attribute,report,63);
  if(!last_error){++reports;next_input=uint32_t(time_us_64())+interval_us;}
 }
}
}
void switch_bt_init(btstack_packet_handler_t){
 hci_dump_init(&dump);
 pico_unique_board_id_t id{};pico_get_unique_board_id(&id);
 const uint8_t identity[6]={0x7c,0xbb,0x8a,id.id[5],id.id[6],uint8_t(id.id[7]^0x41)};
 std::memcpy(local,identity,6);protocol.reset(local);
 hci_set_chipset(btstack_chipset_cyw43_instance());hci_set_bd_addr(local);
 gap_set_class_of_device(0x2508);gap_set_local_name("Pro Controller");
 crypto_ok=crypto_test() && build_gatt();if(!crypto_ok)last_error=0xe0;
 // No sm_init(): the Nintendo protocol is not SMP. We answer LL LTK requests
 // below; starting the standard SM would also send a competing negative reply.
 att_server_register_packet_handler(send_ready);
}
bool switch_bt_connected(){return switch_bt_enabled() && connection!=HCI_CON_HANDLE_INVALID && encrypted && protocol.paired() && subscribed;}
bool switch_bt_is_peer(const uint8_t address[6]){return known && same(address,protocol.bond().peer);}
void switch_bt_pair(){
 if(!switch_bt_enabled())return;
 pairing=true;wake_until=0;
 if(connection!=HCI_CON_HANDLE_INVALID)gap_disconnect(connection);
 else {protocol.reset(local);known=false;advertise_pending=true;}
}
void switch_bt_neutral(){protocol.update(ControllerState{});}
void switch_bt_mode_changed(){
 if(!working)return;
 switch_bt_neutral();
 if(!switch_bt_enabled()){
  wake_until=0;gap_advertisements_enable(0);advertising=false;
  if(connection!=HCI_CON_HANDLE_INVALID)gap_disconnect(connection);
 } else advertise_pending=true;
}
bool switch_bt_wake_request(){
 if(!switch_bt_enabled() || !known || connection!=HCI_CON_HANDLE_INVALID)return false;
 wake_until=now_ms()+2000;++wake_attempts;advertise_pending=true;return true;
}
void switch_bt_update(const ControllerState &s){
 static bool home=false;const bool pressed=(s.buttons&CONTROLLER_BUTTON_HOME)!=0;
 if(pressed && !home && !switch_bt_connected())switch_bt_wake_request();home=pressed;
 protocol.update(s);
}
bool switch_bt_hci(uint8_t *p,uint16_t size){
 if(size<2)return false;
 const auto event=p[0];
 if(event==HCI_EVENT_COMMAND_COMPLETE && size>=6){
  const auto opcode=hci_event_command_complete_get_command_opcode(p);
  if(opcode==HCI_OPCODE_HCI_READ_LOCAL_VERSION_INFORMATION)hci_set_bd_addr(local);
  if((opcode>=0x2006 && opcode<=0x200a) && p[5]){last_error=p[5];trace(event,opcode,last_error);}
 }
 if(event==BTSTACK_EVENT_STATE && size>=3 && btstack_event_state_get_state(p)==HCI_STATE_WORKING){
  working=true;void *ctx=nullptr;const btstack_tlv_t *tlv=nullptr;btstack_tlv_get_instance(&tlv,&ctx);
  uint8_t old[6]{};
  if(tlv){
   if(tlv->get_tag(ctx,ID_TAG,old,6)!=6 || !same(old,local)){
    gap_delete_all_link_keys();tlv->store_tag(ctx,ID_TAG,local,6);
   }
   BondRecord stored{};
   if(tlv->get_tag(ctx,BOND_TAG,reinterpret_cast<uint8_t *>(&stored),sizeof(stored))==sizeof(stored) &&
      stored.magic==BOND_TAG && stored.checksum==checksum(stored.bond) && same(stored.bond.local,local)){
    protocol.restore(stored.bond);known=true;pairing=false;
   }
  }
  advertise_pending=true;return false;
 }
 if(event==HCI_EVENT_META_GAP && size>=3 && hci_event_gap_meta_get_subevent_code(p)==GAP_SUBEVENT_LE_CONNECTION_COMPLETE){
  const auto status=gap_subevent_le_connection_complete_get_status(p);if(status){last_error=status;return false;}
  const auto handle=gap_subevent_le_connection_complete_get_connection_handle(p);
  bd_addr_t address;gap_subevent_le_connection_complete_get_peer_address(p,address);
  if(!switch_bt_enabled() || connection!=HCI_CON_HANDLE_INVALID || (!pairing && known && !same(address,protocol.bond().peer))){gap_disconnect(handle);return false;}
  connection=handle;std::memcpy(peer,address,6);advertising=false;session_reset();
  auto old=protocol.bond();const bool restore=known && !pairing;protocol.reset(local);if(restore)protocol.restore(old);
  interval_us=uint32_t(gap_subevent_le_connection_complete_get_conn_interval(p))*1250;connected_at=now_ms();next_input=0;
  trace(0x70,handle,0);return false;
 }
 if(event==HCI_EVENT_LE_META && size>=3){
  const auto sub=hci_event_le_meta_get_subevent_code(p);
  if(sub==HCI_SUBEVENT_LE_LONG_TERM_KEY_REQUEST && size>=5 && hci_subevent_le_long_term_key_request_get_connection_handle(p)==connection){trace(0x73,connection,protocol.stage());ltk_pending=true;return true;}
  if(sub==HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE && hci_subevent_le_connection_update_complete_get_connection_handle(p)==connection){
   interval_us=uint32_t(hci_subevent_le_connection_update_complete_get_conn_interval(p))*1250;trace(0x72,connection,hci_subevent_le_connection_update_complete_get_status(p));
  }
 }
 if(event==HCI_EVENT_ENCRYPTION_CHANGE && size>=6 && hci_event_encryption_change_get_connection_handle(p)==connection){
  last_error=hci_event_encryption_change_get_status(p);encrypted=!last_error && hci_event_encryption_change_get_encryption_enabled(p);trace(event,connection,last_error);
  if(encrypted && protocol.paired()){known=true;pairing=false;}
  if(!encrypted)gap_disconnect(connection);return false;
 }
 if(event==HCI_EVENT_DISCONNECTION_COMPLETE && size>=6 && hci_event_disconnection_complete_get_connection_handle(p)==connection){
  trace(event,connection,hci_event_disconnection_complete_get_reason(p));connection=HCI_CON_HANDLE_INVALID;session_reset();effects_stop();
  if(pairing){protocol.reset(local);known=false;}
  advertise_pending=true;return true;
 }
 return false;
}
bool switch_bt_l2cap(uint8_t,uint16_t,uint8_t *,uint16_t){return false;}
void switch_bt_task(){
 if(!working)return;
 if(ltk_pending && hci_can_send_command_packet_now()){
  ltk_pending=false;
  if(connection!=HCI_CON_HANDLE_INVALID && protocol.paired() && (known || protocol.stage()==4))
   hci_send_cmd(&hci_le_long_term_key_request_reply,connection,protocol.bond().key);
  else if(connection!=HCI_CON_HANDLE_INVALID)hci_send_cmd(&hci_le_long_term_key_negative_reply,connection);
 }
 if(bond_pending && known && int32_t(now_ms()-save_after)>=0)record_bond();
 if(advertise_pending){advertise_pending=false;set_advertising();}
 if(wake_until && int32_t(now_ms()-wake_until)>=0){wake_until=0;advertise_pending=true;}
 if(!switch_bt_enabled() || connection==HCI_CON_HANDLE_INVALID)return;
 if(!encrypted && now_ms()-connected_at>30000){gap_disconnect(connection);return;}
 switch_rumble_adapter_task();
 const bool due=encrypted && subscribed && int32_t(uint32_t(time_us_64())-next_input)>=0;
 if(!send_requested && (reply_count || due)){
  send_requested=true;att_server_request_can_send_now_event(connection);
 }
}
void switch_bt_diagnostics(uint8_t out[20]){
 std::memset(out,0,20);out[0]='W';out[1]='L';out[2]=1;
 out[3]=uint8_t((working?1:0)|(known?2:0)|(switch_bt_connected()?4:0)|(switch_bt_enabled()?8:0)|16);
 out[4]=last_error;out[5]=uint8_t(connection);out[6]=uint8_t(connection>>8);out[7]=encrypted;out[8]=subscribed;
 put32(out+9,reports);put32(out+13,commands);out[17]=last_cmd;out[18]=last_sub;
}
void switch_bt_pro2_diagnostics(uint8_t out[55]){
 std::memset(out,0,55);out[0]='P';out[1]='2';out[2]=1;
 out[3]=uint8_t((working?1:0)|(known?2:0)|(encrypted?4:0)|(subscribed?8:0)|(pairing?16:0)|(crypto_ok?32:0)|(advertising?64:0)|(bond_pending?128:0));
 out[4]=last_error;out[5]=protocol.stage();out[6]=protocol.selected_report();out[7]=protocol.features();
 put32(out+8,interval_us);put32(out+12,reports);put32(out+16,commands);put32(out+20,rumble_packets);
 put32(out+24,unknown_rumble);put32(out+28,rejected);bd_addr_t actual{};gap_local_bd_addr(actual);std::memcpy(out+32,actual,6);std::memcpy(out+38,peer,6);
 out[44]=last_cmd;out[45]=last_sub;out[46]=uint8_t(att_server_get_mtu(connection));out[47]=uint8_t(att_server_get_mtu(connection)>>8);
 put32(out+48,wake_attempts);out[52]=uint8_t(reply_count);
 out[53]=uint8_t(std::min<uint32_t>(att_rx,255));out[54]=uint8_t(std::min<uint32_t>(att_tx,255));
}
void switch_bt_pro2_att_trace(uint8_t out[55]){
 std::memset(out,0,55);out[0]='A';out[1]='T';out[2]=1;out[3]=uint8_t(att_count);
 for(unsigned i=0;i<6 && i<att_count;++i)std::memcpy(out+4+i*8,att_events[(att_head+15-i)%16],8);
}
void switch_bt_pro2_wake_status(uint8_t out[32]){
 std::memset(out,0,32);out[0]='S';out[1]='2';out[2]=1;out[3]=uint8_t(64|(known?1:0)|(wake_until?2:0));
 put32(out+6,wake_attempts);std::memcpy(out+10,local,6);if(known)std::memcpy(out+16,protocol.bond().hosts[0],6);out[22]=0x69;out[23]=0x20;out[5]=last_error;
}
void switch_bt_motion_diagnostics(uint8_t out[55]){switch_bt_pro2_diagnostics(out);}
void switch_bt_gap_diagnostics(uint8_t out[24]){std::memset(out,0,24);}
void switch_bt_event_page(uint8_t out[59],unsigned skip){
 std::memset(out,0,59);out[0]='E';out[1]=uint8_t(event_count);
 for(unsigned i=0;i<7 && skip+i<event_count;++i)std::memcpy(out+2+i*8,events[(event_head+15-skip-i)%16],8);
}
void switch_bt_trace(uint8_t out[26]){
 std::memset(out,0,26);out[0]='T';out[1]=uint8_t(std::min(event_count,3u));
 for(unsigned i=0;i<out[1];++i)std::memcpy(out+2+i*6,events[(event_head+16-out[1]+i)%16],6);
 std::memcpy(out+20,local,6);
}
#endif
