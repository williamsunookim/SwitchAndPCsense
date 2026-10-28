// SPDX-License-Identifier: MIT
// Separate host/peer state for Switch output; DualSense host lives in bt.cpp.
#include "switch_bt.h"
#include "bt.h"
#include "send_gate.h"
#include "btstack.h"
#include "btstack_tlv.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include "switch_bt_sdp.h"
#include "switch_bt_descriptor.h"
#include "switch_pro_protocol.h"
#include "switch_rumble_adapter.h"
#include "effects_runtime.h"
#include "config.h"
#include "switch2_wake.h"
#include <cstring>
namespace {
constexpr uint32_t HOST_TAG=0x53574853;
bd_addr_t peer{},local{};
bool known=false,claimed=false,outgoing=false,working=false,save_pending=false,send_requested=false,host_suspended=false;
uint8_t control_reply[2]{};uint8_t control_reply_size=0;
hci_con_handle_t handle=HCI_CON_HANDLE_INVALID;
uint16_t control=0,interrupt=0;
btstack_packet_handler_t dispatcher=nullptr;
SwitchProProtocol protocol;
uint8_t hid_sdp[800]{},pnp_sdp[256]{},pending[65]{};
uint16_t pending_size=0;uint8_t last_rx_type=0,last_rx_id=0;
// 16-entry HCI/L2CAP event history: event, link(2), status, time_ms(4).
constexpr unsigned EVENT_SLOTS=16;
uint8_t events[EVENT_SLOTS][8]{};unsigned event_head=0,event_count=0;
void trace(uint8_t event,uint16_t link,uint8_t status) {
 uint8_t *e=events[event_head];event_head=(event_head+1)%EVENT_SLOTS;if(event_count<EVENT_SLOTS)++event_count;
 const uint32_t t=to_ms_since_boot(get_absolute_time());e[0]=event;e[1]=uint8_t(link);e[2]=uint8_t(link>>8);e[3]=status;
 for(unsigned i=0;i<4;++i)e[4+i]=uint8_t(t>>(8*i));
}
uint32_t retry_at=0,save_after=0,reports=0,commands=0,acl_since=0;uint8_t last_error=0;
uint32_t now() {return to_ms_since_boot(get_absolute_time());}
bool same(const uint8_t *address) {return claimed && std::memcmp(peer,address,6)==0;}
void scans() {gap_connectable_control(1);gap_discoverable_control(switch_bt_enabled() && !interrupt?1:0);}
void opened() {
 protocol.reset();protocol.set_mac_address(local);pending_size=0;send_requested=false;host_suspended=false;control_reply_size=0;
 save_pending=true;save_after=now()+1500;scans();
}
void lost() {
 control=interrupt=0;handle=HCI_CON_HANDLE_INVALID;outgoing=false;
 pending_size=0;send_requested=false;protocol.reset();host_suspended=false;control_reply_size=0;
 effects_stop();retry_at=now()+10000;scans();
}
void consume(const uint8_t *p,uint16_t n) {
 if(!switch_bt_enabled())return;
 if(n>=2) {last_rx_type=p[0];last_rx_id=p[1];}
 if(n<2 || (p[0]!=0xa2 && p[0]!=0x52)) return;
 protocol.handle_output_report(0,p+1,n-1);++commands;
 SwitchRumbleState rumble{};if(protocol.take_rumble(rumble))switch_rumble_adapter_apply(rumble);
}
}
void switch_bt_init(btstack_packet_handler_t dispatch) {
 dispatcher=dispatch;
 pico_unique_board_id_t id{};pico_get_unique_board_id(&id);
 const uint8_t identity[6]={0x7c,0xbb,0x8a,id.id[5],id.id[6],static_cast<uint8_t>(id.id[7]^0x40)};
 std::memcpy(local,identity,6);
 switch2_wake_init();
 const auto &wake = config_get_switch2_wake();
 if(switch2_wake_valid(wake)) std::memcpy(local,wake.source,6);
 hci_set_chipset(btstack_chipset_cyw43_instance());hci_set_bd_addr(local);
 protocol.set_bluetooth_transport(true);protocol.set_mac_address(local);
 gap_set_class_of_device(0x2508);gap_set_local_name("Pro Controller");
 gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);gap_set_bondable_mode(1);
 gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH|LM_LINK_POLICY_ENABLE_SNIFF_MODE);
 gap_set_allow_role_switch(true);
 hid_sdp_record_t params{};
 params.hid_device_subclass=8;params.hid_country_code=33;params.hid_virtual_cable=1;
 params.hid_remote_wake=1;params.hid_reconnect_initiate=1;params.hid_normally_connectable=1;
 params.hid_supervision_timeout=3200;params.hid_descriptor=switch_bt_report_descriptor;
 params.hid_descriptor_size=sizeof(switch_bt_report_descriptor);params.device_name="Wireless Gamepad";
 create_sdp_hid_record(hid_sdp,&params);sdp_register_service(hid_sdp);
 create_sdp_pnp_record(pnp_sdp,DEVICE_ID_VENDOR_ID_SOURCE_USB,0x057e,0x2009,1);sdp_register_service(pnp_sdp);
}
bool switch_bt_is_peer(const uint8_t address[6]) {return same(address);}
bool switch_bt_connected() {return switch_bt_enabled() && handle!=HCI_CON_HANDLE_INVALID && control && interrupt && !host_suspended;}
void switch_bt_mode_changed() {
 if(!working)return;
 switch_bt_neutral();
 if(!switch_bt_enabled()) {
  switch2_wake_cancel();
  if(handle!=HCI_CON_HANDLE_INVALID)gap_disconnect(handle);
 } else retry_at=now();
 scans();
}
void switch_bt_pair() {
 if(!switch_bt_enabled())return;
 // Keep both link-key databases intact. Incoming pairing can replace the saved host.
 known=false;save_pending=false;
 if(handle!=HCI_CON_HANDLE_INVALID) {trace(0x80,handle,0);gap_disconnect(handle);}
 else {known=false;claimed=false;retry_at=now()+30000;scans();}
}
bool switch_bt_hci(uint8_t *p,uint16_t size) {
 if(size<2)return false;
 switch2_wake_hci(p,size);
 bd_addr_t addr{};
 const auto event=hci_event_packet_get_type(p);
 if(event==HCI_EVENT_CONNECTION_COMPLETE)trace(event,hci_event_connection_complete_get_connection_handle(p),hci_event_connection_complete_get_status(p));
 else if(event==HCI_EVENT_AUTHENTICATION_COMPLETE)trace(event,hci_event_authentication_complete_get_connection_handle(p),hci_event_authentication_complete_get_status(p));
 else if(event==HCI_EVENT_ENCRYPTION_CHANGE)trace(event,hci_event_encryption_change_get_connection_handle(p),hci_event_encryption_change_get_status(p));
 else if(event==HCI_EVENT_DISCONNECTION_COMPLETE)trace(event,hci_event_disconnection_complete_get_connection_handle(p),hci_event_disconnection_complete_get_reason(p));
 else if(event==HCI_EVENT_SIMPLE_PAIRING_COMPLETE)trace(event,0xfffe,hci_event_simple_pairing_complete_get_status(p));
 else if(event==HCI_EVENT_IO_CAPABILITY_RESPONSE)trace(event,0xfffd,hci_event_io_capability_response_get_authentication_requirements(p));
 else if(event==HCI_EVENT_PIN_CODE_REQUEST)trace(event,0xfffc,0);
 else if(event==HCI_EVENT_LINK_KEY_REQUEST) {
  bd_addr_t key_address;link_key_t key;link_key_type_t key_type;
  hci_event_link_key_request_get_bd_addr(p,key_address);
  trace(event,0xfffb,gap_get_link_key_for_bd_addr(key_address,key,&key_type)?1:0);
 }
 else if(event==HCI_EVENT_COMMAND_STATUS && hci_event_command_status_get_status(p))trace(0xff,hci_event_command_status_get_command_opcode(p),hci_event_command_status_get_status(p));
 switch(event) {
 case HCI_EVENT_COMMAND_COMPLETE:
  if(hci_event_command_complete_get_command_opcode(p)==HCI_OPCODE_HCI_READ_LOCAL_VERSION_INFORMATION) {
   // The Pico SDK transport's open() overwrites the address configured before power-on.
   // Reapply after transport startup, before BTstack writes/reads the controller address.
   hci_set_bd_addr(local);
  }
  return false;
 case BTSTACK_EVENT_STATE:
  if(btstack_event_state_get_state(p)==HCI_STATE_WORKING) {
   working=true;void *ctx=nullptr;const btstack_tlv_t *tlv=nullptr;btstack_tlv_get_instance(&tlv,&ctx);
   switch2_wake_ready();
   uint8_t saved_identity[6]{};
   if(tlv && (tlv->get_tag(ctx,0x53574945,saved_identity,6)!=6 || std::memcmp(saved_identity,local,6)!=0)) {
    // Link keys belong to a local adapter identity. Do not reuse a previous trial's keys.
    gap_delete_all_link_keys();tlv->delete_tag(ctx,HOST_TAG);tlv->store_tag(ctx,0x53574945,local,6);
   }
   if(tlv && tlv->get_tag(ctx,HOST_TAG,addr,6)==6) {std::memcpy(peer,addr,6);known=claimed=true;}
   retry_at=now()+10000;scans();
  }return false;
 case HCI_EVENT_CONNECTION_REQUEST: {
  hci_event_connection_request_get_bd_addr(p,addr);
  const uint32_t cod=hci_event_connection_request_get_class_of_device(p);
  // Sony controllers are peripherals (major 0x05); Switch hosts are consoles.
  link_key_t key;link_key_type_t key_type;
  if(bt_matches_controller(addr) || (!same(addr) && ((cod&0x1f00)==0x0500 || (cod==0 && gap_get_link_key_for_bd_addr(addr,key,&key_type)))))return false;
  if(!switch_bt_enabled()) {
   hci_send_cmd(&hci_reject_connection_request,addr,ERROR_CODE_CONNECTION_REJECTED_DUE_TO_UNACCEPTABLE_BD_ADDR);return true;
  }
  if(handle!=HCI_CON_HANDLE_INVALID && !same(addr)) {
   hci_send_cmd(&hci_reject_connection_request,addr,ERROR_CODE_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES);return true;
  }
  std::memcpy(peer,addr,6);claimed=true;outgoing=false;
  return true; // BTstack has already scheduled the ACL accept.
 }
 case HCI_EVENT_CONNECTION_COMPLETE:
  hci_event_connection_complete_get_bd_addr(p,addr);if(bt_matches_controller(addr) || !same(addr))return false;
  last_error=hci_event_connection_complete_get_status(p);
  if(last_error) {lost();return true;}
  handle=hci_event_connection_complete_get_connection_handle(p);acl_since=now();
  if(!switch_bt_enabled())gap_disconnect(handle);
  return true; // L2CAP/BTstack negotiates security; do not issue a duplicate request.
 case HCI_EVENT_AUTHENTICATION_COMPLETE:
  if(hci_event_authentication_complete_get_connection_handle(p)!=handle)return false;
  last_error=hci_event_authentication_complete_get_status(p);
  if(last_error) {trace(0x81,handle,last_error);gap_drop_link_key_for_bd_addr(peer);gap_disconnect(handle);}
  return true;
 case HCI_EVENT_ENCRYPTION_CHANGE:
  if(hci_event_encryption_change_get_connection_handle(p)!=handle)return false;
  last_error=hci_event_encryption_change_get_status(p);
  if(switch_bt_enabled() && !last_error && hci_event_encryption_change_get_encryption_enabled(p) && outgoing && !control)
   last_error=l2cap_create_channel(dispatcher,peer,BLUETOOTH_PSM_HID_CONTROL,672,&control);
  return true;
 case HCI_EVENT_DISCONNECTION_COMPLETE:
  if(hci_event_disconnection_complete_get_connection_handle(p)!=handle)return false;
  if(hci_event_disconnection_complete_get_reason(p)==5 || hci_event_disconnection_complete_get_reason(p)==6)gap_drop_link_key_for_bd_addr(peer);
  lost();return true;
 default:return false; // BTstack and the existing key handler manage shared security events.
 }
}
bool switch_bt_l2cap(uint8_t type,uint16_t channel,uint8_t *p,uint16_t size) {
 if(type==L2CAP_DATA_PACKET) {
  if(channel!=control && channel!=interrupt)return false;
  if(channel==control && size) {
   const uint8_t transaction=p[0]&0xf0;
   if(transaction==0x10) {
    if((p[0]&15)==5) {trace(0x82,handle,p[0]);gap_disconnect(handle);}
    else if((p[0]&15)==3) {host_suspended=true;effects_stop();}
    else if((p[0]&15)==4)host_suspended=false;
    return true;
   }
   if(transaction!=0xa0) {
    control_reply[0]=0;control_reply_size=1;
    if(transaction==0x60) {control_reply[0]=0xa0;control_reply[1]=1;control_reply_size=2;}
    else if(transaction==0x50)consume(p,size);
    else if(transaction!=0x70 && transaction!=0x90)control_reply[0]=3;
    l2cap_request_can_send_now_event(control);return true;
   }
  }
  consume(p,size);return true;
 }
 if(type!=HCI_EVENT_PACKET || size<2)return false;
 bd_addr_t addr{};
 switch(hci_event_packet_get_type(p)) {
 case L2CAP_EVENT_INCOMING_CONNECTION: {
  l2cap_event_incoming_connection_get_address(p,addr);if(!same(addr))return false;
  const auto cid=l2cap_event_incoming_connection_get_local_cid(p);
  const auto psm=l2cap_event_incoming_connection_get_psm(p);
  if(!switch_bt_enabled()) {l2cap_decline_connection(cid);return true;}
  if(psm==BLUETOOTH_PSM_HID_CONTROL)control=cid;
  else if(psm==BLUETOOTH_PSM_HID_INTERRUPT)interrupt=cid;
  else return false;
  l2cap_accept_connection(cid);return true;
 }
 case L2CAP_EVENT_CHANNEL_OPENED: {
  l2cap_event_channel_opened_get_address(p,addr);if(!same(addr))return false;
  const auto cid=l2cap_event_channel_opened_get_local_cid(p);
  const uint8_t status=l2cap_event_channel_opened_get_status(p);
  // Our outgoing request can lose the race with the console's own incoming
  // channels, which then replace control/interrupt. That abandoned request
  // times out 10 s later (RTX 0x69); it must not tear down the working link.
  if(status && cid!=control && cid!=interrupt) {trace(0x86,cid,status);return true;}
  last_error=status;
  if(last_error) {trace(0x83,handle,last_error);if(handle!=HCI_CON_HANDLE_INVALID)gap_disconnect(handle);else lost();return true;}
  const auto psm=l2cap_event_channel_opened_get_psm(p);trace(0x71,cid,0);
  if(psm==BLUETOOTH_PSM_HID_CONTROL) {
   control=cid;if(outgoing && !interrupt)last_error=l2cap_create_channel(dispatcher,peer,BLUETOOTH_PSM_HID_INTERRUPT,672,&interrupt);
  }else if(psm==BLUETOOTH_PSM_HID_INTERRUPT) {interrupt=cid;known=true;opened();}
  return true;
 }
 case L2CAP_EVENT_CHANNEL_CLOSED: {
  const auto cid=l2cap_event_channel_closed_get_local_cid(p);if(cid!=control && cid!=interrupt)return false;
  if(cid==control)control=0;else interrupt=0;
  pending_size=0;send_requested=false;
  trace(0x84,cid,0);if(handle!=HCI_CON_HANDLE_INVALID)gap_disconnect(handle);return true;
 }
 case L2CAP_EVENT_CAN_SEND_NOW: {
  const auto cid=l2cap_event_can_send_now_get_local_cid(p);
  if(cid==control) {
   if(control_reply_size && l2cap_send(control,control_reply,control_reply_size)==0)control_reply_size=0;
   return true;
  }
  if(cid!=interrupt)return false;
  send_requested=false;
  if(pending_size && switch_bt_connected()) {
   last_error=l2cap_send(interrupt,pending,pending_size);
   if(!last_error) {pending_size=0;++reports;}
  }return true;
 }
 default:return false;
 }
}
void switch_bt_update(const ControllerState &state) {
 if(!switch_bt_connected()) switch2_wake_input((state.buttons & CONTROLLER_BUTTON_HOME)!=0);
 else switch2_wake_neutral();
 protocol.update_controller(state,static_cast<uint32_t>(time_us_64()));
}
void switch_bt_neutral() {protocol.clear_controller();switch2_wake_neutral();}
void switch_bt_task() {
 if(!working)return;
 switch2_wake_task(switch_bt_connected());
 if(!switch_bt_enabled())return;
 const uint32_t t=now();
 if(save_pending && switch_bt_connected() && static_cast<int32_t>(t-save_after)>=0) {
  void *ctx=nullptr;const btstack_tlv_t *tlv=nullptr;btstack_tlv_get_instance(&tlv,&ctx);
  if(tlv && tlv->store_tag(ctx,HOST_TAG,peer,6)==0)save_pending=false;
  else save_after=t+2000;
 }
 if(known && handle==HCI_CON_HANDLE_INVALID && !control && static_cast<int32_t>(t-retry_at)>=0) {
  outgoing=claimed=true;retry_at=t+10000;
  last_error=l2cap_create_channel(dispatcher,peer,BLUETOOTH_PSM_HID_CONTROL,672,&control);
 }
 if(handle!=HCI_CON_HANDLE_INVALID && !control && t-acl_since>=1000) {
  outgoing=true;last_error=l2cap_create_channel(dispatcher,peer,BLUETOOTH_PSM_HID_CONTROL,672,&control);
 }
 if(handle!=HCI_CON_HANDLE_INVALID && !switch_bt_connected() && t-acl_since>20000) {trace(0x85,handle,host_suspended?1:0);gap_disconnect(handle);}
 if(!switch_bt_connected())return;
 switch_rumble_adapter_task();
 if(!pending_size) {
  uint8_t report[64]{};
  if(protocol.next_report(t,report)) {pending[0]=0xa1;std::memcpy(pending+1,report,49);pending_size=50;}
 }
 if(pending_size && !send_requested) {
  request_send(send_requested,[] {last_error=l2cap_request_can_send_now_event(interrupt);return last_error;});
 }
}
void switch_bt_motion_diagnostics(uint8_t out[55]) {protocol.motion_diagnostics(out);}
void switch_bt_gap_diagnostics(uint8_t out[24]) {protocol.gap_diagnostics(out);}
void switch_bt_event_page(uint8_t out[59],unsigned skip) {
 // 'E', total, then up to 7 events newest-first after skipping `skip` newest.
 std::memset(out,0,59);out[0]='E';out[1]=uint8_t(event_count);
 for(unsigned i=0;i<7 && skip+i<event_count;++i)
  std::memcpy(out+2+i*8,events[(event_head+EVENT_SLOTS-1-skip-i)%EVENT_SLOTS],8);
}
void switch_bt_diagnostics(uint8_t out[20]) {
 std::memset(out,0,20);out[0]='W';out[1]='L';out[2]=1;
 out[3]=(working?1:0)|(known?2:0)|(switch_bt_connected()?4:0)|(switch_bt_enabled()?8:0);out[4]=last_error;
 out[5]=uint8_t(handle);out[6]=uint8_t(handle>>8);out[7]=uint8_t(control);out[8]=uint8_t(interrupt);
 out[17]=last_rx_type;out[18]=last_rx_id;out[19]=bt_matches_controller(peer)?1:0;
 for(unsigned i=0;i<4;++i) {out[9+i]=uint8_t(reports>>(8*i));out[13+i]=uint8_t(commands>>(8*i));}
}

void switch_bt_trace(uint8_t out[26]) {
 const unsigned count=event_count>3?3:event_count;
 out[0]='T';out[1]=uint8_t(count);std::memset(out+2,0,24);
 for(unsigned i=0;i<count;++i)std::memcpy(out+2+i*6,events[(event_head+EVENT_SLOTS-count+i)%EVENT_SLOTS],6);
 bd_addr_t actual{};gap_local_bd_addr(actual);std::memcpy(out+20,actual,6);
}
