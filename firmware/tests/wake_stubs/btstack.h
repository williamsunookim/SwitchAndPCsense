#pragma once
#include <cstdint>
using bd_addr_t = uint8_t[6];
constexpr uint8_t HCI_EVENT_COMMAND_COMPLETE = 0x0e;
constexpr uint8_t GAP_EVENT_ADVERTISING_REPORT = 0xda;
constexpr uint16_t HCI_OPCODE_HCI_LE_SET_SCAN_PARAMETERS = 0x200b;
constexpr uint16_t HCI_OPCODE_HCI_LE_SET_SCAN_ENABLE = 0x200c;
inline uint8_t gap_event_advertising_report_get_data_length(const uint8_t *p) {return p[11];}
inline uint8_t gap_event_advertising_report_get_address_type(const uint8_t *p) {return p[3];}
inline const uint8_t *gap_event_advertising_report_get_data(const uint8_t *p) {return p+12;}
inline void gap_event_advertising_report_get_address(const uint8_t *p, uint8_t *a) {for(unsigned i=0;i<6;++i)a[i]=p[9-i];}
void gap_set_scan_params(uint8_t,uint16_t,uint16_t,uint8_t);
void gap_set_scan_duplicate_filter(bool);
void gap_start_scan();
void gap_stop_scan();
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
