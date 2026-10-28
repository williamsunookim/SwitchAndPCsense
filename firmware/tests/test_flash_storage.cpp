#include <cassert>
#include <cstring>
#include <iostream>
#include "config.h"
#include "effects_runtime.h"
#include "hardware/flash.h"
#include "pico/btstack_flash_bank.h"
#include "utils.h"
uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
constexpr uint32_t storage=PICO_FLASH_BANK_STORAGE_OFFSET-FLASH_SECTOR_SIZE;
constexpr uint32_t legacy=PICO_FLASH_SIZE_BYTES-FLASH_SECTOR_SIZE;
TriggerSettings settings{};
unsigned erases=0;
bool fail_safe=false;
const TriggerSettings &effects_settings() { return settings; }
void effects_load(const TriggerSettings &s) { settings=s; }
void set_volume(uint8_t,uint8_t) {}
void set_gain(uint8_t) {}
void set_trigger_reduce(uint8_t) {}
void flash_range_erase(uint32_t offset,size_t size) {
    assert(offset==storage && size==FLASH_SECTOR_SIZE); ++erases;
    std::memset(fake_flash+offset,0xff,size);
}
void flash_range_program(uint32_t offset,const uint8_t *p,size_t n) {
    assert(offset==storage && n==FLASH_PAGE_SIZE); std::memcpy(fake_flash+offset,p,n);
}
int flash_safe_execute(void (*op)(void *),void *arg,unsigned) {
    if (fail_safe) return -1;
    op(arg); return 0;
}
int main() {
    std::memset(fake_flash,0xff,sizeof(fake_flash)); config_load();
    assert(config_get_usb_output_mode()==0 && get_config().config_version==5);
    settings=botw_bow_settings(); config_set_usb_output_mode(1);
    const Switch2WakeSettings wake{{0x98,0xe2,0x55,1,2,3},{0x98,0xe2,0x55,4,5,6},0x2069};
    config_set_switch2_wake(wake);
    assert(config_save() && erases==1);
    uint8_t saved[FLASH_PAGE_SIZE]; std::memcpy(saved,fake_flash+storage,sizeof(saved));
    settings={}; config_set_usb_output_mode(0); config_load();
    assert(config_get_usb_output_mode()==1 && settings.profile==1);
    assert(std::memcmp(&config_get_switch2_wake(), &wake, sizeof(wake))==0);
    // Migration preserves mode/settings without touching the SDK's pairing banks.
    std::memset(fake_flash+storage,0xff,FLASH_SECTOR_SIZE);
    std::memset(fake_flash+PICO_FLASH_BANK_STORAGE_OFFSET,0x5a,PICO_FLASH_BANK_TOTAL_SIZE);
    std::memcpy(fake_flash+legacy,saved,sizeof(saved));
    uint8_t pairing_banks[PICO_FLASH_BANK_TOTAL_SIZE];
    std::memcpy(pairing_banks,fake_flash+PICO_FLASH_BANK_STORAGE_OFFSET,sizeof(pairing_banks));
    settings={}; config_load(); assert(settings.profile==1 && config_get_usb_output_mode()==1);
    assert(config_save());
    assert(std::memcmp(pairing_banks,fake_flash+PICO_FLASH_BANK_STORAGE_OFFSET,sizeof(pairing_banks))==0);
    assert(std::memcmp(fake_flash+legacy,saved,sizeof(saved))==0);
    assert(std::memcmp(&config_get_switch2_wake(), &wake, sizeof(wake))==0);
    fake_flash[storage+128+4]^=1; config_load();
    assert(!switch2_wake_valid(config_get_switch2_wake()));
    // A corrupted effects record cannot load unchecked motor parameters.
    fake_flash[storage+64+4+1]^=3; settings={}; config_load(); assert(settings.profile==0);
    // Body corruption resets gain rather than propagating a bad float.
    fake_flash[storage+offsetof(Config,body)+1]^=0xff;
    config_load(); assert(get_config().haptics_gain==1.0f);
    fail_safe=true; const unsigned before=erases; assert(!config_save() && erases==before);
    std::cout << "flash storage tests passed\n";
}
