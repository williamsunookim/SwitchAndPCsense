#pragma once
enum hid_report_type_t { HID_REPORT_TYPE_INVALID, HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT, HID_REPORT_TYPE_FEATURE };
bool tud_mounted();
bool tud_suspended();

#include <cstdint>
#define TUSB_DESC_DEVICE 1
#define TUSB_DESC_CONFIGURATION 2
#define TUSB_DESC_INTERFACE 4
#define TUSB_DESC_ENDPOINT 5
#define TUSB_CLASS_HID 3
#define HID_DESC_TYPE_HID 0x21
#define HID_DESC_TYPE_REPORT 0x22
#define TUSB_XFER_INTERRUPT 3
bool tud_hid_ready();
bool tud_hid_report(uint8_t id, const void *buffer, uint16_t size);
