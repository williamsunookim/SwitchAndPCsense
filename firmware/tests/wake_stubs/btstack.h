#pragma once
#include <cstdint>
using bd_addr_t = uint8_t[6];
constexpr uint8_t HCI_EVENT_COMMAND_COMPLETE = 0x0e;
constexpr uint16_t HCI_OPCODE_HCI_LE_SET_ADVERTISING_PARAMETERS = 0x2006;
constexpr uint16_t HCI_OPCODE_HCI_LE_SET_ADVERTISING_DATA = 0x2008;
constexpr uint16_t HCI_OPCODE_HCI_LE_SET_ADVERTISE_ENABLE = 0x200a;
constexpr int GAP_RANDOM_ADDRESS_TYPE_OFF = 0;
inline uint16_t hci_event_command_complete_get_command_opcode(const uint8_t *p) {return uint16_t(p[3]) | uint16_t(p[4]<<8);}
inline const uint8_t *hci_event_command_complete_get_return_parameters(const uint8_t *p) {return p+5;}
void gap_random_address_set_mode(int);
void gap_advertisements_set_params(uint16_t,uint16_t,uint8_t,uint8_t,uint8_t *,uint8_t,uint8_t);
void gap_advertisements_set_data(uint8_t,uint8_t *);
void gap_advertisements_enable(int);
