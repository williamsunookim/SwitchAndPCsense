//
// Created by awalol on 2026/5/4.
//

#include "config.h"
#include "effects_runtime.h"

#include <cmath>
#include <cstring>

#include "state_mgr.h"
#include "utils.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/btstack_flash_bank.h"

constexpr uint32_t CONFIG_MAGIC = 0x66ccff00;
constexpr uint16_t CONFIG_VERSION = 5; // 如果想要强制重置配置，再更新 CONFIG_VERSION。
// RP2350 SDK reserves the final sector for erratum E10. Keep settings before
// BTstack's two banks, without moving the pairing banks used by the base image.
constexpr uint32_t CONFIG_FLASH_OFFSET = PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE;
constexpr uint32_t LEGACY_CONFIG_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
static_assert(CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_BANK_STORAGE_OFFSET);
constexpr uint32_t USB_MODE_MAGIC = 0x4d425355; // "USBM"

struct __attribute__((packed)) UsbModeRecord {
    uint32_t magic;
    uint8_t mode;
    uint8_t mode_inverse;
};

constexpr size_t EFFECTS_OFFSET = 64;
struct EffectsRecord { uint32_t magic; TriggerSettings settings; uint8_t reserved[3]; uint32_t crc; };
constexpr uint32_t EFFECTS_MAGIC = 0x31524654;
static_assert(EFFECTS_OFFSET + sizeof(EffectsRecord) <= FLASH_PAGE_SIZE);
static_assert(sizeof(Config) + sizeof(UsbModeRecord) <= EFFECTS_OFFSET);
static Config config{};
static uint8_t usb_output_mode = 0;
bool is_dse = false;

// 编译期保护
// 判断Config结构体是否能放进flash 256bytes
static_assert(sizeof(Config) <= FLASH_PAGE_SIZE);
static_assert(sizeof(Config) + sizeof(UsbModeRecord) <= FLASH_PAGE_SIZE);
// 配置区起始地址必须按 flash sector 对齐。
static_assert(CONFIG_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

uint32_t calc_config_crc(const Config &con) {
    return crc32(reinterpret_cast<const uint8_t *>(&con.body), sizeof(Config_body));
}

const Config *flash_config() {
    return reinterpret_cast<const Config *>(XIP_BASE + CONFIG_FLASH_OFFSET);
}

const uint8_t *flash_usb_mode_record() {
    return reinterpret_cast<const uint8_t *>(
        XIP_BASE + CONFIG_FLASH_OFFSET + sizeof(Config));
}

bool usb_mode_record_valid(const UsbModeRecord &record) {
    return record.magic == USB_MODE_MAGIC && record.mode <= 1 &&
           record.mode_inverse == static_cast<uint8_t>(record.mode ^ 0xffu);
}

void config_valid() {
    // valid config and set default value
    if (config.magic != CONFIG_MAGIC) {
        config.magic = CONFIG_MAGIC;
        printf("[Config] Config Magic Header is invalid\n");
    }
    if (config.size != sizeof(Config_body)) {
        config.size = sizeof(Config_body);
        printf("[Config] Config Body size is invalid\n");
    }
    auto body = &config.body;
    if (body->config_version != CONFIG_VERSION) {
        memset(body, 0xFF, sizeof(Config_body));
        body->config_version = CONFIG_VERSION;
        printf("[Config] Warning: Config may breaking change. Reset to default\n");
    }
    if (std::isnan(body->haptics_gain) || body->haptics_gain < 1.0f || body->haptics_gain > 2.0f) {
        body->haptics_gain = 1.0f;
        printf("[Config] Haptics Gain value is invalid\n");
    }
    if (body->speaker_volume < 0 || body->speaker_volume > 127) {
        body->speaker_volume = 100;
        printf("[Config] Speaker Volume is invalid\n");
    }
    if (body->headset_volume < 0 || body->headset_volume > 127) {
        body->headset_volume = 100;
        printf("[Config] Headset Volume is invalid\n");
    }
    if (body->speaker_gain < 0 || body->speaker_gain > 7) {
        body->speaker_gain = 2;
        printf("[Config] speaker_gain is invalid\n");
    }
    if (body->inactive_time < 0 || body->inactive_time > 60) {
        body->inactive_time = 30;
        printf("[Config] Inactive time is invalid\n");
    }
    if (body->disable_pico_led > 1) {
        body->disable_pico_led = 0;
        printf("[Config] disable_pico_led is invalid\n");
    }
    if (body->polling_rate_mode > 2) {
        body->polling_rate_mode = 1;
        printf("[Config] polling_rate_mode is invalid\n");
    }
    if (body->audio_buffer_length < 16 || body->audio_buffer_length > 128) {
        body->audio_buffer_length = 64;
        printf("[Config] haptics_buffer_length is invalid\n");
    }
    if (body->controller_mode > 2) {
        body->controller_mode = 2;
        printf("[Config] controller_mode is invalid\n");
    }
    if (body->enable_usb_sn > 1) {
        body->enable_usb_sn = 0;
        printf("[Config] Warning: enable_usb_sn is invalid\n");
    }
    if (body->ps_shortcut_enabled > 1) {
        body->ps_shortcut_enabled = 0;
        printf("[Config] ps_shortcut_enabled is invalid\n");
    }
    if (body->disable_mic > 1) {
        body->disable_mic = 0;
        printf("[Config] disable_mic is invalid\n");
    }
    if (body->disable_speaker > 1) {
        body->disable_speaker = 0;
        printf("[Config] disable_speaker is invalid\n");
    }
    if (body->enable_wake > 1) {
        body->enable_wake = 0;
        printf("[Config] enable_wake is invalid\n");
    }
    if (body->trigger_reduce > 10) {
        body->trigger_reduce = 0;
        printf("[Config] trigger_reduce is invalid\n");
    }
}

void config_load() {
    // Read legacy settings once when upgrading; all subsequent writes use the
    // dedicated sector. The BTstack link-key storage address remains unchanged.
    const uint32_t source = flash_config()->magic == CONFIG_MAGIC ? CONFIG_FLASH_OFFSET : LEGACY_CONFIG_OFFSET;
    const auto *stored = reinterpret_cast<const uint8_t *>(XIP_BASE + source);
    EffectsRecord effects{};
    std::memcpy(&effects, stored + EFFECTS_OFFSET, sizeof(effects));
    if (effects.magic == EFFECTS_MAGIC && trigger_settings_valid(effects.settings) &&
        effects.crc == crc32(reinterpret_cast<const uint8_t *>(&effects.settings), sizeof(TriggerSettings)))
        effects_load(effects.settings);
    memcpy(&config, stored, sizeof(Config));

    if (config.magic == CONFIG_MAGIC && config.crc32 != calc_config_crc(config))
        config.body.config_version = 0xff;
    config_valid();

    UsbModeRecord record{};
    memcpy(&record, stored + sizeof(Config), sizeof(record));
    if (usb_mode_record_valid(record)) {
        usb_output_mode = record.mode;
    } else {
        usb_output_mode = 0;
    }
}

// Runs with core1 parked (flash_safe_execute) and core0 interrupts disabled, so
// neither core touches XIP flash while the sector is erased/programmed. Without
// the core1 park this races the audio core and corrupts audio (buzzing).
static void config_save_flash_op(void *param) {
    const uint8_t *page = static_cast<const uint8_t *>(param);
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);
}

bool config_save() {
    config.crc32 = calc_config_crc(config);
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &config, sizeof(Config));
    const UsbModeRecord mode_record{
        .magic = USB_MODE_MAGIC,
        .mode = usb_output_mode,
        .mode_inverse = static_cast<uint8_t>(usb_output_mode ^ 0xffu),
    };
    memcpy(page + sizeof(Config), &mode_record, sizeof(mode_record));
    EffectsRecord effects{}; effects.magic = EFFECTS_MAGIC; effects.settings = effects_settings();
    effects.crc = crc32(reinterpret_cast<const uint8_t *>(&effects.settings), sizeof(TriggerSettings));
    std::memcpy(page + EFFECTS_OFFSET, &effects, sizeof(effects));

    const int rc = flash_safe_execute(config_save_flash_op, page, 1000);
    if (rc != PICO_OK) {
        printf("[Config] config_save flash_safe_execute failed: %d\n", rc);
        return false;
    }

    Config verify{};
    memcpy(&verify, flash_config(), sizeof(verify));
    UsbModeRecord verify_mode{};
    memcpy(&verify_mode, flash_usb_mode_record(), sizeof(verify_mode));
    const auto verify_crc32 = calc_config_crc(verify);
    if (std::memcmp(reinterpret_cast<const void *>(XIP_BASE + CONFIG_FLASH_OFFSET), page, FLASH_PAGE_SIZE) == 0 &&
        verify_crc32 == config.crc32 && usb_mode_record_valid(verify_mode) &&
        verify_mode.mode == usb_output_mode) {
        printf("[Config] Config write flash verify success\n");
        return true;
    }
    printf("[Config] Config write flash verify failed\n");
    return false;
}

Config_body& get_config() {
    return config.body;
}

uint8_t config_get_usb_output_mode() {
    return usb_output_mode;
}

void config_set_usb_output_mode(uint8_t mode) {
    usb_output_mode = mode <= 1 ? mode : 0;
}

void set_config(const uint8_t *new_config, const uint16_t len) {
    const auto copy_len = len < sizeof(Config_body) ? len : sizeof(Config_body);
    memcpy(&config.body, new_config, copy_len);
    config_valid();
    if (config.body.disable_pico_led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    }
    set_volume(config.body.speaker_volume,config.body.headset_volume);
    if (config.body.speaker_gain != 0) {
        set_gain(config.body.speaker_gain);
    }
    if (config.body.trigger_reduce != 0) {
        set_trigger_reduce(config.body.trigger_reduce);
    }
}

void set_config(const Config_body &new_config) {
    config.body = new_config;
    config_valid();
}
