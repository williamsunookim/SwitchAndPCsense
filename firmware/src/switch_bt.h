// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "controller_state.h"
#ifdef SWITCH_BT_WIRELESS
#include "btstack_defines.h"
inline bool switch_bt_enabled() { return true; }
void switch_bt_init(btstack_packet_handler_t dispatch);
bool switch_bt_hci(uint8_t *packet,uint16_t size);
bool switch_bt_l2cap(uint8_t type,uint16_t channel,uint8_t *packet,uint16_t size);
void switch_bt_task();
void switch_bt_update(const ControllerState &state);
void switch_bt_neutral();
void switch_bt_pair();
bool switch_bt_connected();
bool switch_bt_is_peer(const uint8_t address[6]);
void switch_bt_diagnostics(uint8_t out[20]);
void switch_bt_trace(uint8_t out[26]);
void switch_bt_motion_diagnostics(uint8_t out[55]);
void switch_bt_gap_diagnostics(uint8_t out[24]);
void switch_bt_event_page(uint8_t out[59],unsigned skip);
#else
inline bool switch_bt_enabled() { return false; }
inline bool switch_bt_connected() { return false; }
#endif
