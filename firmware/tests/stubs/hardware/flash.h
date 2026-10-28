#pragma once
#include <cstdint>
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#define PICO_FLASH_SIZE_BYTES (2u*1024u*1024u)
extern uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE reinterpret_cast<uintptr_t>(fake_flash)
void flash_range_erase(uint32_t offset, size_t size);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t size);
